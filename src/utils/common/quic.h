/*	Copyright (C) 2020 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

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

#pragma once

#include <stdbool.h>
#include <poll.h>
#include <netdb.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "contrib/quicly/quicly.h"

/*! \brief QUIC params. */
typedef struct {
	/*! Use QUIC indicator. */
	bool enable;
	/*! Server hostname */
	char *hostname;
} quic_params_t;

/*! \brief QUIC context. */
typedef struct {
	/*! QUIC parameters. */
	const quic_params_t *params;
	/*! Poll timeout. */
	int wait;
	/*! Receive fd pool. */
	struct pollfd pfd;
	/*! Quicly context. */
	quicly_context_t quicly;
	/*! 
	 *  !Caution! Do not move this struct.
	 *  Function `quic_conn_get_ctx` use
	 *  static position in struct.
	 */
	struct recv_buf {
		uint8_t *base;
		uint8_t *ptr;
		size_t capacity;
	} buf;
	/*! Quicly connection context. */
	quicly_conn_t *client;
	/*! Connection ID. */
	quicly_cid_plaintext_t cid;
} quic_ctx_t;

/*! \brief Initialize QUIC context structure. */
int quic_ctx_init(quic_ctx_t *ctx, const quic_params_t *params, const int wait);

/*! \brief Connect to remote server. */
int quic_ctx_connect(quic_ctx_t *ctx, const int fd, struct sockaddr *sa, const char *remote_str);

/*! \brief Stores data in send stream.
 *  Data are not sent immediately, they are stored in stack and sent when stream is opened.
 */
int quic_ctx_send(quic_ctx_t *ctx, const uint8_t *buf, const size_t buflen);

/*! \brief Receive data from QUIC stream. */
int quic_ctx_receive(quic_ctx_t *ctx, uint8_t *buf, size_t buflen);

/*! \brief Close all streams and then connection itself. */
void quic_ctx_close(quic_ctx_t *ctx);

struct recv_buf *quic_conn_get_ctx(quicly_conn_t *conn);