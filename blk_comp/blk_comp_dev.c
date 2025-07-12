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

#include "include/blk_comp_module.h"
#include "include/blk_comp_req.h"
#include "include/blk_comp_stats.h"
#include "include/blk_comp_under_dev.h"
#include "include/gendisk_utils.h"

// Free block device context
void blk_comp_dev_free(struct blk_comp_dev *bcdev)
{
	if (bcdev == NULL)
		return;

	struct gendisk		  *disk	       = bcdev->disk;
	struct blk_comp_under_dev *under_dev   = bcdev->under_dev;
	struct blk_comp_stats	  *read_stats  = bcdev->read_stats;
	struct blk_comp_stats	  *write_stats = bcdev->write_stats;

	blk_comp_gendisk_free(disk);
	blk_comp_under_dev_free(under_dev);
	blk_comp_stats_free(read_stats);
	blk_comp_stats_free(write_stats);

	kfree(bcdev);

	BLK_COMP_PR_DEBUG("released block device context");
}

// Allocate block device context
struct blk_comp_dev *blk_comp_dev_alloc(void)
{
	struct blk_comp_dev	  *bcdev       = NULL;
	struct gendisk		  *disk	       = NULL;
	struct blk_comp_under_dev *under_dev   = NULL;
	struct blk_comp_stats	  *read_stats  = NULL;
	struct blk_comp_stats	  *write_stats = NULL;

	bcdev = kzalloc(sizeof(*bcdev), GFP_KERNEL);
	if (bcdev == NULL) {
		BLK_COMP_PR_ERR("failed to allocate block device context");
		return NULL;
	}

	under_dev	 = blk_comp_under_dev_alloc();
	bcdev->under_dev = under_dev;
	if (under_dev == NULL) {
		BLK_COMP_PR_ERR("failed to allocate underlying device context");
		goto free_device;
	}

	disk	    = blk_comp_gendisk_alloc();
	bcdev->disk = disk;
	if (disk == NULL) {
		BLK_COMP_PR_ERR("failed to allocate generic disk context");
		goto free_device;
	}

	read_stats	  = blk_comp_stats_alloc();
	bcdev->read_stats = read_stats;
	if (read_stats == NULL) {
		BLK_COMP_PR_ERR("failed to allocate read stats");
		goto free_device;
	}

	write_stats	   = blk_comp_stats_alloc();
	bcdev->write_stats = write_stats;
	if (write_stats == NULL) {
		BLK_COMP_PR_ERR("failed to allocate write stats");
		goto free_device;
	}

	BLK_COMP_PR_DEBUG("allocated block device context");
	return bcdev;

free_device:
	blk_comp_dev_free(bcdev);
	return NULL;
}

// Initialize the device to be managed by the driver
int blk_comp_dev_init(struct blk_comp_dev *bcdev, const char *dev_path,
		      int major, int first_minor)
{
	int			   ret	     = 0;
	struct gendisk		  *disk	     = bcdev->disk;
	struct blk_comp_under_dev *under_dev = bcdev->under_dev;

	ret = blk_comp_under_dev_open(under_dev, dev_path);
	if (ret) {
		BLK_COMP_PR_ERR("failed to open underlying device");
		return ret;
	}

	ret = blk_comp_gendisk_add(disk, bcdev, major, first_minor);
	if (ret) {
		BLK_COMP_PR_ERR("failed to add generic disk");
		return ret;
	}

	BLK_COMP_PR_DEBUG("initialized block device");
	return 0;
}

// Submit bio request to the device
void blk_comp_dev_submit_bio(struct bio *original_bio)
{
	blk_status_t	     status = BLK_STS_OK;
	struct blk_comp_dev *bcdev =
		original_bio->bi_bdev->bd_disk->private_data;

	struct blk_comp_req *bcreq = blk_comp_req_alloc();
	if (bcreq == NULL) {
		BLK_COMP_PR_ERR("failed to allocate request context");
		status = BLK_STS_RESOURCE;
		goto submit_with_err;
	}

	status = blk_comp_req_init(bcreq, original_bio, bcdev);
	if (status != BLK_STS_OK) {
		BLK_COMP_PR_ERR("failed to initialize request");
		goto free_request;
	}

	status = blk_comp_req_submit(bcreq);
	if (status != BLK_STS_OK) {
		BLK_COMP_PR_ERR("failed to submit request");
		goto free_request;
	}

	BLK_COMP_PR_INFO("submitted bio request");
	return;

free_request:
	kfree(bcreq);
submit_with_err:
	original_bio->bi_status = status;
	bio_endio(original_bio);
}
