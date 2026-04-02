// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_DEV_H
#define LZ4E_DEV_H

#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/types.h>

#include "lz4e_chunk.h"
#include "lz4e_static.h"
#include "lz4e_stats.h"
#include "lz4e_under_dev.h"

/* device to be managed by the driver */
struct lz4e_dev {
	struct gendisk *disk;
	struct lz4e_under_dev *under_dev;

	struct lz4e_stats *read_stats;
	struct lz4e_stats *write_stats;

	lz4e_comp_t comp_type;
	int acceleration;
} LZ4E_ALIGN_64;

/* allocate block device */
struct lz4e_dev *lz4e_dev_alloc(gfp_t gfp_mask);

/* initialize device to be managed by the driver */
int lz4e_dev_init(struct lz4e_dev *lzdev, const char *dev_path, int major,
		  int first_minor, lz4e_comp_t comp_type, int acceleration);

/* free block device */
void lz4e_dev_free(struct lz4e_dev *lzdev);

/* submit bio request to device */
void lz4e_dev_submit_bio(struct bio *orig_bio);

#endif
