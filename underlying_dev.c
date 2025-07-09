// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include "include/underlying_dev.h"

#define POOL_SIZE 1024

// Free the underlying device context
void underlying_dev_free(struct underlying_dev **dev_ptr) {
	struct underlying_dev *under_dev = *dev_ptr;

	if (under_dev == NULL) return;

	struct file *fbdev = under_dev->fbdev;
	struct bio_set *bset = under_dev->bset;

	if (fbdev != NULL)
		bdev_fput(fbdev);

	if (bset != NULL)
		kfree(bset);

	kfree(under_dev);
	*dev_ptr = NULL;

	pr_info("Released underlying device context");
}

// Allocate underlying device context
int underlying_dev_alloc(struct underlying_dev **dev_ptr) {
	struct underlying_dev *under_dev = NULL;
	struct bio_set *bset = NULL;

	under_dev = kzalloc(sizeof(*under_dev), GFP_KERNEL);
	if (under_dev == NULL) {
		pr_err("Failed to allocate underlying device context");
		goto alloc_err;
	}

	bset = kzalloc(sizeof(*bset), GFP_KERNEL);
	under_dev->bset = bset;
	if (bset == NULL) {
		pr_err("Failed to allocate bio set");
		goto alloc_err;
	}

	*dev_ptr = under_dev;

	pr_info("Allocated underlying device context");
	return 0;

alloc_err:
	underlying_dev_free(&under_dev);
	return -ENOMEM;
}

// Initialize underlying device
int underlying_dev_init(struct underlying_dev *under_dev, const char *dev_path) {
	int ret = 0;
	struct block_device *bdev = NULL;
	struct file *fbdev = NULL;
	struct bio_set *bset = under_dev->bset;

	fbdev = bdev_file_open_by_path(dev_path, BLK_OPEN_READ | BLK_OPEN_WRITE, under_dev, NULL);
	if (IS_ERR_OR_NULL(fbdev)) {
		pr_err("Failed to open file associated with device: %s", dev_path);
		return PTR_ERR(fbdev);
	}

	bdev = file_bdev(fbdev);

	ret = bioset_init(bset, POOL_SIZE, 0, BIOSET_NEED_BVECS);
	if (ret) {
		pr_err("Failed to initialize bio set");
		return ret;
	}

	under_dev->bdev = bdev;
	under_dev->fbdev = fbdev;

	pr_info("Initialized underlying device: %s", dev_path);
	return 0;
}
