// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include "include/blk_comp_dev.h"
#include "include/underlying_dev.h"
#include "include/gendisk_utils.h"

void blk_comp_dev_free(struct blk_comp_dev **dev_ptr) {
	struct blk_comp_dev *bcdev = *dev_ptr;

	if (bcdev == NULL) return;

	struct underlying_dev *under_dev = bcdev->under_dev;
	struct gendisk *disk = bcdev->disk;

	if (under_dev != NULL) {
		underlying_dev_free(&under_dev);
	}

	if (disk != NULL) {
		gendisk_free(&disk);
	}

	kfree(bcdev);
	*dev_ptr = NULL;
}

// Allocate block device context
int blk_comp_dev_alloc(struct blk_comp_dev **dev_ptr) {
	int ret;
	struct blk_comp_dev *bcdev = *dev_ptr;
	struct underlying_dev *under_dev = NULL;
	struct gendisk *disk = NULL;

	bcdev = kzalloc(sizeof(*bcdev), GFP_KERNEL);
	if (bcdev == NULL) {
		pr_err("Failed to allocate block device context");
		goto alloc_err;
	}

	ret = underlying_dev_alloc(&under_dev);
	bcdev->under_dev = under_dev;
	if (ret) {
		pr_err("Failed to allocate underlying device context");
		goto alloc_err;
	}

	ret = gendisk_alloc(&disk);
	bcdev->disk = disk;
	if (ret) {
		pr_err("Failed to allocate generic disk context");
		goto alloc_err;
	}

	return 0;

alloc_err:
	blk_comp_dev_free(&bcdev);
	return -ENOMEM;
}

// Initialize the device to be managed by the driver
int blk_comp_dev_init(struct blk_comp_dev *bcdev, const char *dev_path, int major, int first_minor) {
	int ret = 0;
	struct underlying_dev *under_dev = bcdev->under_dev;
	struct gendisk *disk = bcdev->disk;

	ret = underlying_dev_init(under_dev, dev_path);
	if (ret) {
		pr_err("Failed to initialize underlying device");
		return ret;
	}

	ret = gendisk_init(disk, bcdev, major, first_minor);
	if (ret < 0) {
		pr_err("Failed to initialize generic disk");
		return ret;
	}

	return 0;
}

// Submit bio request to the device
void blk_comp_dev_submit_bio(struct bio *bio) {

}
