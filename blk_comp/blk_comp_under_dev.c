// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <asm-generic/errno-base.h>
#include <linux/bio.h>
#include <linux/blkdev.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gfp_types.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/stddef.h>

#include "include/blk_comp_under_dev.h"

#define BIO_SET_POOL_SIZE 1024

// Free underlying device context
void blk_comp_under_dev_free(struct blk_comp_under_dev *under_dev)
{
	if (under_dev == NULL)
		return;

	struct file    *fbdev = under_dev->fbdev;
	struct bio_set *bset  = under_dev->bset;

	if (fbdev != NULL)
		bdev_fput(fbdev);

	if (bset != NULL) {
		bioset_exit(bset);
		kfree(bset);
	}

	kfree(under_dev);

	pr_info("Released underlying device context");
}

// Allocate underlying device context
struct blk_comp_under_dev *blk_comp_under_dev_alloc(void)
{
	struct blk_comp_under_dev *under_dev = NULL;
	struct bio_set		  *bset	     = NULL;

	under_dev = kzalloc(sizeof(*under_dev), GFP_KERNEL);
	if (under_dev == NULL) {
		pr_err("Failed to allocate underlying device context");
		return NULL;
	}

	bset		= kzalloc(sizeof(*bset), GFP_KERNEL);
	under_dev->bset = bset;
	if (bset == NULL) {
		pr_err("Failed to allocate bio set");
		goto free_device;
	}

	pr_info("Allocated underlying device context");
	return under_dev;

free_device:
	blk_comp_under_dev_free(under_dev);
	return NULL;
}

// Open underlying device
int blk_comp_under_dev_open(struct blk_comp_under_dev *under_dev,
			    const char		      *dev_path)
{
	int		     ret   = 0;
	struct block_device *bdev  = NULL;
	struct file	    *fbdev = NULL;
	struct bio_set	    *bset  = under_dev->bset;

	fbdev = bdev_file_open_by_path(dev_path, BLK_OPEN_READ | BLK_OPEN_WRITE,
				       under_dev, NULL);
	if (IS_ERR_OR_NULL(fbdev)) {
		pr_err("Failed to open file associated with device: %s",
		       dev_path);
		return (int)PTR_ERR(fbdev);
	}

	bdev		 = file_bdev(fbdev);
	under_dev->bdev	 = bdev;
	under_dev->fbdev = fbdev;

	ret = bioset_init(bset, BIO_SET_POOL_SIZE, 0, BIOSET_NEED_BVECS);
	if (ret) {
		pr_err("Failed to initialize bio set");
		return ret;
	}

	pr_info("Opened underlying device: %s", dev_path);
	return 0;
}
