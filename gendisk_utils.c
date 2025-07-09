// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <linux/nodemask_types.h>

#include "include/gendisk_utils.h"

static const struct block_device_operations bcdev_ops = {
	.owner = THIS_MODULE,
	.submit_bio = blk_comp_dev_submit_bio,
};

// Free generic disk context
void gendisk_free(struct gendisk **disk_ptr) {
	struct gendisk *disk = *disk_ptr;

	if (disk == NULL) return;

	del_gendisk(disk);
	put_disk(disk);

	kfree(disk);
	*disk_ptr = NULL;

	pr_info("Released generic disk context");
}

// Allocate generic disk context
int gendisk_alloc(struct gendisk **disk_ptr) {
	struct gendisk *disk = NULL;

	disk = blk_alloc_disk(NULL, NUMA_NO_NODE);
	if (disk == NULL) {
		pr_err("Failed to allocate generic disk context");
		goto alloc_err;
	}

	*disk_ptr = disk;

	pr_info("Allocated generic disk context");
	return 0;

alloc_err:
	gendisk_free(&disk);
	return -ENOMEM;
}

// Initialize generic disk
int gendisk_init(struct gendisk *disk, struct blk_comp_dev *bcdev, int major, int first_minor) {
	int ret = 0;

	disk->major = major;
	disk->first_minor = first_minor;
	disk->minors = 1;
	disk->fops = &bcdev_ops;
	disk->private_data = bcdev;

	// Disable partition support
	disk->flags |= GENHD_FL_NO_PART;

	set_capacity(disk, get_capacity(bcdev->under_dev->bdev->bd_disk));

	ret = snprintf(disk->disk_name, DISK_NAME_LEN, "blk-comp-%d", disk->first_minor);
	if (ret < 0) {
		pr_err("Failed to write generic disk name");
		return ret;
	}

	ret = add_disk(disk);
	if (ret) {
		pr_err("Failed to add generic disk: %s", disk->disk_name);
		return ret;
	}

	pr_info("Initialized generic disk:, %s", disk->disk_name);
	return 0;
}
