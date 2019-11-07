/*  Copyright (C) 2019 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

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
#include <pthread.h>

#include "libknot/libknot.h"
#include "knot/common/log.h"
#include "knot/zone/check.h"
#include "knot/zone/adds_tree.h"

#define nassert(apex, node, cond) if (!(cond)) { \
	knot_dname_txt_storage_t owner; \
	(void)knot_dname_to_str(owner, (node)->owner, sizeof(owner)); \
	log_zone_info(apex, "DBG, assert node %s '%s'\n", owner, #cond); \
	ret = KNOT_ERROR; \
}

int node_check_basic(const zone_node_t *node, const knot_dname_t *apex,
		     const char *info, const zone_node_t *base_node)
{
	int ret = KNOT_EOK;
	assert(node->owner != NULL);
	if (node->rrset_count > 0) {
		nassert(apex, node, node->rrs != NULL);
	}
	if (node->flags & NODE_FLAGS_BINODE) {
		const zone_node_t *counter = binode_counterpart((zone_node_t *)node);
		nassert(apex, node, counter->flags & NODE_FLAGS_BINODE);
		nassert(apex, node, (node->flags ^ counter->flags) & NODE_FLAGS_SECOND);
		nassert(apex, node, counter->owner != NULL);
		nassert(apex, node, counter->owner == node->owner);
		nassert(apex, counter, node->owner == counter->owner);
		if (counter->rrset_count != node->rrset_count) {
			nassert(apex, node, counter->rrs != node->rrs);
		}
	}
	if (ret != KNOT_EOK) {
		knot_dname_txt_storage_t base_owner;
		(void)knot_dname_to_str(base_owner, base_node->owner, sizeof(base_owner));
		log_zone_info(apex, "DBG, ^ %s basic check failed for %s", info, base_owner);
	}
	return ret;
}

int node_check_links(const zone_node_t *node, const knot_dname_t *apex)
{
	int ret = node_check_basic(node, apex, "node", node);
	if (ret == KNOT_EOK && (node->flags & NODE_FLAGS_NSEC3_NODE)) {
		ret = node_check_basic(node->nsec3_node, apex, "nsec3", node);
	}
	if (ret == KNOT_EOK && knot_dname_cmp(node->owner, apex) != 0) {
		nassert(apex, node, node->parent != NULL);
		if (ret == KNOT_EOK) {
			ret = node_check_basic(node->parent, apex, "parent", node);
		}
	}

	for (int i = 0; ret == KNOT_EOK && i < node->rrset_count; i++) {
		const struct rr_data *rrs = &node->rrs[i];
		for (int j = 0; ret == KNOT_EOK && rrs->additional != NULL && j < rrs->additional->count; j++) {
			ret = node_check_basic(rrs->additional->glues[j].node, apex, "glue", node);
		}
	}

	return ret;
}

int node_check_unified(const zone_node_t *node, const knot_dname_t *apex)
{
	const zone_node_t *counter = binode_counterpart((zone_node_t *)node);
	int ret = KNOT_EOK;
	nassert(apex, node, counter != NULL);
	nassert(apex, node, counter->children == node->children);
	if ((counter->flags ^ node->flags) != NODE_FLAGS_SECOND) {
		log_zone_info(apex, "DBG, node flags %hu, counter flags %hu\n", node->flags, counter->flags);
	}
	nassert(apex, node, counter->nsec3_hash == node->nsec3_hash);
	nassert(apex, node, counter->nsec3_wildcard_name == node->nsec3_wildcard_name);
	nassert(apex, node, counter->rrs == node->rrs);
	nassert(apex, node, counter->rrset_count == node->rrset_count);
	return ret;
}

typedef struct {
	pthread_t thread;
	const zone_contents_t *contents;
	size_t node_index;
	size_t thread_id;
	size_t threads;
	int ret;
	bool shall_unified;
} check_ctx_t;

static int check_one(const zone_node_t *node, void *ctx)
{
	check_ctx_t *check = ctx;
	if ((check->node_index++ % check->threads) == check->thread_id) {
		int ret = node_check_links(node, check->contents->apex->owner);
		if (ret == KNOT_EOK && check->shall_unified) {
			ret = node_check_unified(node, check->contents->apex->owner);
		}
		return ret;
	}
	return KNOT_EOK;
}

static void *check_all(void *ctx)
{
	check_ctx_t *check = ctx;
	check->ret = zone_tree_apply(check->contents->nodes, (zone_tree_apply_cb_t)check_one, ctx);
	if (check->ret == KNOT_EOK && check->contents->nsec3_nodes != NULL) {
		check->ret = zone_tree_apply(check->contents->nsec3_nodes, (zone_tree_apply_cb_t)check_one, ctx);
	}
	return NULL;
}

#define for_threads for (size_t i = 0; i < threads; i++)

int zone_contents_check(const zone_contents_t *contents, bool shall_unified)
{
	size_t threads = CHECK_THREADS;
	check_ctx_t ctxs[CHECK_THREADS];
	int ret = KNOT_EOK;

	for_threads {
		ctxs[i].contents = contents;
		ctxs[i].node_index = 0;
		ctxs[i].thread_id = i;
		ctxs[i].threads = threads;
		ctxs[i].shall_unified = shall_unified;
		(void)pthread_create(&ctxs[i].thread, NULL, check_all, &ctxs[i]);
	}

	for_threads {
		(void)pthread_join(ctxs[i].thread, NULL);
		if (ret == KNOT_EOK) {
			ret = ctxs[i].ret;
		}
	}

	return ret;
}