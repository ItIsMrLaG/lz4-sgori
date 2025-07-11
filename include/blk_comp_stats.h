// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef BLK_COMP_STATISTICS
#define BLK_COMP_STATISTICS

#include <linux/atomic.h>
#include <linux/blk_types.h>

// Struct representing request statistics of a disk for one of the operations
struct blk_comp_stats {
	atomic64_t req_count;
	atomic64_t vec_count;

	atomic64_t data_in_bytes;
};

// Allocate request statistics
struct blk_comp_stats *blk_comp_stats_alloc(void);

// Update statistics using given bio
void blk_comp_stats_update(struct blk_comp_stats *bcstats, struct bio *bio);

// Free request statistics
void blk_comp_stats_free(struct blk_comp_stats *stats);

#endif
