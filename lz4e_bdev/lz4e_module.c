// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <linux/blkdev.h>
#include <linux/gfp_types.h>
#include <linux/init.h>
#include <linux/kstrtox.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/stat.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/sysfs.h>

#include "include/lz4e_module.h"

#include "include/lz4e.h"
#include "include/lz4e_chunk.h"
#include "include/lz4e_dev.h"
#include "include/lz4e_static.h"
#include "include/lz4e_stats.h"

static struct lz4e_module lzmod = {
	.comp_type = LZ4E_COMP_DEFAULT,
	.acceleration = LZ4E_ACCELERATION_DEFAULT,
};

// Callbacks can have unused parameters
// NOLINTBEGIN(misc-unused-parameters)

/* ------------------------- disk mapper/unmapper ------------------------- */

static int lz4e_create_disk(const char *arg, const struct kernel_param *kpar)
{
	struct lz4e_dev *lzdev;
	int ret;

	lzdev = lz4e_dev_alloc(GFP_KERNEL);
	if (!lzdev) {
		LZ4E_PR_ERR("failed to allocate block device");
		return -ENOMEM;
	}

	ret = lz4e_dev_init(lzdev, arg, lzmod.major, LZ4E_FIRST_MINOR,
			    lzmod.comp_type, lzmod.acceleration);
	if (ret) {
		LZ4E_PR_ERR("failed to initialize block device");
		goto free_device;
	}

	lzmod.lzdev = lzdev;

	LZ4E_PR_INFO("device mapped successfully");
	return 0;

free_device:
	lz4e_dev_free(lzdev);
	return ret;
}
LZ4E_CB_W_IF_NO_DEV(lz4e_mapper_w, lz4e_create_disk, lzmod);

static int lz4e_delete_disk(const char *arg, const struct kernel_param *kpar)
{
	lz4e_dev_free(lzmod.lzdev);
	lzmod.lzdev = NULL;

	LZ4E_PR_INFO("device unmapped successfully");
	return 0;
}
LZ4E_CB_W_IF_DEV(lz4e_unmapper_w, lz4e_delete_disk, lzmod);

static int lz4e_get_disk_info(char *buf, const struct kernel_param *kpar)
{
	char *disk_name = lzmod.lzdev->disk->disk_name;
	char *under_disk_name =
		lzmod.lzdev->under_dev->bdev->bd_disk->disk_name;

	return sysfs_emit(buf, "%s: proxy over %s\n", disk_name,
			  under_disk_name);
}
LZ4E_CB_R_IF_DEV(lz4e_mapper_r, lz4e_get_disk_info, lzmod);
LZ4E_CB_R_IF_DEV(lz4e_unmapper_r, lz4e_get_disk_info, lzmod);

/* --------------------------- compression type --------------------------- */

static int lz4e_set_comp_type(const char *arg, const struct kernel_param *kpar)
{
	int icomp = 0;

	for (; (icomp < LZ4E_COMP_TYPE_COUNT) &&
	       (strncmp(arg, lz4e_comp_str[icomp], LZ4E_COMP_STR_LEN) != 0);
	     ++icomp) {
	}

	if (icomp == LZ4E_COMP_TYPE_COUNT) {
		LZ4E_PR_ERR("undefined compression type");
		return -EINVAL;
	}

	lzmod.comp_type = lz4e_comp_type[icomp];

	if (lzmod.lzdev)
		lzmod.lzdev->comp_type = lz4e_comp_type[icomp];

	LZ4E_PR_INFO("set compression type: %s", lz4e_comp_str[icomp]);
	return 0;
}
LZ4E_CB_W(lz4e_comp_type_w, lz4e_set_comp_type);

static int lz4e_get_comp_type(char *buf, const struct kernel_param *kpar)
{
	int icomp = 0;

	for (; (icomp < LZ4E_COMP_TYPE_COUNT) &&
	       (lzmod.comp_type != lz4e_comp_type[icomp]);
	     ++icomp) {
	}

	if (icomp == LZ4E_COMP_TYPE_COUNT) {
		LZ4E_PR_ERR("undefined compression type");
		return -EAGAIN;
	}

	return sysfs_emit(buf, "%s\n", lz4e_comp_str[icomp]);
}
LZ4E_CB_R(lz4e_comp_type_r, lz4e_get_comp_type);

/* ----------------------- compression acceleration ----------------------- */

static int lz4e_set_acceleration(const char *arg,
				 const struct kernel_param *kpar)
{
	int acceleration;
	int ret;

	ret = kstrtoint(arg, 0, &acceleration);
	if (ret) {
		LZ4E_PR_ERR("invalid acceleration factor");
		return ret;
	}

	lzmod.acceleration = acceleration;

	if (lzmod.lzdev)
		lzmod.lzdev->acceleration = acceleration;

	LZ4E_PR_INFO("set acceleration: %d", acceleration);
	return 0;
}
LZ4E_CB_W(lz4e_acceleration_w, lz4e_set_acceleration);

static int lz4e_get_acceleration(char *buf, const struct kernel_param *kpar)
{
	return sysfs_emit(buf, "%d\n", lzmod.acceleration);
}
LZ4E_CB_R(lz4e_acceleration_r, lz4e_get_acceleration);

/* ---------------------------- I/O statistics ---------------------------- */

static int lz4e_reset_stats(const char *arg, const struct kernel_param *kpar)
{
	lz4e_stats_reset(lzmod.lzdev->read_stats);
	lz4e_stats_reset(lzmod.lzdev->write_stats);

	LZ4E_PR_INFO("request stats reset");
	return 0;
}
LZ4E_CB_W_IF_DEV(lz4e_stats_reset_w, lz4e_reset_stats, lzmod);

LZ4E_STATS_R_GETTER(lz4e_stats_r_reqs_total_r, reqs_total, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_reqs_failed_r, reqs_failed, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_min_vec_r, min_vec, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_max_vec_r, max_vec, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_vecs_r, vecs, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_segments_r, segments, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_decomp_size_r, decomp_size, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_comp_size_r, comp_size, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_mem_usage_r, mem_usage, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_copy_ns_r, copy_ns, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_comp_ns_r, comp_ns, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_decomp_ns_r, decomp_ns, lzmod);
LZ4E_STATS_R_GETTER(lz4e_stats_r_total_ns_r, total_ns, lzmod);

LZ4E_STATS_W_GETTER(lz4e_stats_w_reqs_total_r, reqs_total, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_reqs_failed_r, reqs_failed, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_min_vec_r, min_vec, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_max_vec_r, max_vec, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_vecs_r, vecs, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_segments_r, segments, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_decomp_size_r, decomp_size, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_comp_size_r, comp_size, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_mem_usage_r, mem_usage, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_copy_ns_r, copy_ns, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_comp_ns_r, comp_ns, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_decomp_ns_r, decomp_ns, lzmod);
LZ4E_STATS_W_GETTER(lz4e_stats_w_total_ns_r, total_ns, lzmod);

// Callbacks can have unused parameters
// NOLINTEND(misc-unused-parameters)

/* --------------------------- module param ops --------------------------- */

LZ4E_PARAM_OPS(lz4e_mapper_ops, lz4e_mapper_w, lz4e_mapper_r);
LZ4E_PARAM_OPS(lz4e_unmapper_ops, lz4e_unmapper_w, lz4e_unmapper_r);

LZ4E_PARAM_OPS(lz4e_comp_type_ops, lz4e_comp_type_w, lz4e_comp_type_r);
LZ4E_PARAM_OPS(lz4e_acceleration_ops, lz4e_acceleration_w, lz4e_acceleration_r);

LZ4E_PARAM_OPS(lz4e_stats_reset_ops, lz4e_stats_reset_w, NULL);

LZ4E_PARAM_OPS(lz4e_stats_r_reqs_total_ops, NULL, lz4e_stats_r_reqs_total_r);
LZ4E_PARAM_OPS(lz4e_stats_r_reqs_failed_ops, NULL, lz4e_stats_r_reqs_failed_r);
LZ4E_PARAM_OPS(lz4e_stats_r_min_vec_ops, NULL, lz4e_stats_r_min_vec_r);
LZ4E_PARAM_OPS(lz4e_stats_r_max_vec_ops, NULL, lz4e_stats_r_max_vec_r);
LZ4E_PARAM_OPS(lz4e_stats_r_vecs_ops, NULL, lz4e_stats_r_vecs_r);
LZ4E_PARAM_OPS(lz4e_stats_r_segments_ops, NULL, lz4e_stats_r_segments_r);
LZ4E_PARAM_OPS(lz4e_stats_r_decomp_size_ops, NULL, lz4e_stats_r_decomp_size_r);
LZ4E_PARAM_OPS(lz4e_stats_r_comp_size_ops, NULL, lz4e_stats_r_comp_size_r);
LZ4E_PARAM_OPS(lz4e_stats_r_mem_usage_ops, NULL, lz4e_stats_r_mem_usage_r);
LZ4E_PARAM_OPS(lz4e_stats_r_copy_ns_ops, NULL, lz4e_stats_r_copy_ns_r);
LZ4E_PARAM_OPS(lz4e_stats_r_comp_ns_ops, NULL, lz4e_stats_r_comp_ns_r);
LZ4E_PARAM_OPS(lz4e_stats_r_decomp_ns_ops, NULL, lz4e_stats_r_decomp_ns_r);
LZ4E_PARAM_OPS(lz4e_stats_r_total_ns_ops, NULL, lz4e_stats_r_total_ns_r);

LZ4E_PARAM_OPS(lz4e_stats_w_reqs_total_ops, NULL, lz4e_stats_w_reqs_total_r);
LZ4E_PARAM_OPS(lz4e_stats_w_reqs_failed_ops, NULL, lz4e_stats_w_reqs_failed_r);
LZ4E_PARAM_OPS(lz4e_stats_w_min_vec_ops, NULL, lz4e_stats_w_min_vec_r);
LZ4E_PARAM_OPS(lz4e_stats_w_max_vec_ops, NULL, lz4e_stats_w_max_vec_r);
LZ4E_PARAM_OPS(lz4e_stats_w_vecs_ops, NULL, lz4e_stats_w_vecs_r);
LZ4E_PARAM_OPS(lz4e_stats_w_segments_ops, NULL, lz4e_stats_w_segments_r);
LZ4E_PARAM_OPS(lz4e_stats_w_decomp_size_ops, NULL, lz4e_stats_w_decomp_size_r);
LZ4E_PARAM_OPS(lz4e_stats_w_comp_size_ops, NULL, lz4e_stats_w_comp_size_r);
LZ4E_PARAM_OPS(lz4e_stats_w_mem_usage_ops, NULL, lz4e_stats_w_mem_usage_r);
LZ4E_PARAM_OPS(lz4e_stats_w_copy_ns_ops, NULL, lz4e_stats_w_copy_ns_r);
LZ4E_PARAM_OPS(lz4e_stats_w_comp_ns_ops, NULL, lz4e_stats_w_comp_ns_r);
LZ4E_PARAM_OPS(lz4e_stats_w_decomp_ns_ops, NULL, lz4e_stats_w_decomp_ns_r);
LZ4E_PARAM_OPS(lz4e_stats_w_total_ns_ops, NULL, lz4e_stats_w_total_ns_r);

/* --------------------------- module init/exit --------------------------- */

static int __init lz4e_module_init(void)
{
	int major = register_blkdev(LZ4E_MAJOR, LZ4E_DEVICE_NAME);

	if (major < 0) {
		LZ4E_PR_ERR("failed to load module");
		return -EIO;
	}

	lzmod.major = major;

	LZ4E_PR_INFO("module loaded successfully");
	return 0;
}

static void __exit lz4e_module_exit(void)
{
	unregister_blkdev((unsigned int)lzmod.major, LZ4E_DEVICE_NAME);
	lzmod.major = 0;

	lz4e_dev_free(lzmod.lzdev);
	lzmod.lzdev = NULL;

	LZ4E_PR_INFO("module unloaded successfully");
}

/* --------------------------- register module ---------------------------- */

LZ4E_MODULE_PARAM(mapper, lz4e_mapper_ops, S_IRUGO | S_IWUSR,
		  "Map to existing block device");
LZ4E_MODULE_PARAM(unmapper, lz4e_unmapper_ops, S_IRUGO | S_IWUSR,
		  "Unmap from existing block device");

LZ4E_MODULE_PARAM(comp_type, lz4e_comp_type_ops, S_IRUGO | S_IWUSR,
		  "Path for compression (cont, vect, strm, extd)");
LZ4E_MODULE_PARAM(acceleration, lz4e_acceleration_ops, S_IRUGO | S_IWUSR,
		  "Acceleration factor for compression");

LZ4E_MODULE_PARAM(stats_reset, lz4e_stats_reset_ops, S_IWUSR,
		  "Reset all request statistics");

LZ4E_MODULE_PARAM(stats_r_reqs_total, lz4e_stats_r_reqs_total_ops, S_IRUGO,
		  "Total number of read requests");
LZ4E_MODULE_PARAM(stats_r_reqs_failed, lz4e_stats_r_reqs_failed_ops, S_IRUGO,
		  "Number of failed read requests");
LZ4E_MODULE_PARAM(stats_r_min_vec, lz4e_stats_r_min_vec_ops, S_IRUGO,
		  "Minimal size in bytes of I/O vector for read");
LZ4E_MODULE_PARAM(stats_r_max_vec, lz4e_stats_r_max_vec_ops, S_IRUGO,
		  "Maximum size in bytes of I/O vector for read");
LZ4E_MODULE_PARAM(stats_r_vecs, lz4e_stats_r_vecs_ops, S_IRUGO,
		  "Number of multi-page I/O vectors read from");
LZ4E_MODULE_PARAM(stats_r_segments, lz4e_stats_r_segments_ops, S_IRUGO,
		  "Number of single-page segments read from");
LZ4E_MODULE_PARAM(stats_r_decomp_size, lz4e_stats_r_decomp_size_ops, S_IRUGO,
		  "Total size in bytes of read data before compression");
LZ4E_MODULE_PARAM(stats_r_comp_size, lz4e_stats_r_comp_size_ops, S_IRUGO,
		  "Total size in bytes of read data after compression");
LZ4E_MODULE_PARAM(stats_r_mem_usage, lz4e_stats_r_mem_usage_ops, S_IRUGO,
		  "Memory used for running compression for read");
LZ4E_MODULE_PARAM(stats_r_copy_ns, lz4e_stats_r_copy_ns_ops, S_IRUGO,
		  "Time elapsed during data copying for read in nanoseconds");
LZ4E_MODULE_PARAM(stats_r_comp_ns, lz4e_stats_r_comp_ns_ops, S_IRUGO,
		  "Time elapsed during compression for read in nanoseconds");
LZ4E_MODULE_PARAM(stats_r_decomp_ns, lz4e_stats_r_decomp_ns_ops, S_IRUGO,
		  "Time elapsed during decompression for read in nanoseconds");
LZ4E_MODULE_PARAM(stats_r_total_ns, lz4e_stats_r_total_ns_ops, S_IRUGO,
		  "Total time elapsed during read in nanoseconds");

LZ4E_MODULE_PARAM(stats_w_reqs_total, lz4e_stats_w_reqs_total_ops, S_IRUGO,
		  "Total number of write requests");
LZ4E_MODULE_PARAM(stats_w_reqs_failed, lz4e_stats_w_reqs_failed_ops, S_IRUGO,
		  "Number of failed write requests");
LZ4E_MODULE_PARAM(stats_w_min_vec, lz4e_stats_w_min_vec_ops, S_IRUGO,
		  "Minimal size in bytes of I/O vector for write");
LZ4E_MODULE_PARAM(stats_w_max_vec, lz4e_stats_w_max_vec_ops, S_IRUGO,
		  "Maximum size in bytes of I/O vector for write");
LZ4E_MODULE_PARAM(stats_w_vecs, lz4e_stats_w_vecs_ops, S_IRUGO,
		  "Number of multi-page I/O vectors written to");
LZ4E_MODULE_PARAM(stats_w_segments, lz4e_stats_w_segments_ops, S_IRUGO,
		  "Number of single-page segments written to");
LZ4E_MODULE_PARAM(stats_w_decomp_size, lz4e_stats_w_decomp_size_ops, S_IRUGO,
		  "Total size in bytes of written data before compression");
LZ4E_MODULE_PARAM(stats_w_comp_size, lz4e_stats_w_comp_size_ops, S_IRUGO,
		  "Total size in bytes of written data after compression");
LZ4E_MODULE_PARAM(stats_w_mem_usage, lz4e_stats_w_mem_usage_ops, S_IRUGO,
		  "Memory used for running compression for write");
LZ4E_MODULE_PARAM(stats_w_copy_ns, lz4e_stats_w_copy_ns_ops, S_IRUGO,
		  "Time elapsed during data copying for write in nanoseconds");
LZ4E_MODULE_PARAM(stats_w_comp_ns, lz4e_stats_w_comp_ns_ops, S_IRUGO,
		  "Time elapsed during compression for write in nanoseconds");
LZ4E_MODULE_PARAM(stats_w_decomp_ns, lz4e_stats_w_decomp_ns_ops, S_IRUGO,
		  "Time elapsed during decompression for write in nanoseconds");
LZ4E_MODULE_PARAM(stats_w_total_ns, lz4e_stats_w_total_ns_ops, S_IRUGO,
		  "Total time elapsed during write in nanoseconds");

module_init(lz4e_module_init);
module_exit(lz4e_module_exit);

MODULE_AUTHOR("Alexander Bugaev");
MODULE_DESCRIPTION("Proxy block device for testing extended LZ4");
MODULE_LICENSE("GPL");
