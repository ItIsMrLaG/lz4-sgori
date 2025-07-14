// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/gfp_types.h>
#include <linux/slab.h>
#include <linux/stddef.h>

#include "include/lz4e_under_dev.h"

#include "include/lz4e_static.h"

void LZ4E_under_dev_free(struct LZ4E_under_dev *under_dev)
{
	if (!under_dev)
		return;

	struct file *fbdev = under_dev->fbdev;
	struct bio_set *bset = under_dev->bset;

	if (fbdev)
		bdev_fput(fbdev);

	if (bset) {
		bioset_exit(bset);
		kfree(bset);
	}

	kfree(under_dev);

	LZ4E_PR_DEBUG("released underlying device context");
}

struct LZ4E_under_dev *LZ4E_under_dev_alloc(void)
{
	struct LZ4E_under_dev *under_dev;
	struct bio_set *bset;

	under_dev = kzalloc(sizeof(*under_dev), GFP_KERNEL);
	if (!under_dev) {
		LZ4E_PR_ERR("failed to allocate underlying device context");
		return NULL;
	}

	bset = kzalloc(sizeof(*bset), GFP_KERNEL);
	under_dev->bset = bset;
	if (!bset) {
		LZ4E_PR_ERR("failed to allocate bio set");
		goto free_device;
	}

	LZ4E_PR_DEBUG("allocated underlying device context");
	return under_dev;

free_device:
	LZ4E_under_dev_free(under_dev);
	return NULL;
}

int LZ4E_under_dev_open(struct LZ4E_under_dev *under_dev, const char *dev_path)
{
	struct bio_set *bset = under_dev->bset;
	struct block_device *bdev;
	struct file *fbdev;
	int ret;

	fbdev = bdev_file_open_by_path(dev_path, BLK_OPEN_READ | BLK_OPEN_WRITE,
				       under_dev, NULL);
	if (IS_ERR_OR_NULL(fbdev)) {
		LZ4E_PR_ERR("failed to open device: %s", dev_path);
		return (int)PTR_ERR(fbdev);
	}

	bdev = file_bdev(fbdev);
	under_dev->bdev = bdev;
	under_dev->fbdev = fbdev;

	ret = bioset_init(bset, LZ4E_BIOSET_SIZE, 0, BIOSET_NEED_BVECS);
	if (ret) {
		LZ4E_PR_ERR("failed to initialize bio set");
		return ret;
	}

	LZ4E_PR_INFO("opened underlying device: %s", dev_path);
	return 0;
}
