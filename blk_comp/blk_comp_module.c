// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <asm-generic/errno-base.h>
#include <linux/blkdev.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/printk.h>
#include <linux/stat.h>
#include <linux/stddef.h>

#include "include/blk_comp_module.h"

#include "include/blk_comp_dev.h"

#define BLK_COMP_MAJOR	     0
#define BLK_COMP_FIRST_MINOR 0
#define BLK_COMP_NAME	     "blk-comp-dev"

static struct blk_comp bcomp = {};

// Create disk over device at specified path
static int blk_comp_disk_create(const char *arg, const struct kernel_param *kp)
{
	int		     ret   = 0;
	struct blk_comp_dev *bcdev = bcomp.bcdev;

	if (bcdev != NULL) {
		pr_err("Device already exists");
		return -EBUSY;
	}

	ret = blk_comp_dev_alloc(&bcdev);
	if (ret) {
		pr_err("Failed to allocate block device context");
		goto alloc_err;
	}

	ret = blk_comp_dev_init(bcdev, arg, bcomp.major, BLK_COMP_FIRST_MINOR);
	if (ret) {
		pr_err("Failed to initialize block device");
		goto init_err;
	}

	bcomp.bcdev = bcdev;

	pr_info("Device mapped successfully");
	return 0;

init_err:
	blk_comp_dev_free(&bcdev);
alloc_err:
	return ret;
}

// Remove existing disk
static int blk_comp_disk_delete(const char *arg, const struct kernel_param *kp)
{
	struct blk_comp_dev *bcdev = bcomp.bcdev;

	if (bcdev == NULL) {
		pr_err("No device for unmapping");
		return -ENODEV;
	}

	blk_comp_dev_free(&bcdev);
	bcomp.bcdev = bcdev;

	pr_info("Device unmapped successfully");
	return 0;
}

// Operations for creating disks
static const struct kernel_param_ops blk_comp_map_ops = {
	.set = blk_comp_disk_create,
	.get = NULL,
};

// Operations for deleting disks
static const struct kernel_param_ops blk_comp_unmap_ops = {
	.set = blk_comp_disk_delete,
	.get = NULL,
};

// Initialize module
static int __init blk_comp_init(void)
{
	bcomp.major = register_blkdev(BLK_COMP_MAJOR, BLK_COMP_NAME);

	if (bcomp.major < 0) {
		pr_err("Failed to load module");
		return -EIO;
	}

	pr_info("Module loaded successfully");
	return 0;
}

// Remove module
static void __exit blk_comp_exit(void)
{
	struct blk_comp_dev *bcdev = bcomp.bcdev;

	blk_comp_dev_free(&bcdev);

	unregister_blkdev((unsigned int)bcomp.major, BLK_COMP_NAME);

	pr_info("Module unloaded successfully");
}

module_param_cb(mapper, &blk_comp_map_ops, NULL, S_IWUSR);
MODULE_PARM_DESC(mapper, "Map to existing block device");

module_param_cb(unmapper, &blk_comp_unmap_ops, NULL, S_IWUSR);
MODULE_PARM_DESC(unmapper, "Unmap from existing block device");

module_init(blk_comp_init);
module_exit(blk_comp_exit);

MODULE_AUTHOR("Alexander Bugaev");
MODULE_DESCRIPTION("Test");
MODULE_LICENSE("GPL");
