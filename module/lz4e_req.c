// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <asm-generic/errno-base.h>
#include <asm/page.h>
#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/blkdev.h>
#include <linux/gfp_types.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <vdso/page.h>

#include "include/lz4e_req.h"

#include "include/lz4e_chunk.h"
#include "include/lz4e_dev.h"
#include "include/lz4e_static.h"
#include "include/lz4e_stats.h"
#include "include/lz4e_under_dev.h"

void LZ4E_req_free(struct LZ4E_req *lzreq)
{
	if (!lzreq)
		return;

	LZ4E_chunk_free(lzreq->chunk);

	kfree(lzreq);

	LZ4E_PR_DEBUG("released request context");
}

struct LZ4E_req *LZ4E_req_alloc(void)
{
	struct LZ4E_req *lzreq;

	lzreq = kzalloc(sizeof(*lzreq), GFP_NOIO);
	if (!lzreq) {
		LZ4E_PR_ERR("failed to allocate request context");
		return NULL;
	}

	LZ4E_PR_DEBUG("allocated request context");
	return lzreq;
}

static blk_status_t LZ4E_read_req_init(struct LZ4E_req *lzreq,
				       struct bio *original_bio,
				       struct LZ4E_dev *lzdev)
{
	struct block_device *bdev = lzdev->under_dev->bdev;
	struct bio_set *bset = lzdev->under_dev->bset;
	struct LZ4E_stats *stats_to_update = lzdev->read_stats;
	struct bio *new_bio;

	new_bio = bio_alloc_clone(bdev, original_bio, GFP_NOIO, bset);
	if (!new_bio) {
		LZ4E_PR_ERR("failed to clone original bio");
		return BLK_STS_RESOURCE;
	}

	new_bio->bi_vcnt = original_bio->bi_vcnt;
	lzreq->original_bio = original_bio;
	lzreq->new_bio = new_bio;
	lzreq->stats_to_update = stats_to_update;

	LZ4E_PR_DEBUG("initialized read request");
	return BLK_STS_OK;
}

static inline unsigned int LZ4E_bio_page_count(struct bio *bio)
{
	unsigned int pages = DIV_ROUND_UP_SECTOR_T(bio_sectors(bio),
						   PAGE_SIZE / SECTOR_SIZE);

	return min_t(unsigned int, pages, BIO_MAX_VECS);
}

static struct bio *LZ4E_alloc_new_bio(struct bio *original_bio,
				      struct LZ4E_under_dev *under_dev)
{
	struct block_device *bdev = under_dev->bdev;
	struct bio_set *bset = under_dev->bset;
	struct bio *new_bio;

	new_bio = bio_alloc_bioset(
		bdev, (unsigned short)LZ4E_bio_page_count(original_bio),
		original_bio->bi_opf, GFP_NOIO, bset);
	if (!new_bio) {
		LZ4E_PR_ERR("failed to allocate new bio");
		return NULL;
	}

	new_bio->bi_iter.bi_sector = original_bio->bi_iter.bi_sector;

	LZ4E_PR_DEBUG("allocated new bio");
	return new_bio;
}

static int LZ4E_add_buf_to_bio(struct bio *bio, struct LZ4E_buffer *buf)
{
	char *data = buf->data;
	unsigned int data_len = (unsigned int)buf->data_size;
	unsigned int page_off;
	unsigned int page_len;
	int ret;

	page_off = offset_in_page(data);
	page_len = min_t(unsigned int, data_len, PAGE_SIZE - page_off);

	while (data_len) {
		ret = bio_add_page(bio, virt_to_page(data), page_len, page_off);
		if (ret != page_len) {
			LZ4E_PR_ERR("failed to add page to bio");
			return -EAGAIN;
		}

		data += page_len;
		data_len -= page_len;

		page_off = 0;
		page_len = min_t(unsigned int, data_len, PAGE_SIZE);
	}

	LZ4E_PR_DEBUG("added buffer to bio");
	return 0;
}

static blk_status_t LZ4E_write_req_init(struct LZ4E_req *lzreq,
					struct bio *original_bio,
					struct LZ4E_dev *lzdev)
{
	struct LZ4E_stats *stats_to_update = lzdev->write_stats;
	struct LZ4E_chunk *chunk;
	struct bio *new_bio;
	blk_status_t status;
	int ret;

	chunk = LZ4E_chunk_alloc((int)original_bio->bi_iter.bi_size);
	if (!chunk) {
		LZ4E_PR_ERR("failed to allocate chunk");
		return BLK_STS_RESOURCE;
	}

	LZ4E_buf_copy_from_bio(&chunk->src_buf, original_bio);

	ret = LZ4E_chunk_compress(chunk);
	if (ret) {
		LZ4E_PR_ERR("failed to compress data");
		status = BLK_STS_IOERR;
		goto free_chunk;
	}

	ret = LZ4E_chunk_decompress(chunk);
	if (ret) {
		LZ4E_PR_ERR("failed to decompress data");
		status = BLK_STS_IOERR;
		goto free_chunk;
	}

	new_bio = LZ4E_alloc_new_bio(original_bio, lzdev->under_dev);
	if (!new_bio) {
		LZ4E_PR_ERR("failed to allocate new bio");
		status = BLK_STS_RESOURCE;
		goto free_chunk;
	}

	ret = LZ4E_add_buf_to_bio(new_bio, &chunk->src_buf);
	if (ret) {
		LZ4E_PR_ERR("failed to add buffer to bio");
		status = BLK_STS_IOERR;
		goto put_new_bio;
	}

	lzreq->original_bio = original_bio;
	lzreq->new_bio = new_bio;
	lzreq->stats_to_update = stats_to_update;
	lzreq->chunk = chunk;

	LZ4E_PR_DEBUG("initialized write request");
	return BLK_STS_OK;

put_new_bio:
	bio_put(new_bio);
free_chunk:
	LZ4E_chunk_free(chunk);
	return status;
}

blk_status_t LZ4E_req_init(struct LZ4E_req *lzreq, struct bio *original_bio,
			   struct LZ4E_dev *lzdev)
{
	enum req_op op_type = bio_op(original_bio);

	switch (op_type) {
	case REQ_OP_READ:
		return LZ4E_read_req_init(lzreq, original_bio, lzdev);
	case REQ_OP_WRITE:
		return LZ4E_write_req_init(lzreq, original_bio, lzdev);
	default:
		LZ4E_PR_ERR("unsupported request operation");
		return BLK_STS_NOTSUPP;
	}
}

static void LZ4E_end_io(struct bio *new_bio)
{
	struct LZ4E_req *lzreq = new_bio->bi_private;
	struct bio *original_bio = lzreq->original_bio;
	struct LZ4E_stats *stats_to_update = lzreq->stats_to_update;

	LZ4E_stats_update(stats_to_update, new_bio);

	LZ4E_PR_INFO("completed bio request");

	original_bio->bi_status = new_bio->bi_status;
	bio_endio(original_bio);

	bio_put(new_bio);
	LZ4E_req_free(lzreq);
}

void LZ4E_req_submit(struct LZ4E_req *lzreq)
{
	struct bio *new_bio = lzreq->new_bio;

	new_bio->bi_end_io = LZ4E_end_io;
	new_bio->bi_private = lzreq;

	submit_bio_noacct(new_bio);

	LZ4E_PR_DEBUG("submitted request to underlying device");
}
