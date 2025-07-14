// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/gfp_types.h>
#include <linux/slab.h>
#include <linux/stddef.h>

#include "include/blk_comp_dev.h"

#include "include/blk_comp_req.h"
#include "include/blk_comp_static.h"
#include "include/blk_comp_stats.h"
#include "include/blk_comp_under_dev.h"
#include "include/gendisk_utils.h"

void LZ4E_dev_free(struct LZ4E_dev *bcdev)
{
	if (!bcdev)
		return;

	struct gendisk *disk = bcdev->disk;
	struct LZ4E_under_dev *under_dev = bcdev->under_dev;
	struct LZ4E_stats *read_stats = bcdev->read_stats;
	struct LZ4E_stats *write_stats = bcdev->write_stats;

	LZ4E_gendisk_free(disk);
	LZ4E_under_dev_free(under_dev);
	LZ4E_stats_free(read_stats);
	LZ4E_stats_free(write_stats);

	kfree(bcdev);

	LZ4E_PR_DEBUG("released block device context");
}

struct LZ4E_dev *LZ4E_dev_alloc(void)
{
	struct gendisk *disk;
	struct LZ4E_under_dev *under_dev;
	struct LZ4E_stats *read_stats;
	struct LZ4E_stats *write_stats;
	struct LZ4E_dev *bcdev;

	bcdev = kzalloc(sizeof(*bcdev), GFP_KERNEL);
	if (!bcdev) {
		LZ4E_PR_ERR("failed to allocate block device context");
		return NULL;
	}

	under_dev = LZ4E_under_dev_alloc();
	bcdev->under_dev = under_dev;
	if (!under_dev) {
		LZ4E_PR_ERR("failed to allocate underlying device context");
		goto free_device;
	}

	disk = LZ4E_gendisk_alloc();
	bcdev->disk = disk;
	if (!disk) {
		LZ4E_PR_ERR("failed to allocate generic disk context");
		goto free_device;
	}

	read_stats = LZ4E_stats_alloc();
	bcdev->read_stats = read_stats;
	if (!read_stats) {
		LZ4E_PR_ERR("failed to allocate read stats");
		goto free_device;
	}

	write_stats = LZ4E_stats_alloc();
	bcdev->write_stats = write_stats;
	if (!write_stats) {
		LZ4E_PR_ERR("failed to allocate write stats");
		goto free_device;
	}

	LZ4E_PR_DEBUG("allocated block device context");
	return bcdev;

free_device:
	LZ4E_dev_free(bcdev);
	return NULL;
}

int LZ4E_dev_init(struct LZ4E_dev *bcdev, const char *dev_path, int major,
		  int first_minor)
{
	struct gendisk *disk = bcdev->disk;
	struct LZ4E_under_dev *under_dev = bcdev->under_dev;
	int ret;

	ret = LZ4E_under_dev_open(under_dev, dev_path);
	if (ret) {
		LZ4E_PR_ERR("failed to open underlying device");
		return ret;
	}

	ret = LZ4E_gendisk_add(disk, bcdev, major, first_minor);
	if (ret) {
		LZ4E_PR_ERR("failed to add generic disk");
		return ret;
	}

	LZ4E_PR_DEBUG("initialized block device");
	return 0;
}

void LZ4E_dev_submit_bio(struct bio *original_bio)
{
	struct LZ4E_dev *bcdev = original_bio->bi_bdev->bd_disk->private_data;
	struct LZ4E_req *bcreq;
	blk_status_t status;

	bcreq = LZ4E_req_alloc();
	if (!bcreq) {
		LZ4E_PR_ERR("failed to allocate request context");
		status = BLK_STS_RESOURCE;
		goto submit_with_err;
	}

	status = LZ4E_req_init(bcreq, original_bio, bcdev);
	if (status != BLK_STS_OK) {
		LZ4E_PR_ERR("failed to initialize request");
		goto free_request;
	}

	status = LZ4E_req_submit(bcreq);
	if (status != BLK_STS_OK) {
		LZ4E_PR_ERR("failed to submit request");
		goto free_request;
	}

	LZ4E_PR_INFO("submitted bio request");
	return;

free_request:
	LZ4E_req_free(bcreq);
submit_with_err:
	original_bio->bi_status = status;
	bio_endio(original_bio);
}
