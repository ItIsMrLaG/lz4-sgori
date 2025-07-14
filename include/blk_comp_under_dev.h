// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef BLK_COMP_UNDER_DEV_H
#define BLK_COMP_UNDER_DEV_H

#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/fs.h>

#include "blk_comp_static.h"

// Struct representing a physical block device
struct blk_comp_under_dev {
	struct block_device *bdev;
	struct file *fbdev;
	struct bio_set *bset;
} BLK_COMP_ALIGN_32;

// Allocate underlying device context
struct blk_comp_under_dev *blk_comp_under_dev_alloc(void);

// Open underlying device
int blk_comp_under_dev_open(struct blk_comp_under_dev *under_dev,
			    const char *dev_path);

// Free underlying device context
void blk_comp_under_dev_free(struct blk_comp_under_dev *under_dev);

#endif
