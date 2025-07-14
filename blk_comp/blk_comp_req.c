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

#include "include/blk_comp_req.h"

#include "include/blk_comp_dev.h"
#include "include/blk_comp_static.h"
#include "include/blk_comp_stats.h"
#include "include/blk_comp_under_dev.h"

void LZ4E_req_free(struct LZ4E_req *bcreq)
{
	kfree(bcreq);

	LZ4E_PR_DEBUG("released request context");
}

struct LZ4E_req *LZ4E_req_alloc(void)
{
	struct LZ4E_req *bcreq;

	bcreq = kzalloc(sizeof(*bcreq), GFP_NOIO);
	if (!bcreq) {
		LZ4E_PR_ERR("failed to allocate request context");
		return NULL;
	}

	LZ4E_PR_DEBUG("allocated request context");
	return bcreq;
}

blk_status_t LZ4E_req_init(struct LZ4E_req *bcreq, struct bio *original_bio,
			   struct LZ4E_dev *bcdev)
{
	enum req_op op_type = bio_op(original_bio);
	struct LZ4E_under_dev *under_dev = bcdev->under_dev;
	struct LZ4E_stats *stats_to_update;

	switch (op_type) {
	case REQ_OP_READ:
		stats_to_update = bcdev->read_stats;
		break;
	case REQ_OP_WRITE:
		stats_to_update = bcdev->write_stats;
		break;
	default:
		LZ4E_PR_ERR("unsupported request operation");
		return BLK_STS_NOTSUPP;
	}

	bcreq->original_bio = original_bio;
	bcreq->under_dev = under_dev;
	bcreq->stats_to_update = stats_to_update;

	LZ4E_PR_DEBUG("initialized request");
	return BLK_STS_OK;
}

static void LZ4E_end_io(struct bio *new_bio)
{
	struct LZ4E_req *bcreq = new_bio->bi_private;
	struct bio *original_bio = bcreq->original_bio;
	struct LZ4E_stats *stats_to_update = bcreq->stats_to_update;

	LZ4E_stats_update(stats_to_update, new_bio);

	LZ4E_PR_INFO("completed bio request");

	original_bio->bi_status = new_bio->bi_status;
	bio_endio(original_bio);

	bio_put(new_bio);
	LZ4E_req_free(bcreq);
}

blk_status_t LZ4E_req_submit(struct LZ4E_req *bcreq)
{
	struct bio *original_bio = bcreq->original_bio;
	struct block_device *bdev = bcreq->under_dev->bdev;
	struct bio_set *bset = bcreq->under_dev->bset;
	struct bio *new_bio;

	new_bio = bio_alloc_clone(bdev, original_bio, GFP_NOIO, bset);
	if (!new_bio) {
		LZ4E_PR_ERR("failed to clone original bio");
		return BLK_STS_RESOURCE;
	}

	new_bio->bi_end_io = LZ4E_end_io;
	new_bio->bi_private = bcreq;

	submit_bio_noacct(new_bio);

	LZ4E_PR_DEBUG("submitted request to underlying device");
	return BLK_STS_OK;
}
