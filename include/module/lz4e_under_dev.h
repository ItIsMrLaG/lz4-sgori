// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_UNDER_DEV_H
#define LZ4E_UNDER_DEV_H

#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/fs.h>

#include "lz4e_static.h"

// Struct representing a physical block device
struct lz4e_under_dev {
	struct block_device *bdev;
	struct file *fbdev;
	struct bio_set *bset;
} LZ4E_ALIGN_32;

// Allocate underlying device context
struct lz4e_under_dev *lz4e_under_dev_alloc(void);

// Open underlying device
int lz4e_under_dev_open(struct lz4e_under_dev *under_dev, const char *dev_path);

// Free underlying device context
void lz4e_under_dev_free(struct lz4e_under_dev *under_dev);

#endif
