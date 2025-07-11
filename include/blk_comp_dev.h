// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef BLK_COMP_DEVICE
#define BLK_COMP_DEVICE

#include <linux/blk_types.h>
#include <linux/blkdev.h>

#include "blk_comp_stats.h"
#include "blk_comp_under_dev.h"

// Struct representing a device to be managed by the driver
struct blk_comp_dev {
	struct gendisk		  *disk;
	struct blk_comp_under_dev *under_dev;
	struct blk_comp_stats	  *read_stats;
	struct blk_comp_stats	  *write_stats;
};

// Allocate block device context
struct blk_comp_dev *blk_comp_dev_alloc(void);

// Initialize device to be managed by the driver
int blk_comp_dev_init(struct blk_comp_dev *bcdev, const char *arg, int major,
		      int first_minor);

// Free block device context
void blk_comp_dev_free(struct blk_comp_dev *bcdev);

// Submit bio request to device
void blk_comp_dev_submit_bio(struct bio *bio);

#endif
