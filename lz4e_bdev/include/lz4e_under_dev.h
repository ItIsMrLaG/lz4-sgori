// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_UNDER_DEV_H
#define LZ4E_UNDER_DEV_H

#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/fs.h>
#include <linux/types.h>

#include "lz4e_static.h"

/* struct representing a physical block device */
struct lz4e_under_dev {
	struct block_device *bdev;
	struct file *fbdev;
	struct bio_set *bset;
} LZ4E_ALIGN_32;

/* allocate underlying device */
struct lz4e_under_dev *lz4e_under_dev_alloc(gfp_t gfp_mask);

/* open underlying device */
int lz4e_under_dev_init(struct lz4e_under_dev *under_dev, const char *dev_path);

/* free underlying device */
void lz4e_under_dev_free(struct lz4e_under_dev *under_dev);

#endif
