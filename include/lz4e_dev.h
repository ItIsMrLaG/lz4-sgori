// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_DEV_H
#define LZ4E_DEV_H

#include <linux/blk_types.h>
#include <linux/blkdev.h>

#include "lz4e_static.h"
#include "lz4e_stats.h"
#include "lz4e_under_dev.h"

// Struct representing a device to be managed by the driver
struct LZ4E_dev {
	struct gendisk *disk;
	struct LZ4E_under_dev *under_dev;
	struct LZ4E_stats *read_stats;
	struct LZ4E_stats *write_stats;
} LZ4E_ALIGN_32;

// Allocate block device context
struct LZ4E_dev *LZ4E_dev_alloc(void);

// Initialize device to be managed by the driver
int LZ4E_dev_init(struct LZ4E_dev *lzdev, const char *dev_path, int major,
		  int first_minor);

// Free block device context
void LZ4E_dev_free(struct LZ4E_dev *lzdev);

// Submit bio request to device
void LZ4E_dev_submit_bio(struct bio *original_bio);

#endif
