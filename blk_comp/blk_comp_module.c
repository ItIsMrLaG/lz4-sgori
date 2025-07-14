// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <asm-generic/errno-base.h>
#include <asm-generic/int-ll64.h>
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

static struct LZ4E_module bcomp = {};

// Callbacks can have unused parameters
// NOLINTBEGIN(misc-unused-parameters)

static int LZ4E_create_disk(const char *arg, const struct kernel_param *kpar)
{
	struct LZ4E_dev *bcdev = bcomp.bcdev;
	int ret;

	if (bcdev) {
		LZ4E_PR_ERR("device already exists");
		return -EBUSY;
	}

	bcdev = LZ4E_dev_alloc();
	if (!bcdev) {
		LZ4E_PR_ERR("failed to allocate block device context");
		return -ENOMEM;
	}

	ret = LZ4E_dev_init(bcdev, arg, bcomp.major, LZ4E_FIRST_MINOR);
	if (ret) {
		LZ4E_PR_ERR("failed to initialize block device");
		goto free_device;
	}

	bcomp.bcdev = bcdev;

	LZ4E_PR_INFO("device mapped successfully");
	return 0;

free_device:
	LZ4E_dev_free(bcdev);
	return ret;
}

static int LZ4E_delete_disk(const char *arg, const struct kernel_param *kpar)
{
	struct LZ4E_dev *bcdev = bcomp.bcdev;

	if (!bcdev) {
		LZ4E_PR_ERR("no device for unmapping");
		return -ENODEV;
	}

	LZ4E_dev_free(bcdev);
	bcomp.bcdev = NULL;

	LZ4E_PR_INFO("device unmapped successfully");
	return 0;
}

static int LZ4E_get_disk_info(char *buf, const struct kernel_param *kpar)
{
	struct LZ4E_dev *bcdev = bcomp.bcdev;
	int ret;

	if (!bcdev) {
		LZ4E_PR_ERR("no device found");
		return -ENODEV;
	}

	char *disk_name = bcdev->disk->disk_name;
	char *under_disk_name = bcdev->under_dev->bdev->bd_disk->disk_name;

	ret = sysfs_emit(buf, "%s: proxy over %s\n", disk_name,
			 under_disk_name);
	if (ret < 0)
		LZ4E_PR_ERR("failed to write disk info");

	return ret;
}

static int LZ4E_reset_stats(const char *arg, const struct kernel_param *kpar)
{
	struct LZ4E_dev *bcdev = bcomp.bcdev;

	if (!bcdev) {
		LZ4E_PR_ERR("no stats to reset");
		return -ENODEV;
	}

	LZ4E_stats_reset(bcdev->read_stats);
	LZ4E_stats_reset(bcdev->write_stats);

	LZ4E_PR_INFO("request stats reset");
	return 0;
}

static int LZ4E_get_stats(char *buf, const struct kernel_param *kpar)
{
	struct LZ4E_dev *bcdev = bcomp.bcdev;
	int ret;

	if (!bcdev) {
		LZ4E_PR_ERR("no stats available");
		return -ENODEV;
	}

	struct LZ4E_stats *read_stats = bcdev->read_stats;
	struct LZ4E_stats *write_stats = bcdev->write_stats;

	u64 r_reqs_total = (u64)atomic64_read(&read_stats->reqs_total);
	u64 r_reqs_failed = (u64)atomic64_read(&read_stats->reqs_failed);
	u64 r_vec_count = (u64)atomic64_read(&read_stats->vec_count);
	u64 r_data_in_bytes = (u64)atomic64_read(&read_stats->data_in_bytes);

	u64 w_reqs_total = (u64)atomic64_read(&write_stats->reqs_total);
	u64 w_reqs_failed = (u64)atomic64_read(&write_stats->reqs_failed);
	u64 w_vec_count = (u64)atomic64_read(&write_stats->vec_count);
	u64 w_data_in_bytes = (u64)atomic64_read(&write_stats->data_in_bytes);

	ret = sysfs_emit(buf, LZ4E_STATS_FORMAT, r_reqs_total, r_reqs_failed,
			 r_vec_count, r_data_in_bytes, w_reqs_total,
			 w_reqs_failed, w_vec_count, w_data_in_bytes,
			 r_reqs_total + w_reqs_total,
			 r_reqs_failed + w_reqs_failed,
			 r_vec_count + w_vec_count,
			 r_data_in_bytes + w_data_in_bytes);
	if (ret < 0)
		LZ4E_PR_ERR("failed to write request stats");

	return ret;
}

// Callbacks can have unused parameters
// NOLINTEND(misc-unused-parameters)

static int __init LZ4E_module_init(void)
{
	int major = register_blkdev(LZ4E_MAJOR, LZ4E_MODULE_NAME);

	if (major < 0) {
		LZ4E_PR_ERR("failed to load module");
		return -EIO;
	}

	bcomp.major = major;

	LZ4E_PR_INFO("module loaded successfully");
	return 0;
}

static void __exit LZ4E_module_exit(void)
{
	int major = bcomp.major;
	struct LZ4E_dev *bcdev = bcomp.bcdev;

	unregister_blkdev((unsigned int)major, LZ4E_MODULE_NAME);
	bcomp.major = 0;

	LZ4E_dev_free(bcdev);
	bcomp.bcdev = NULL;

	LZ4E_PR_INFO("module unloaded successfully");
}

static const struct kernel_param_ops blk_comp_map_ops = {
	.set = LZ4E_create_disk,
	.get = LZ4E_get_disk_info,
};

static const struct kernel_param_ops blk_comp_unmap_ops = {
	.set = LZ4E_delete_disk,
	.get = LZ4E_get_disk_info,
};

static const struct kernel_param_ops blk_comp_stats_ops = {
	.set = LZ4E_reset_stats,
	.get = LZ4E_get_stats,
};

module_param_cb(mapper, &blk_comp_map_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mapper, "Map to existing block device");

module_param_cb(unmapper, &blk_comp_unmap_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(unmapper, "Unmap from existing block device");

module_param_cb(stats, &blk_comp_stats_ops, NULL, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(stats, "Block device request statistics");

module_init(LZ4E_module_init);
module_exit(LZ4E_module_exit);

MODULE_AUTHOR("Alexander Bugaev");
MODULE_DESCRIPTION("Proxy");
MODULE_LICENSE("GPL");
