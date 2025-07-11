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
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/stddef.h>

#include "include/blk_comp_req.h"

#include "include/blk_comp_dev.h"
#include "include/blk_comp_stats.h"
#include "include/blk_comp_under_dev.h"

// Callback for ending new requests
static void blk_comp_end_io(struct bio *new_bio)
{
	struct blk_comp_req *bcreq = (struct blk_comp_req *)new_bio->bi_private;
	struct bio	    *original_bio      = bcreq->original_bio;
	struct blk_comp_stats *stats_to_update = bcreq->stats_to_update;
	blk_status_t	       status	       = new_bio->bi_status;

	kfree(bcreq);

	blk_comp_stats_update(stats_to_update, new_bio);
	bio_put(new_bio);

	original_bio->bi_status = status;
	bio_endio(original_bio);

	pr_info("Completed bio request");
}

// Free request context
void blk_comp_req_free(struct blk_comp_req *bcreq)
{
	kfree(bcreq);

	pr_info("Released request context");
}

// Allocate request context
struct blk_comp_req *blk_comp_req_alloc(void)
{
	struct blk_comp_req *bcreq = NULL;

	bcreq = kzalloc(sizeof(*bcreq), GFP_NOIO);
	if (bcreq == NULL) {
		pr_err("Failed to allocate request context");
		return NULL;
	}

	pr_info("Allocated request context");
	return bcreq;
}

// Initialize request to device with given bio
blk_status_t blk_comp_req_init(struct blk_comp_req *bcreq,
			       struct bio	   *original_bio,
			       struct blk_comp_dev *bcdev)
{
	struct blk_comp_under_dev *under_dev	   = bcdev->under_dev;
	struct blk_comp_stats	  *stats_to_update = NULL;

	enum req_op op_type = bio_op(original_bio);

	switch (op_type) {
	case REQ_OP_READ:
		stats_to_update = bcdev->read_stats;
		break;
	case REQ_OP_WRITE:
		stats_to_update = bcdev->write_stats;
		break;
	default:
		pr_err("Unsupported request operation");
		return BLK_STS_NOTSUPP;
	}

	bcreq->original_bio    = original_bio;
	bcreq->under_dev       = under_dev;
	bcreq->stats_to_update = stats_to_update;

	pr_info("Initialized request");
	return BLK_STS_OK;
}

// Submit request to underlying device
blk_status_t blk_comp_req_submit(struct blk_comp_req *bcreq)
{
	struct bio		  *original_bio = bcreq->original_bio;
	struct blk_comp_under_dev *under_dev	= bcreq->under_dev;
	struct block_device	  *bdev		= under_dev->bdev;
	struct bio_set		  *bset		= under_dev->bset;

	struct bio *new_bio =
		bio_alloc_clone(bdev, original_bio, GFP_NOIO, bset);
	if (new_bio == NULL) {
		pr_err("Failed to clone original bio");
		return BLK_STS_RESOURCE;
	}

	new_bio->bi_iter    = original_bio->bi_iter;
	new_bio->bi_private = bcreq;
	new_bio->bi_end_io  = blk_comp_end_io;

	submit_bio_noacct(new_bio);

	pr_info("Submitted request to underlying device");
	return BLK_STS_OK;
}
