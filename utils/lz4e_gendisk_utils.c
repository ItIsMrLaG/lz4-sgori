// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/nodemask_types.h>
#include <linux/sprintf.h>
#include <linux/stddef.h>

#include "include/lz4e_gendisk_utils.h"

#include "include/lz4e_dev.h"
#include "include/lz4e_static.h"

static const struct block_device_operations lz4e_disk_ops = {
	.owner = THIS_MODULE,
	.submit_bio = LZ4E_dev_submit_bio,
};

void LZ4E_gendisk_free(struct gendisk *disk)
{
	if (!disk)
		return;

	del_gendisk(disk);
	put_disk(disk);

	LZ4E_PR_DEBUG("released generic disk context");
}

struct gendisk *LZ4E_gendisk_alloc(void)
{
	struct gendisk *disk;

	disk = blk_alloc_disk(NULL, NUMA_NO_NODE);
	if (!disk) {
		LZ4E_PR_ERR("failed to allocate generic disk context");
		return NULL;
	}

	LZ4E_PR_DEBUG("allocated generic disk context");
	return disk;
}

int LZ4E_gendisk_add(struct gendisk *disk, struct LZ4E_dev *lzdev, int major,
		     int first_minor)
{
	int ret;

	disk->major = major;
	disk->first_minor = first_minor;
	disk->minors = 1;
	disk->fops = &lz4e_disk_ops;
	disk->private_data = lzdev;

	// Do not support multiple minors, disable partition support
	disk->flags |= GENHD_FL_NO_PART;

	set_capacity(disk, get_capacity(lzdev->under_dev->bdev->bd_disk));

	ret = snprintf(disk->disk_name, DISK_NAME_LEN, LZ4E_MODULE_NAME "%d",
		       disk->first_minor);
	if (ret < 0) {
		LZ4E_PR_ERR("failed to write generic disk name");
		return ret;
	}

	ret = add_disk(disk);
	if (ret) {
		LZ4E_PR_ERR("failed to add generic disk: %s", disk->disk_name);
		return ret;
	}

	LZ4E_PR_INFO("initialized generic disk: %s", disk->disk_name);
	return 0;
}
