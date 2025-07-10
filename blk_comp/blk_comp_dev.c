// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include "include/blk_comp_dev.h"

#include "include/gendisk_utils.h"

// Free block device context
void blk_comp_dev_free(struct blk_comp_dev **dev_ptr)
{
	struct blk_comp_dev *bcdev = *dev_ptr;

	if (bcdev == NULL)
		return;

	struct underlying_dev *under_dev = bcdev->under_dev;
	struct gendisk	      *disk	 = bcdev->disk;

	blk_comp_gendisk_free(&disk);
	blk_comp_under_dev_free(&under_dev);

	kfree(bcdev);
	*dev_ptr = NULL;

	pr_info("Released block device context");
}

// Allocate block device context
int blk_comp_dev_alloc(struct blk_comp_dev **dev_ptr)
{
	int		       ret	 = 0;
	struct blk_comp_dev   *bcdev	 = NULL;
	struct underlying_dev *under_dev = NULL;
	struct gendisk	      *disk	 = NULL;

	bcdev = kzalloc(sizeof(*bcdev), GFP_KERNEL);
	if (bcdev == NULL) {
		pr_err("Failed to allocate block device context");
		return -ENOMEM;
	}

	ret		 = blk_comp_under_dev_alloc(&under_dev);
	bcdev->under_dev = under_dev;
	if (ret) {
		pr_err("Failed to allocate underlying device context");
		goto alloc_err;
	}

	ret	    = blk_comp_gendisk_alloc(&disk);
	bcdev->disk = disk;
	if (ret) {
		pr_err("Failed to allocate generic disk context");
		goto alloc_err;
	}

	*dev_ptr = bcdev;

	pr_info("Allocated block device context");
	return 0;

alloc_err:
	blk_comp_dev_free(&bcdev);
	return ret;
}

// Initialize the device to be managed by the driver
int blk_comp_dev_init(struct blk_comp_dev *bcdev, const char *dev_path,
		      int major, int first_minor)
{
	int		       ret	 = 0;
	struct underlying_dev *under_dev = bcdev->under_dev;
	struct gendisk	      *disk	 = bcdev->disk;

	ret = blk_comp_under_dev_open(under_dev, dev_path);
	if (ret) {
		pr_err("Failed to open underlying device");
		return ret;
	}

	ret = blk_comp_gendisk_add(disk, bcdev, major, first_minor);
	if (ret) {
		pr_err("Failed to add generic disk");
		return ret;
	}

	pr_info("Initialized block device");
	return 0;
}

// Submit bio request to the device
void blk_comp_dev_submit_bio(struct bio *bio)
{
}
