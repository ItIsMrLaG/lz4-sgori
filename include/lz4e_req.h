// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_REQ_H
#define LZ4E_REQ_H

#include <linux/blk_types.h>

#include "lz4e_dev.h"
#include "lz4e_static.h"
#include "lz4e_stats.h"
#include "lz4e_under_dev.h"

// Struct representing request to the underlying device
struct LZ4E_req {
	struct bio *original_bio;
	struct LZ4E_under_dev *under_dev;
	struct LZ4E_stats *stats_to_update;
} LZ4E_ALIGN_32;

// Allocate request context
struct LZ4E_req *LZ4E_req_alloc(void);

// Initialize request to device with given bio
blk_status_t LZ4E_req_init(struct LZ4E_req *lzreq, struct bio *original_bio,
			   struct LZ4E_dev *lzdev);

// Submit request to underlying device
blk_status_t LZ4E_req_submit(struct LZ4E_req *lzreq);

// Free request context
void LZ4E_req_free(struct LZ4E_req *lzreq);

#endif
