// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <asm-generic/int-ll64.h>
#include <linux/blk_types.h>
#include <linux/fortify-string.h>
#include <linux/gfp_types.h>
#include <linux/slab.h>
#include <linux/stddef.h>

#include "include/module/lz4e_stats.h"

#include "include/module/lz4e_static.h"

void lz4e_stats_free(struct lz4e_stats *lzstats)
{
	kfree(lzstats);

	LZ4E_PR_DEBUG("released request stats");
}

struct lz4e_stats *lz4e_stats_alloc(void)
{
	struct lz4e_stats *lzstats;

	lzstats = kzalloc(sizeof(*lzstats), GFP_KERNEL);
	if (!lzstats) {
		LZ4E_PR_ERR("failed to allocate request stats");
		return NULL;
	}

	LZ4E_PR_DEBUG("allocated request stats");
	return lzstats;
}

void lz4e_stats_update(struct lz4e_stats *lzstats, struct bio *bio)
{
	atomic64_inc(&lzstats->reqs_total);

	if (bio->bi_status != BLK_STS_OK) {
		atomic64_inc(&lzstats->reqs_failed);
		return;
	}

	atomic64_add((s64)bio->bi_vcnt, &lzstats->vec_count);
	atomic64_add((s64)bio->bi_iter.bi_size, &lzstats->data_in_bytes);

	LZ4E_PR_DEBUG("updated request stats");
}

void lz4e_stats_reset(struct lz4e_stats *lzstats)
{
	memset(lzstats, 0, sizeof(*lzstats));

	LZ4E_PR_DEBUG("reset request stats");
}
