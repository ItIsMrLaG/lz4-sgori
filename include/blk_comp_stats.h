// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef BLK_COMP_STATISTICS
#define BLK_COMP_STATISTICS

#include <linux/blk_types.h>
#include <linux/types.h>

// Struct representing request statistics of a disk for one of the operations
struct blk_comp_stats {
	atomic64_t reqs_total;
	atomic64_t reqs_failed;
	atomic64_t vec_count;
	atomic64_t data_in_bytes;
};

// Allocate request statistics
struct blk_comp_stats *blk_comp_stats_alloc(void);

// Update statistics using given bio
void blk_comp_stats_update(struct blk_comp_stats *bcstats, struct bio *bio);

// Reset request statistics
void blk_comp_stats_reset(struct blk_comp_stats *bcstats);

// Free request statistics
void blk_comp_stats_free(struct blk_comp_stats *bcstats);

#endif
