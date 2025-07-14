// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef BLK_COMP_REQ_H
#define BLK_COMP_REQ_H

#include <linux/blk_types.h>

#include "blk_comp_dev.h"
#include "blk_comp_static.h"
#include "blk_comp_stats.h"
#include "blk_comp_under_dev.h"

// Struct representing request to the underlying device
struct LZ4E_req {
	struct bio *original_bio;
	struct LZ4E_under_dev *under_dev;
	struct LZ4E_stats *stats_to_update;
} LZ4E_ALIGN_32;

// Allocate request context
struct LZ4E_req *LZ4E_req_alloc(void);

// Initialize request to device with given bio
blk_status_t LZ4E_req_init(struct LZ4E_req *bcreq, struct bio *original_bio,
			   struct LZ4E_dev *bcdev);

// Submit request to underlying device
blk_status_t LZ4E_req_submit(struct LZ4E_req *bcreq);

// Free request context
void LZ4E_req_free(struct LZ4E_req *bcreq);

#endif
