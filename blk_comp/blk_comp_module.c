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
#include <linux/sysfs.h>

#include "include/blk_comp_module.h"

#include "include/blk_comp_dev.h"
#include "include/blk_comp_static.h"
#include "include/blk_comp_stats.h"

static struct blk_comp bcomp = {};

// Callbacks can have unused parameters
// NOLINTBEGIN(misc-unused-parameters)

// Create disk over device at specified path
static int blk_comp_create_disk(const char *arg,
				const struct kernel_param *kpar)
{
	int ret = 0;
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
static int blk_comp_delete_disk(const char *arg,
				const struct kernel_param *kpar)
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

// Get info about existing disk
static int blk_comp_get_disk_info(char *buf, const struct kernel_param *kpar)
{
	int ret = 0;
	struct blk_comp_dev *bcdev = bcomp.bcdev;

	if (bcdev == NULL) {
		BLK_COMP_PR_ERR("no device found");
		return -ENODEV;
	}

	char *disk_name = bcdev->disk->disk_name;
	char *under_disk_name = bcdev->under_dev->bdev->bd_disk->disk_name;

	ret = sysfs_emit(buf, "%s: proxy over %s\n", disk_name,
			 under_disk_name);
	if (ret < 0)
		BLK_COMP_PR_ERR("failed to write disk info");

	return ret;
}

// Reset request statistics of existing disk
static int blk_comp_reset_stats(const char *arg,
				const struct kernel_param *kpar)
{
	struct blk_comp_dev *bcdev = bcomp.bcdev;

	if (bcdev == NULL) {
		BLK_COMP_PR_ERR("no stats to reset");
		return -ENODEV;
	}

	struct blk_comp_stats *read_stats = bcdev->read_stats;
	struct blk_comp_stats *write_stats = bcdev->write_stats;

	blk_comp_stats_reset(read_stats);
	blk_comp_stats_reset(write_stats);

	BLK_COMP_PR_INFO("request stats reset");
	return 0;
}

// Get request statistics of existing disk
static int blk_comp_get_stats(char *buf, const struct kernel_param *kpar)
{
	int ret = 0;
	struct blk_comp_dev *bcdev = bcomp.bcdev;

	if (bcdev == NULL) {
		BLK_COMP_PR_ERR("no stats available");
		return -ENODEV;
	}

	struct blk_comp_stats *read_stats = bcdev->read_stats;
	struct blk_comp_stats *write_stats = bcdev->write_stats;

	long long r_reqs_total = atomic64_read(&read_stats->reqs_total);
	long long r_reqs_failed = atomic64_read(&read_stats->reqs_failed);
	long long r_vec_count = atomic64_read(&read_stats->vec_count);
	long long r_data_in_bytes = atomic64_read(&read_stats->data_in_bytes);

	long long w_reqs_total = atomic64_read(&write_stats->reqs_total);
	long long w_reqs_failed = atomic64_read(&write_stats->reqs_failed);
	long long w_vec_count = atomic64_read(&write_stats->vec_count);
	long long w_data_in_bytes = atomic64_read(&write_stats->data_in_bytes);

	ret = sysfs_emit(buf, BLK_COMP_STATS_FORMAT, r_reqs_total,
			 r_reqs_failed, r_vec_count, r_data_in_bytes,
			 w_reqs_total, w_reqs_failed, w_vec_count,
			 w_data_in_bytes, r_reqs_total + w_reqs_total,
			 r_reqs_failed + w_reqs_failed,
			 r_vec_count + w_vec_count,
			 r_data_in_bytes + w_data_in_bytes);
	if (ret < 0)
		BLK_COMP_PR_ERR("failed to write request stats");

	return ret;
}

// Callbacks can have unused parameters
// NOLINTEND(misc-unused-parameters)

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
	int major = bcomp.major;
	struct blk_comp_dev *bcdev = bcomp.bcdev;

	unregister_blkdev((unsigned int)major, BLK_COMP_MODULE_NAME);
	bcomp.major = 0;

	blk_comp_dev_free(bcdev);
	bcomp.bcdev = NULL;

	BLK_COMP_PR_INFO("module unloaded successfully");
}

// Operations for creating disks
static const struct kernel_param_ops blk_comp_map_ops = {
	.set = blk_comp_create_disk,
	.get = blk_comp_get_disk_info,
};

// Operations for deleting disks
static const struct kernel_param_ops blk_comp_unmap_ops = {
	.set = blk_comp_delete_disk,
	.get = blk_comp_get_disk_info,
};

// Operations for managing request statistics
static const struct kernel_param_ops blk_comp_stats_ops = {
	.set = blk_comp_reset_stats,
	.get = blk_comp_get_stats,
};

module_param_cb(mapper, &blk_comp_map_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mapper, "Map to existing block device");

module_param_cb(unmapper, &blk_comp_unmap_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(unmapper, "Unmap from existing block device");

module_param_cb(stats, &blk_comp_stats_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(stats, "Block device request statistics");

module_init(blk_comp_init);
module_exit(blk_comp_exit);

MODULE_AUTHOR("Alexander Bugaev");
MODULE_DESCRIPTION("Proxy");
MODULE_LICENSE("GPL");
