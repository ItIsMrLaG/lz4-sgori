// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/types.h>

#include "include/lz4e_under_dev.h"

#include "include/lz4e_static.h"

void lz4e_under_dev_free(struct lz4e_under_dev *under_dev)
{
	if (!under_dev)
		return;

	if (under_dev->fbdev)
		bdev_fput(under_dev->fbdev);

	bioset_exit(under_dev->bset);
	kfree(under_dev->bset);

	kfree(under_dev);

	LZ4E_PR_DEBUG("released underlying device");
}

struct lz4e_under_dev *lz4e_under_dev_alloc(gfp_t gfp_mask)
{
	struct lz4e_under_dev *under_dev;
	struct bio_set *bset;

	under_dev = kzalloc(sizeof(*under_dev), gfp_mask);
	if (!under_dev) {
		LZ4E_PR_ERR("failed to allocate underlying device: %zu bytes",
			    sizeof(*under_dev));
		goto error;
	}

	bset = kzalloc(sizeof(*bset), gfp_mask);
	if (!bset) {
		LZ4E_PR_ERR("failed to allocate bio set: %zu bytes",
			    sizeof(*bset));
		goto free_under_dev;
	}

	under_dev->bset = bset;

	LZ4E_PR_DEBUG("allocated underlying device");
	return under_dev;

free_under_dev:
	kfree(under_dev);
error:
	return NULL;
}

int lz4e_under_dev_init(struct lz4e_under_dev *under_dev, const char *dev_path)
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
