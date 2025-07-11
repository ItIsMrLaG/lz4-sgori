// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <asm-generic/errno-base.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/nodemask_types.h>
#include <linux/sprintf.h>
#include <linux/stddef.h>

#include "include/gendisk_utils.h"

#include "include/blk_comp_dev.h"
#include "include/blk_comp_module.h"

// Supported block device operations
static const struct block_device_operations blk_comp_disk_ops = {
	.owner	    = THIS_MODULE,
	.submit_bio = blk_comp_dev_submit_bio,
};

// Free generic disk context
void blk_comp_gendisk_free(struct gendisk *disk)
{
	if (disk == NULL)
		return;

	del_gendisk(disk);
	put_disk(disk);

	BLK_COMP_PR_DEBUG("released generic disk context");
}

// Allocate generic disk context
struct gendisk *blk_comp_gendisk_alloc(void)
{
	struct gendisk *disk = NULL;

	disk = blk_alloc_disk(NULL, NUMA_NO_NODE);
	if (disk == NULL) {
		BLK_COMP_PR_ERR("failed to allocate generic disk context");
		return NULL;
	}

	BLK_COMP_PR_DEBUG("allocated generic disk context");
	return disk;
}

// Add generic disk
int blk_comp_gendisk_add(struct gendisk *disk, struct blk_comp_dev *bcdev,
			 int major, int first_minor)
{
	int ret = 0;

	disk->major	   = major;
	disk->first_minor  = first_minor;
	disk->minors	   = 1;
	disk->fops	   = &blk_comp_disk_ops;
	disk->private_data = bcdev;

	// Do not support multiple minors, disable partition support
	disk->flags |= GENHD_FL_NO_PART;

	set_capacity(disk, get_capacity(bcdev->under_dev->bdev->bd_disk));

	ret = snprintf(disk->disk_name, DISK_NAME_LEN, "blk-comp-%d",
		       disk->first_minor);
	if (ret < 0) {
		BLK_COMP_PR_ERR("failed to write generic disk name");
		return ret;
	}

	ret = add_disk(disk);
	if (ret) {
		BLK_COMP_PR_ERR("failed to add generic disk: %s",
				disk->disk_name);
		return ret;
	}

	BLK_COMP_PR_INFO("initialized generic disk: %s", disk->disk_name);
	return 0;
}
