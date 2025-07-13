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
struct blk_comp_req {
	struct bio		  *original_bio;
	struct blk_comp_under_dev *under_dev;
	struct blk_comp_stats	  *stats_to_update;
} BLK_COMP_ALIGN_32;

// Allocate request context
struct blk_comp_req *blk_comp_req_alloc(void);

// Initialize request to device with given bio
blk_status_t blk_comp_req_init(struct blk_comp_req *bcreq,
			       struct bio	   *original_bio,
			       struct blk_comp_dev *bcdev);

// Submit request to underlying device
blk_status_t blk_comp_req_submit(struct blk_comp_req *bcreq);

// Free request context
void blk_comp_req_free(struct blk_comp_req *bcreq);

#endif
