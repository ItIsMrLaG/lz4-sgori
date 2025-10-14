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
#include <linux/lz4.h>
#include <linux/math.h>
#include <linux/minmax.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <vdso/page.h>

#include "include/module/lz4e_req.h"

#include "include/module/lz4e_chunk.h"
#include "include/module/lz4e_dev.h"
#include "include/module/lz4e_static.h"
#include "include/module/lz4e_stats.h"
#include "include/module/lz4e_under_dev.h"

void lz4e_req_free(struct lz4e_req *lzreq)
{
	if (!lzreq)
		return;

	lz4e_chunk_free(lzreq->chunk);

	kfree(lzreq);

	LZ4E_PR_DEBUG("released request context");
}

struct lz4e_req *lz4e_req_alloc(void)
{
	struct lz4e_req *lzreq;

	lzreq = kzalloc(sizeof(*lzreq), GFP_NOIO);
	if (!lzreq) {
		LZ4E_PR_ERR("failed to allocate request context");
		return NULL;
	}

	LZ4E_PR_DEBUG("allocated request context");
	return lzreq;
}

static blk_status_t lz4e_read_req_init(struct lz4e_req *lzreq,
				       struct bio *original_bio,
				       struct lz4e_dev *lzdev)
{
	struct block_device *bdev = lzdev->under_dev->bdev;
	struct bio_set *bset = lzdev->under_dev->bset;
	struct lz4e_stats *stats_to_update = lzdev->read_stats;
	struct bio *new_bio;

	new_bio = bio_alloc(bdev, original_bio->bi_vcnt, REQ_OP_READ, GFP_NOIO);
	if (!new_bio) {
		LZ4E_PR_ERR("failed to alloc new bio");
		return BLK_STS_RESOURCE;
	}

	lzreq->original_bio = original_bio;
	lzreq->new_bio = new_bio;
	lzreq->stats_to_update = stats_to_update;

	LZ4E_PR_DEBUG("initialized read request");
	return BLK_STS_OK;
}

static inline unsigned short lz4e_bio_bytes_to_pages(size_t bytes)
{
	size_t pages = DIV_ROUND_UP(bytes, PAGE_SIZE);

	return (unsigned short)min_t(size_t, pages, BIO_MAX_VECS);
}

static struct bio *lz4e_alloc_new_bio(struct bio *original_bio,
				      struct lz4e_under_dev *under_dev)
{
	size_t bsize = LZ4_COMPRESSBOUND(original_bio->bi_iter.bi_size);
	struct block_device *bdev = under_dev->bdev;
	struct bio_set *bset = under_dev->bset;
	struct bio *new_bio;

	new_bio = bio_alloc_bioset(bdev, lz4e_bio_bytes_to_pages(bsize),
				   original_bio->bi_opf, GFP_NOIO, bset);
	if (!new_bio) {
		LZ4E_PR_ERR("failed to allocate new bio");
		return NULL;
	}

	new_bio->bi_iter.bi_sector = original_bio->bi_iter.bi_sector;

	LZ4E_PR_DEBUG("allocated new bio");
	return new_bio;
}

static void lz4e_reset_bio(struct bio *bio_to_reset, struct bio *original_bio,
			   struct lz4e_under_dev *under_dev)
{
	bio_reset(bio_to_reset, under_dev->bdev, original_bio->bi_opf);

	bio_to_reset->bi_iter.bi_sector = original_bio->bi_iter.bi_sector;

	LZ4E_PR_DEBUG("reset new bio");
}

static int lz4e_add_buf_to_bio(struct bio *bio, struct lz4e_buffer *buf)
{
	char *data = buf->data;
	unsigned int buf_len = (unsigned int)buf->buf_size;
	unsigned int page_off;
	unsigned int page_len;
	int ret;

	page_off = offset_in_page(data);
	page_len = min_t(unsigned int, buf_len, PAGE_SIZE - page_off);

	while (buf_len) {
		ret = bio_add_page(bio, virt_to_page(data), page_len, page_off);
		if (ret != page_len) {
			LZ4E_PR_ERR("failed to add page to bio");
			return -EAGAIN;
		}

		data += page_len;
		buf_len -= page_len;

		page_off = 0;
		page_len = min_t(unsigned int, buf_len, PAGE_SIZE);
	}

	LZ4E_PR_DEBUG("added buffer to bio");
	return 0;
}

static blk_status_t lz4e_write_req_init(struct lz4e_req *lzreq,
					struct bio *original_bio,
					struct lz4e_dev *lzdev)
{
	struct lz4e_stats *stats_to_update = lzdev->write_stats;
	struct lz4e_chunk *chunk;
	struct bio *new_bio;
	blk_status_t status;
	int ret;

	chunk = lz4e_chunk_alloc((int)original_bio->bi_iter.bi_size);
	if (!chunk) {
		LZ4E_PR_ERR("failed to allocate chunk");
		return BLK_STS_RESOURCE;
	}

	new_bio = lz4e_alloc_new_bio(original_bio, lzdev->under_dev);
	if (!new_bio) {
		LZ4E_PR_ERR("failed to allocate new bio");
		status = BLK_STS_RESOURCE;
		goto free_chunk;
	}

	ret = lz4e_add_buf_to_bio(new_bio, &chunk->dst_buf);
	if (ret) {
		LZ4E_PR_ERR("failed to add dst buffer to bio");
		status = BLK_STS_IOERR;
		goto put_new_bio;
	}

	chunk->src_buf.bio = original_bio;
	chunk->dst_buf.bio = new_bio;

	ret = lz4e_chunk_compress_ext(chunk);
	if (ret) {
		LZ4E_PR_ERR("failed to compress data");
		status = BLK_STS_IOERR;
		goto put_new_bio;
	}

	ret = lz4e_chunk_decompress(chunk);
	if (ret) {
		LZ4E_PR_ERR("failed to decompress data");
		status = BLK_STS_IOERR;
		goto put_new_bio;
	}

	lz4e_reset_bio(new_bio, original_bio, lzdev->under_dev);

	ret = lz4e_add_buf_to_bio(new_bio, &chunk->src_buf);
	if (ret) {
		LZ4E_PR_ERR("failed to add src buffer to bio");
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
	lz4e_chunk_free(chunk);
	return status;
}

blk_status_t lz4e_req_init(struct lz4e_req *lzreq, struct bio *original_bio,
			   struct lz4e_dev *lzdev)
{
	enum req_op op_type = bio_op(original_bio);

	switch (op_type) {
	case REQ_OP_READ:
		return lz4e_read_req_init(lzreq, original_bio, lzdev);
	case REQ_OP_WRITE:
		return lz4e_write_req_init(lzreq, original_bio, lzdev);
	default:
		LZ4E_PR_ERR("unsupported request operation");
		return BLK_STS_NOTSUPP;
	}
}

static void lz4e_end_io(struct bio *new_bio)
{
	struct lz4e_req *lzreq = new_bio->bi_private;
	struct bio *original_bio = lzreq->original_bio;
	struct lz4e_stats *stats_to_update = lzreq->stats_to_update;

	lz4e_stats_update(stats_to_update, new_bio);

	LZ4E_PR_INFO("completed bio request");

	original_bio->bi_status = new_bio->bi_status;
	bio_endio(original_bio);

	bio_put(new_bio);
	lz4e_req_free(lzreq);
}

void lz4e_req_submit(struct lz4e_req *lzreq)
{
	struct bio *new_bio = lzreq->new_bio;

	new_bio->bi_end_io = lz4e_end_io;
	new_bio->bi_private = lzreq;

	submit_bio_noacct(new_bio);

	LZ4E_PR_DEBUG("submitted request to underlying device");
}
