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
#include <linux/stat.h>
#include <linux/stddef.h>

#include "include/blk_comp_module.h"

#include "include/blk_comp_dev.h"

static struct blk_comp bcomp = {};

// Create disk over device at specified path
static int blk_comp_disk_create(const char *arg, const struct kernel_param *kp)
{
	int		     ret   = 0;
	struct blk_comp_dev *bcdev = bcomp.bcdev;

	if (bcdev != NULL) {
		BLK_COMP_PR_ERR("device already exists");
		return -EBUSY;
	}

	bcdev = blk_comp_dev_alloc();
	if (bcdev == NULL) {
		BLK_COMP_PR_ERR("failed to allocate block device context");
		return -ENOMEM;
	}

	ret = blk_comp_dev_init(bcdev, arg, bcomp.major, BLK_COMP_FIRST_MINOR);
	if (ret) {
		BLK_COMP_PR_ERR("failed to initialize block device");
		goto free_device;
	}

	bcomp.bcdev = bcdev;

	BLK_COMP_PR_INFO("device mapped successfully");
	return 0;

free_device:
	blk_comp_dev_free(bcdev);
	return ret;
}

// Remove existing disk
static int blk_comp_disk_delete(const char *arg, const struct kernel_param *kp)
{
	struct blk_comp_dev *bcdev = bcomp.bcdev;

	if (bcdev == NULL) {
		BLK_COMP_PR_ERR("no device for unmapping");
		return -ENODEV;
	}

	blk_comp_dev_free(bcdev);
	bcomp.bcdev = NULL;

	BLK_COMP_PR_INFO("device unmapped successfully");
	return 0;
}

// Module init callback
static int __init blk_comp_init(void)
{
	int major = register_blkdev(BLK_COMP_MAJOR, BLK_COMP_MODULE_NAME);

	if (major < 0) {
		BLK_COMP_PR_ERR("failed to load module");
		return -EIO;
	}

	bcomp.major = major;

	BLK_COMP_PR_INFO("module loaded successfully");
	return 0;
}

// Module exit callback
static void __exit blk_comp_exit(void)
{
	int		     major = bcomp.major;
	struct blk_comp_dev *bcdev = bcomp.bcdev;

	unregister_blkdev((unsigned int)major, BLK_COMP_MODULE_NAME);
	bcomp.major = 0;

	blk_comp_dev_free(bcdev);
	bcomp.bcdev = NULL;

	BLK_COMP_PR_INFO("module unloaded successfully");
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

module_param_cb(mapper, &blk_comp_map_ops, NULL, S_IWUSR);
MODULE_PARM_DESC(mapper, "Map to existing block device");

module_param_cb(unmapper, &blk_comp_unmap_ops, NULL, S_IWUSR);
MODULE_PARM_DESC(unmapper, "Unmap from existing block device");

module_init(blk_comp_init);
module_exit(blk_comp_exit);

MODULE_AUTHOR("Alexander Bugaev");
MODULE_DESCRIPTION("Test");
MODULE_LICENSE("GPL");
