// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef UNDERLYING_DEVICE
#define UNDERLYING_DEVICE

#include <linux/blkdev.h>
#include <linux/fs.h>
#include <linux/bio.h>

// Struct representing a physical block device
struct underlying_dev {
	struct block_device *bdev;
	struct file *fbdev;
	struct bio_set *bset;
};

// Allocate underlying device context
int underlying_dev_alloc(struct underlying_dev **dev_ptr);

// Initialize underlying device
int underlying_dev_init(struct underlying_dev *under_dev, const char *dev_path);

// Free underlying device context
void underlying_dev_free(struct underlying_dev **dev_ptr);

#endif
