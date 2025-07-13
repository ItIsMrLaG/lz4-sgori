// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/bvec.h>
#include <linux/fortify-string.h>
#include <linux/gfp_types.h>
#include <linux/slab.h>
#include <linux/stddef.h>

#include "include/blk_comp_stats.h"

#include "include/blk_comp_static.h"

// Free request statistics
void blk_comp_stats_free(struct blk_comp_stats *bcstats)
{
	kfree(bcstats);

	BLK_COMP_PR_DEBUG("released request stats");
}

// Allocate request statistics
struct blk_comp_stats *blk_comp_stats_alloc(void)
{
	struct blk_comp_stats *bcstats = NULL;

	bcstats = kzalloc(sizeof(*bcstats), GFP_KERNEL);
	if (bcstats == NULL) {
		BLK_COMP_PR_ERR("failed to allocate request stats");
		return NULL;
	}

	BLK_COMP_PR_DEBUG("allocated request stats");
	return bcstats;
}

// Update statistics using given bio
void blk_comp_stats_update(struct blk_comp_stats *bcstats, struct bio *bio)
{
	atomic64_inc(&bcstats->reqs_total);

	if (bio->bi_status != BLK_STS_OK) {
		atomic64_inc(&bcstats->reqs_failed);
		return;
	}

	struct bio_vec	 bvec;
	struct bvec_iter iter;
	bio_for_each_segment (bvec, bio, iter) {
		atomic64_inc(&bcstats->vec_count);
		atomic64_add((long long)bvec.bv_len, &bcstats->data_in_bytes);
	}

	BLK_COMP_PR_DEBUG("updated request stats");
}

// Reset request statistics
void blk_comp_stats_reset(struct blk_comp_stats *bcstats)
{
	memset(bcstats, 0, sizeof(*bcstats));

	BLK_COMP_PR_DEBUG("reset request stats");
}
