// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <linux/blk_types.h>
#include <linux/gfp_types.h>
#include <linux/slab.h>
#include <linux/stddef.h>

#include "include/blk_comp_stats.h"

#include "include/blk_comp_module.h"

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
}
