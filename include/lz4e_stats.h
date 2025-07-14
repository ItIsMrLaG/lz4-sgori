// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_STATS_H
#define LZ4E_STATS_H

#include <linux/blk_types.h>
#include <linux/types.h>

#include "lz4e_static.h"

// Struct representing request statistics of a disk for one of the operations
struct LZ4E_stats {
	atomic64_t reqs_total;
	atomic64_t reqs_failed;
	atomic64_t vec_count;
	atomic64_t data_in_bytes;
} LZ4E_ALIGN_32;

// Allocate request statistics
struct LZ4E_stats *LZ4E_stats_alloc(void);

// Update statistics using given bio
void LZ4E_stats_update(struct LZ4E_stats *lzstats, struct bio *bio);

// Reset request statistics
void LZ4E_stats_reset(struct LZ4E_stats *lzstats);

// Free request statistics
void LZ4E_stats_free(struct LZ4E_stats *lzstats);

#endif
