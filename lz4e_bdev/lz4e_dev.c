// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/gfp_types.h>
#include <linux/init.h>
#include <linux/nodemask_types.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/types.h>

#include "include/lz4e_dev.h"

#include "include/lz4e_chunk.h"
#include "include/lz4e_req.h"
#include "include/lz4e_static.h"
#include "include/lz4e_stats.h"
#include "include/lz4e_under_dev.h"

static const struct block_device_operations lz4e_disk_ops = {
	.owner = THIS_MODULE,
	.submit_bio = lz4e_dev_submit_bio,
};

static void lz4e_gendisk_free(struct gendisk *disk)
{
	if (!disk)
		return;

	del_gendisk(disk);
	put_disk(disk);

	LZ4E_PR_DEBUG("released disk");
}

static struct gendisk *lz4e_gendisk_alloc(void)
{
	struct gendisk *disk;

	disk = blk_alloc_disk(NULL, NUMA_NO_NODE);
	if (!disk) {
		LZ4E_PR_ERR("failed to allocate disk");
		return NULL;
	}

	LZ4E_PR_DEBUG("allocated disk");
	return disk;
}

static int lz4e_gendisk_add(struct gendisk *disk, struct lz4e_dev *lzdev,
			    int major, int first_minor)
{
	int ret;

	disk->major = major;
	disk->first_minor = first_minor;
	disk->minors = 1;
	disk->fops = &lz4e_disk_ops;
	disk->private_data = lzdev;

	/* do not support multiple minors, disable partition support */
	disk->flags |= GENHD_FL_NO_PART;

	set_capacity(disk, get_capacity(lzdev->under_dev->bdev->bd_disk));

	ret = snprintf(disk->disk_name, DISK_NAME_LEN, LZ4E_MODULE_NAME "%d",
		       disk->first_minor);
	if (ret < 0) {
		LZ4E_PR_ERR("failed to write disk name");
		return ret;
	}

	ret = add_disk(disk);
	if (ret) {
		LZ4E_PR_ERR("failed to add disk: %s", disk->disk_name);
		return ret;
	}

	LZ4E_PR_INFO("initialized disk: %s", disk->disk_name);
	return 0;
}

void lz4e_dev_free(struct lz4e_dev *lzdev)
{
	if (!lzdev)
		return;

	lz4e_gendisk_free(lzdev->disk);
	lz4e_under_dev_free(lzdev->under_dev);
	lz4e_stats_free(lzdev->read_stats);
	lz4e_stats_free(lzdev->write_stats);

	kfree(lzdev);

	LZ4E_PR_DEBUG("released block device");
}

struct lz4e_dev *lz4e_dev_alloc(gfp_t gfp_mask)
{
	struct lz4e_dev *lzdev;
	struct lz4e_under_dev *under_dev;
	struct gendisk *disk;
	struct lz4e_stats *read_stats;
	struct lz4e_stats *write_stats;

	lzdev = kzalloc(sizeof(*lzdev), gfp_mask);
	if (!lzdev) {
		LZ4E_PR_ERR("failed to allocate dev struct: %zu bytes",
			    sizeof(*lzdev));
		goto error;
	}

	under_dev = lz4e_under_dev_alloc(gfp_mask);
	if (!under_dev) {
		LZ4E_PR_ERR("failed to allocate underlying device");
		goto free_dev;
	}

	disk = lz4e_gendisk_alloc();
	if (!disk) {
		LZ4E_PR_ERR("failed to allocate disk");
		goto free_under_dev;
	}

	read_stats = lz4e_stats_alloc(gfp_mask);
	if (!read_stats) {
		LZ4E_PR_ERR("failed to allocate read stats");
		goto free_disk;
	}

	write_stats = lz4e_stats_alloc(gfp_mask);
	if (!write_stats) {
		LZ4E_PR_ERR("failed to allocate write stats");
		goto free_read_stats;
	}

	lzdev->under_dev = under_dev;
	lzdev->disk = disk;
	lzdev->read_stats = read_stats;
	lzdev->write_stats = write_stats;

	LZ4E_PR_DEBUG("allocated block device");
	return lzdev;

free_read_stats:
	lz4e_stats_free(read_stats);
free_disk:
	lz4e_gendisk_free(disk);
free_under_dev:
	lz4e_under_dev_free(under_dev);
free_dev:
	kfree(lzdev);
error:
	return NULL;
}

int lz4e_dev_init(struct lz4e_dev *lzdev, const char *dev_path, int major,
		  int first_minor, lz4e_comp_t comp_type, int acceleration)
{
	struct gendisk *disk = lzdev->disk;
	struct lz4e_under_dev *under_dev = lzdev->under_dev;
	int ret;

	ret = lz4e_under_dev_init(under_dev, dev_path);
	if (ret) {
		LZ4E_PR_ERR("failed to open underlying device");
		return ret;
	}

	ret = lz4e_gendisk_add(disk, lzdev, major, first_minor);
	if (ret) {
		LZ4E_PR_ERR("failed to add disk");
		return ret;
	}

	lzdev->comp_type = comp_type;
	lzdev->acceleration = acceleration;

	LZ4E_PR_DEBUG("initialized block device");
	return 0;
}

void lz4e_dev_submit_bio(struct bio *orig_bio)
{
	struct lz4e_dev *lzdev = orig_bio->bi_bdev->bd_disk->private_data;
	struct lz4e_req *lzreq;
	blk_status_t status;

	lzreq = lz4e_req_alloc(orig_bio, lzdev->under_dev, GFP_NOIO,
			       lzdev->comp_type);
	if (!lzreq) {
		LZ4E_PR_ERR("failed to allocate request");
		status = BLK_STS_RESOURCE;
		goto submit_with_err;
	}

	status = lz4e_req_init(lzreq, orig_bio, lzdev);
	if (status != BLK_STS_OK) {
		LZ4E_PR_ERR("failed to initialize request");
		goto free_request;
	}

	lz4e_req_submit(lzreq);

	LZ4E_PR_DEBUG("submitted request");
	return;

free_request:
	lz4e_req_free(lzreq);
submit_with_err:
	orig_bio->bi_status = status;
	bio_endio(orig_bio);
}
