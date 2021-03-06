/*  Copyright (C) 2020 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <errno.h>
#include <ifaddrs.h>
#include <poll.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/resource.h>

#include "libknot/libknot.h"
#include "contrib/openbsd/strlcpy.h"

#include "load_queries.h"

volatile bool dns_xdp_trigger = false;

pthread_mutex_t global_mutex;
uint64_t global_pkts_sent = 0;
uint64_t global_pkts_recv = 0;
uint64_t global_size_recv = 0;

#define LOCAL_PORT_MIN  1024
#define LOCAL_PORT_MAX 65535

#define LISTEN_PORT	KNOT_XDP_LISTEN_PORT_ALL

typedef struct {
	char		dev[IFNAMSIZ];
	uint64_t	qps, duration;
	unsigned	at_once;
	uint8_t		local_mac[6], target_mac[6];
	struct in_addr	local_ipv4, target_ipv4;
	struct in6_addr local_ipv6, target_ipv6;
	bool		ipv6;
	uint16_t	target_port;
	unsigned	n_threads, thread_id;
} dns_xdp_gun_ctx_t;

inline static void timer_start(struct timespec *timesp)
{
	clock_gettime(CLOCK_MONOTONIC, timesp);
}

inline static uint64_t timer_end(struct timespec *timesp)
{
	struct timespec end;
	clock_gettime(CLOCK_MONOTONIC, &end);
	uint64_t res = (end.tv_sec - timesp->tv_sec) * (uint64_t)1000000;
	res += ((int64_t)end.tv_nsec - timesp->tv_nsec) / 1000;
	return res;
}

static void set_sockaddr(void *sa_in, struct in_addr *addr, uint16_t port)
{
	struct sockaddr_in *saddr = sa_in;
	saddr->sin_family = AF_INET;
	saddr->sin_port = htobe16(port);
	saddr->sin_addr = *addr;
}

static void set_sockaddr6(void *sa_in, struct in6_addr *addr, uint16_t port)
{
	struct sockaddr_in6 *saddr = sa_in;
	saddr->sin6_family = AF_INET6;
	saddr->sin6_port = htobe16(port);
	saddr->sin6_addr = *addr;
}

inline static void set_pkt_vars(struct iovec pkt, uint16_t ord)
{
	*(uint16_t *)(pkt.iov_base + 0) = htobe16(ord); // transaction ID
}

static void next_payload(struct pkt_payload **payload, int increment)
{
	if (*payload == NULL) {
		*payload = global_payloads;
	}
	for (int i = 0; i < increment; i++) {
		if ((*payload)->next == NULL) {
			*payload = global_payloads;
		} else {
			*payload = (*payload)->next;
		}
	}
}

static int alloc_pkts(knot_xdp_msg_t *pkts, int npkts, struct knot_xdp_socket *xsk,
                      dns_xdp_gun_ctx_t *ctx, uint64_t tick, struct pkt_payload **payl)
{
	uint64_t unique = (tick * ctx->n_threads + ctx->thread_id) * ctx->at_once;

	for (int i = 0; i < npkts; i++) {
		int ret = knot_xdp_send_alloc(xsk, ctx->ipv6, &pkts[i], NULL);
		if (ret != KNOT_EOK) {
			return ret;
		}

		uint16_t local_port = LOCAL_PORT_MIN + unique % (LOCAL_PORT_MAX + 1 - LOCAL_PORT_MIN);
		if (ctx->ipv6) {
			set_sockaddr6(&pkts[i].ip_from, &ctx->local_ipv6, local_port);
			set_sockaddr6(&pkts[i].ip_to, &ctx->target_ipv6, ctx->target_port);
		} else {
			set_sockaddr(&pkts[i].ip_from, &ctx->local_ipv4, local_port);
			set_sockaddr(&pkts[i].ip_to, &ctx->target_ipv4, ctx->target_port);
		}

		memcpy(pkts[i].eth_from, ctx->local_mac, 6);
		memcpy(pkts[i].eth_to, ctx->target_mac, 6);

		memcpy(pkts[i].payload.iov_base, (*payl)->payload, (*payl)->len);
		pkts[i].payload.iov_len = (*payl)->len;

		set_pkt_vars(pkts[i].payload, unique & 0xffff);

		unique++;
		next_payload(payl, ctx->n_threads);
	}
	return KNOT_EOK;
}

void *dns_xdp_gun_thread(void *_ctx)
{
	dns_xdp_gun_ctx_t *ctx = _ctx;
	struct knot_xdp_socket *xsk;
	struct timespec timer;
	knot_xdp_msg_t pkts[ctx->at_once];
	uint64_t tot_sent = 0, tot_recv = 0, tot_size = 0;
	uint64_t duration = 0;

	knot_xdp_load_bpf_t mode = (ctx->thread_id == 0 ?
	                            KNOT_XDP_LOAD_BPF_ALWAYS : KNOT_XDP_LOAD_BPF_NEVER);
	int ret = knot_xdp_init(&xsk, ctx->dev, ctx->thread_id, LISTEN_PORT, mode);
	if (ret != KNOT_EOK) {
		printf("failed to init XDP socket#%u: %s\n", ctx->thread_id, knot_strerror(ret));
		return NULL;
	}

	struct pollfd pfd = { knot_xdp_socket_fd(xsk), POLLIN, 0 };

	while (!dns_xdp_trigger) {
		usleep(1000);
	}

	uint64_t tick = 0;
	struct pkt_payload *payload_ptr = NULL;
	next_payload(&payload_ptr, ctx->thread_id);

	timer_start(&timer);

	while (duration < ctx->duration + 1000000) {

		// sending part
		if (duration < ctx->duration) {
			knot_xdp_send_prepare(xsk);
			ret = alloc_pkts(pkts, ctx->at_once, xsk, ctx, tick, &payload_ptr);
			if (ret != KNOT_EOK) {
				printf("thread#%u alloc_pkts failed: %s\n", ctx->thread_id, knot_strerror(ret));
				goto end;
			}

			uint32_t really_sent = 0;
			ret = knot_xdp_send(xsk, pkts, ctx->at_once, &really_sent);
			if (ret != KNOT_EOK) {
				printf("thread#%u send_pkts failed: %s\n", ctx->thread_id, knot_strerror(ret));
				goto end;
			}
			assert(really_sent == ctx->at_once);
			tot_sent += really_sent;

			ret = knot_xdp_send_finish(xsk);
			if (ret != KNOT_EOK) {
				printf("thread#%u flush failed: %s\n", ctx->thread_id, knot_strerror(ret));
				goto end;
			}
		}

		// receiving part
		while (1) {
			ret = poll(&pfd, 1, 0);
			if (ret < 0) {
				printf("thread#%u poll failed: %s\n", ctx->thread_id, strerror(errno));
				goto end;
			}
			if (!pfd.revents) {
				break;
			}

			uint32_t recvd = 0;
			ret = knot_xdp_recv(xsk, pkts, ctx->at_once, &recvd);
			if (ret != KNOT_EOK) {
				printf("thread#%u recv_pkts failed: %s\n", ctx->thread_id, knot_strerror(ret));
				goto end;
			}
			for (int i = 0; i < recvd; i++) {
				tot_size += pkts[i].payload.iov_len; // FIXME this size is only DNS payload w/o IP+UDP
			}
			knot_xdp_recv_finish(xsk, pkts, recvd);
			tot_recv += recvd;
			pfd.revents = 0;
		}

		// speed part
		uint64_t dura_exp = (tot_sent * 1000000) / ctx->qps;
		duration = timer_end(&timer);
		if (dura_exp > duration) {
			usleep(dura_exp - duration);
		}
		if (duration > ctx->duration) {
			usleep(1000);
		}
		tick++;
	}

end:
	knot_xdp_deinit(xsk);

	printf("thread#%u sent %lu recvd %lu\n", ctx->thread_id, tot_sent, tot_recv);
	pthread_mutex_lock(&global_mutex);
	global_pkts_sent += tot_sent;
	global_pkts_recv += tot_recv;
	global_size_recv += tot_size;
	pthread_mutex_unlock(&global_mutex);

	return NULL;
}

static int dev2mac(const char *dev, uint8_t *mac)
{
	struct ifreq ifr;
	memset(&ifr, 0, sizeof(ifr));
	int fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (fd < 0) {
		return -errno;
	}
	strlcpy(ifr.ifr_name, dev, IFNAMSIZ);

	int ret = ioctl(fd, SIOCGIFHWADDR, &ifr);
	if (ret >= 0) {
		memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
	} else {
		ret = -errno;
	}
	close(fd);
	return ret;
}

static int send_pkt_to(void *ip, bool ipv6)
{
	int fd = socket(ipv6 ? AF_INET6 : AF_INET, SOCK_RAW, ipv6 ? IPPROTO_ICMPV6 : IPPROTO_ICMP);
	if (fd < 0) {
		return -errno;
	}

	struct sockaddr_in6 s = { 0 };
	struct sockaddr_in *sin = (struct sockaddr_in *)&s;
	struct sockaddr_in6 *sin6 = &s;
	if (ipv6) {
		sin6->sin6_family = AF_INET6;
		memcpy(&sin6->sin6_addr, ip, sizeof(struct in6_addr));
	} else {
		sin->sin_family = AF_INET;
		memcpy(&sin->sin_addr, ip, sizeof(struct in_addr));
	}

	static const uint8_t dummy_pkt[] = {
		0x08, 0x00, 0xec, 0x72, 0x0b, 0x87, 0x00, 0x06,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // padding
	};
	static const size_t dummy_pkt_size = sizeof(dummy_pkt);

	int ret = sendto(fd, dummy_pkt, dummy_pkt_size, 0, &s, ipv6 ? sizeof(struct sockaddr_in6) : sizeof(struct sockaddr_in));
	if (ret < 0) {
		ret = -errno;
	}
	close(fd);
	return ret;
}

static bool str2mac(const char *str, uint8_t mac[])
{
	unsigned mac_int[6] = { 0 };

	int ret = sscanf(str, "%x:%x:%x:%x:%x:%x", &mac_int[0], &mac_int[1],
	                 &mac_int[2], &mac_int[3], &mac_int[4], &mac_int[5]);
	if (ret != 6) {
		return false;
	}
	for (int i = 0; i < 6; i++) {
		if (mac_int[i] > 0xff) {
			return false;
		}
		mac[i] = mac_int[i];
	}
	return true;
}

static int ip_route_get(const char *ip, const char *what, char **res)
{
	char cmd[50 + strlen(ip) + strlen(what)];
	(void)snprintf(cmd, sizeof(cmd), "ip route get %s | grep -o ' %s [^ ]* '", ip, what);

	FILE *p = popen(cmd, "r");
	if (p == NULL) {
		return knot_map_errno();
	}

	char check[16] = { 0 }, got[256] = { 0 };
	if (fscanf(p, "%15s%255s", check, got) != 2 ||
	    strcmp(check, what) != 0) {
		int ret = feof(p) ? KNOT_ENOENT : KNOT_EMALF;
		pclose(p);
		return ret;
	}
	pclose(p);

	*res = strdup(got);
	return *res == NULL ? KNOT_ENOMEM : KNOT_EOK;
}

static int remoteIP2MAC(const char *ip_str, bool ipv6, char devname[], uint8_t remote_mac[])
{
	FILE *p = popen(ipv6 ? "ip -6 neigh" : "arp -ne", "r");
	if (p == NULL) {
		return knot_map_errno();
	}

	char line_buf[1024] = { 0 };
	int ret = KNOT_ENOENT;
	while (fgets(line_buf, sizeof(line_buf) - 1, p) != NULL && ret == KNOT_ENOENT) {
		char fields[5][strlen(line_buf) + 1];
		if (sscanf(line_buf, "%s%s%s%s%s", fields[0], fields[1], fields[2], fields[3], fields[4]) != 5) {
			continue;
		}
		if (strcmp(fields[0], ip_str) != 0) {
			continue;
		}
		if (!str2mac(fields[ipv6 ? 4 : 2], remote_mac)) {
			ret = KNOT_EMALF;
		} else {
			strlcpy(devname, fields[ipv6 ? 2 : 4], IFNAMSIZ);
			ret = KNOT_EOK;
		}
	}
	pclose(p);
	return ret;
}

static int distantIP2MAC(const char *ip_str, bool ipv6, char devname[], uint8_t remote_mac[])
{
	char *via = NULL;
	int ret = ip_route_get(ip_str, "via", &via);
	switch (ret) {
	case KNOT_ENOENT: // same subnet, no via
		return remoteIP2MAC(ip_str, ipv6, devname, remote_mac);
	case KNOT_EOK:
		ret = remoteIP2MAC(via, ipv6, devname, remote_mac);
		free(via);
		return ret;
	default:
		return ret;
	}
}

static int remoteIP2local(const char *ip_str, bool ipv6, char devname[], void *local)
{
	char *dev = NULL, *loc = NULL;
	int ret = ip_route_get(ip_str, "dev", &dev);
	if (ret != KNOT_EOK) {
		return ret;
	}
	strlcpy(devname, dev, IFNAMSIZ);
	free(dev);

	ret = ip_route_get(ip_str, "src", &loc);
	if (ret != KNOT_EOK) {
		return ret;
	}
	ret = (inet_pton(ipv6 ? AF_INET6 : AF_INET, loc, local) <= 0 ? KNOT_EMALF : KNOT_EOK);
	free(loc);

	return ret;
}

int main(int argc, char *argv[])
{
	const char *usage = "usage: dns_xdp_gun <qps> <length_s> <target_IP> <target_port> <pkts_at_once> <queries_file>";

	dns_xdp_gun_ctx_t ctx = { { 0 } }, *thread_ctxs = NULL;
	pthread_t *threads = NULL;

	if (argc == 7) {
		int arg = atoi(argv[1]);
		if (arg > 0) {
			ctx.qps = arg;
		} else {
			goto pusage;
		}

		double argf = atof(argv[2]);
		if (argf > 0) {
			ctx.duration = argf * 1000000.0;
			assert(ctx.duration >= 1000);
		} else {
			goto pusage;
		}

		ctx.ipv6 = false;
		if (!inet_aton(argv[3], &ctx.target_ipv4)) {
			ctx.ipv6 = true;
			if (inet_pton(AF_INET6, argv[3], &ctx.target_ipv6) <= 0) {
				printf("invalid target IP\n");
				goto pusage;
			}
		}
		int ret = ctx.ipv6 ? send_pkt_to(&ctx.target_ipv6, true) : send_pkt_to(&ctx.target_ipv4, false);
		if (ret < 0) {
			printf("can't send dummy packet to `%s`: %s\n", argv[3], strerror(-ret));
			goto pusage;
		}
		usleep(10000);

		char dev1[IFNAMSIZ], dev2[IFNAMSIZ];
		ret = distantIP2MAC(argv[3], ctx.ipv6, dev1, ctx.target_mac);
		if (ret != KNOT_EOK) {
			printf("can't get remote MAC of `%s` by ARP query: %s\n", argv[3], knot_strerror(ret));
			goto pusage;
		}
		ret = remoteIP2local(argv[3], ctx.ipv6, dev2, ctx.ipv6 ? (void *)&ctx.local_ipv6 : (void *)&ctx.local_ipv4);
		if (ret != KNOT_EOK) {
			printf("can't get local IP reachig remote `%s`: %s\n", argv[3], knot_strerror(ret));
			goto pusage;
		}
		if (strncmp(dev1, dev2, IFNAMSIZ) != 0) {
			printf("device names comming from `ip` and `arp` differ (%s != %s)\n", dev1, dev2);
			goto pusage;
		} else {
			strlcpy(ctx.dev, dev1, IFNAMSIZ);
		}
		ret = dev2mac(ctx.dev, ctx.local_mac);
		if (ret < 0) {
			printf("failed to get MAC of device `%s`: %s\n", ctx.dev, strerror(-ret));
			goto pusage;
		}

		arg = atoi(argv[4]);
		if (arg > 0 && arg <= 0xffff) {
			ctx.target_port = arg;
		} else {
			goto pusage;
		}

		arg = atoi(argv[5]);
		if (arg > 0) {
			ctx.at_once = arg;
		} else {
			goto pusage;
		}

		arg = knot_eth_queues(ctx.dev);
		if (arg >= 0) {
			ctx.n_threads = arg;
			if (ctx.qps < ctx.n_threads) {
				printf("QPS must be at least the number of threads (%u)\n", ctx.n_threads);
				goto pusage;
			}
			ctx.qps /= ctx.n_threads;
		} else {
			printf("unable to get number of queues for %s: %s\n", ctx.dev, knot_strerror(arg));
			goto pusage;
		}

		thread_ctxs = malloc(ctx.n_threads * sizeof(*thread_ctxs));
		threads = malloc(ctx.n_threads * sizeof(*threads));
		if (thread_ctxs == NULL || threads == NULL) {
			printf("out of memory\n");
			goto pusage;
		}
		for (int i = 0; i < ctx.n_threads; i++) {
			thread_ctxs[i] = ctx;
			thread_ctxs[i].thread_id = i;
		}

		if (!load_queries(argv[6])) {
			goto pusage;
		}
	} else {
		goto pusage;
	}

	struct rlimit no_limit = { RLIM_INFINITY, RLIM_INFINITY };
	int ret = setrlimit(RLIMIT_MEMLOCK, &no_limit);
	if (ret) {
		printf("unable to unset memory lock limit: %s\n", strerror(errno));
		goto pusage;
	}
	pthread_mutex_init(&global_mutex, NULL);

	for (size_t i = 0; i < ctx.n_threads; i++) {
		pthread_create(&threads[i], NULL, dns_xdp_gun_thread, &thread_ctxs[i]);
		usleep((i + 1) * 10000);
	}

	dns_xdp_trigger = true;
	usleep(1000000);
	dns_xdp_trigger = false;

	for (size_t i = 0; i < ctx.n_threads; i++) {
		pthread_join(threads[i], NULL);
	}
	pthread_mutex_destroy(&global_mutex);
	printf("total sent %lu (%lu qps)\n", global_pkts_sent, global_pkts_sent * 1000 / (ctx.duration / 1000));
	if (global_pkts_sent > 0) {
		printf("total recv %lu (%lu qps) (%lu%%)\n", global_pkts_recv,
		       global_pkts_recv * 1000 / (ctx.duration / 1000), global_pkts_recv * 100 / global_pkts_sent);
		printf("avg recv size: %lu B\n", global_pkts_recv > 0 ? global_size_recv / global_pkts_recv : 0);
	}

	free(thread_ctxs);
	free(threads);
	free_global_payloads();
	return 0;

pusage:
	printf("%s\n", usage);
	free(thread_ctxs);
	free(threads);
	return 1;
}
