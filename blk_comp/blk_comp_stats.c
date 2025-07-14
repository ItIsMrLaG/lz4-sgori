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

#include "include/blk_comp_stats.h"

#include "include/blk_comp_static.h"

void LZ4E_stats_free(struct LZ4E_stats *bcstats)
{
	kfree(bcstats);

	LZ4E_PR_DEBUG("released request stats");
}

struct LZ4E_stats *LZ4E_stats_alloc(void)
{
	struct LZ4E_stats *bcstats;

	bcstats = kzalloc(sizeof(*bcstats), GFP_KERNEL);
	if (!bcstats) {
		LZ4E_PR_ERR("failed to allocate request stats");
		return NULL;
	}

	LZ4E_PR_DEBUG("allocated request stats");
	return bcstats;
}

void LZ4E_stats_update(struct LZ4E_stats *bcstats, struct bio *bio)
{
	atomic64_inc(&bcstats->reqs_total);

	if (bio->bi_status != BLK_STS_OK) {
		atomic64_inc(&bcstats->reqs_failed);
		return;
	}

	atomic64_add((s64)bio->bi_vcnt, &bcstats->vec_count);
	atomic64_add((s64)bio->bi_iter.bi_size, &bcstats->data_in_bytes);

	LZ4E_PR_DEBUG("updated request stats");
}

void LZ4E_stats_reset(struct LZ4E_stats *bcstats)
{
	memset(bcstats, 0, sizeof(*bcstats));

	LZ4E_PR_DEBUG("reset request stats");
}
