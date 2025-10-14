// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <asm-generic/bug.h>
#include <asm-generic/errno-base.h>
#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/bvec.h>
#include <linux/lz4.h>
#include <linux/slab.h>

#include "include/module/lz4e_chunk.h"

#include "include/lz4e/lz4e.h"
#include "include/module/lz4e_static.h"

void lz4e_buf_copy_from_bio(struct lz4e_buffer *dst, struct bio *src)
{
	char *ptr = dst->data;
	struct bio_vec bvec;
	struct bvec_iter iter;

	bio_for_each_segment (bvec, src, iter) {
		memcpy_from_bvec(ptr, &bvec);
		ptr += bvec.bv_len;
	}

	dst->data_size = (int)src->bi_iter.bi_size;

	LZ4E_PR_DEBUG("copied from bio to src buffer");
}

void lz4e_buf_copy_to_bio(struct bio *original_bio, struct lz4e_buffer *dst)
{
	size_t offset = 0;
	int iter = 0;

	while (iter < original_bio->bi_vcnt && offset < dst->data_size) {
		struct bio_vec *bvec = &original_bio->bi_io_vec[iter];
		size_t copy_len = bvec->bv_len;

		if (offset + copy_len > dst->data_size) {
			copy_len = dst->data_size - offset;
		}

		void *dst_ptr = page_address(bvec->bv_page) + bvec->bv_offset;

		memcpy(dst_ptr, &dst->data[offset], copy_len);

		offset += copy_len;
		iter++;
	}

	LZ4E_PR_DEBUG("copied from dst buffer to original_bio");
}

void lz4e_chunk_free(struct lz4e_chunk *chunk)
{
	if (!chunk)
		return;

	kfree(chunk->src_buf.data);
	kfree(chunk->dst_buf.data);
	kfree(chunk->wrkmem);

	kfree(chunk);

	LZ4E_PR_DEBUG("released chunk");
}

static inline void lz4e_buffer_init(struct lz4e_buffer *buf, char *data,
				    int buf_size)
{
	buf->data = data;
	buf->buf_size = buf_size;
}

struct lz4e_chunk *lz4e_chunk_alloc(int src_size)
{
	int dst_size = LZ4_COMPRESSBOUND(src_size);
	char *src_data;
	char *dst_data;
	void *wrkmem;
	struct lz4e_chunk *chunk;

	chunk = kzalloc(sizeof(*chunk), GFP_NOIO);
	if (!chunk) {
		LZ4E_PR_ERR("failed to allocate chunk context");
		return NULL;
	}

	src_data = kzalloc((size_t)src_size, GFP_NOIO);
	lz4e_buffer_init(&chunk->src_buf, src_data, src_size);
	if (!src_data) {
		LZ4E_PR_ERR("failed to allocate src buffer");
		goto free_chunk;
	}

	dst_data = kzalloc((size_t)dst_size, GFP_NOIO);
	lz4e_buffer_init(&chunk->dst_buf, dst_data, dst_size);
	if (!dst_data) {
		LZ4E_PR_ERR("failed to allocate dst buffer");
		goto free_chunk;
	}

	wrkmem = kzalloc(LZ4E_MEM_COMPRESS, GFP_NOIO);
	chunk->wrkmem = wrkmem;
	if (!wrkmem) {
		LZ4E_PR_ERR("failed to allocate working memory");
		goto free_chunk;
	}

	LZ4E_PR_DEBUG("allocated chunk");
	return chunk;

free_chunk:
	lz4e_chunk_free(chunk);
	return NULL;
}

int lz4e_chunk_compress(struct lz4e_chunk *chunk)
{
	struct lz4e_buffer src_buf = chunk->src_buf;
	struct lz4e_buffer dst_buf = chunk->dst_buf;
	void *wrkmem = chunk->wrkmem;
	int ret;

	ret = LZ4_compress_default(src_buf.data, dst_buf.data,
				   src_buf.data_size, dst_buf.buf_size, wrkmem);
	if (!ret) {
		LZ4E_PR_ERR("failed to compress data");
		return -EIO;
	}

	chunk->dst_buf.data_size = ret;

	LZ4E_PR_INFO("compressed data into dst buffer: %d bytes", ret);
	return 0;
}

int lz4e_chunk_decompress(struct lz4e_chunk *chunk)
{
	struct lz4e_buffer src_buf = chunk->src_buf;
	struct lz4e_buffer dst_buf = chunk->dst_buf;
	int ret;

	ret = LZ4_decompress_safe(dst_buf.data, src_buf.data, dst_buf.data_size,
				  src_buf.buf_size);
	if (ret < 0) {
		LZ4E_PR_ERR("failed to decompress data");
		return -EIO;
	}

	BUG_ON(ret != src_buf.buf_size);
	chunk->src_buf.data_size = ret;

	LZ4E_PR_INFO("decompressed data into src buffer: %d bytes", ret);
	return 0;
}

int lz4e_chunk_compress_ext(struct lz4e_chunk *chunk)
{
	struct bio *src_bio = chunk->src_buf.bio;
	struct bio *dst_bio = chunk->dst_buf.bio;
	struct bvec_iter src_iter = src_bio->bi_iter;
	struct bvec_iter dst_iter = dst_bio->bi_iter;
	void *wrkmem = chunk->wrkmem;
	int ret;

	ret = LZ4E_compress_default(src_bio->bi_io_vec, dst_bio->bi_io_vec,
				    &src_iter, &dst_iter, wrkmem);
	if (!ret) {
		LZ4E_PR_ERR("failed to compress data");
		return -EIO;
	}

	chunk->dst_buf.data_size = ret;

	LZ4E_PR_INFO("compressed data into dst buffer: %d bytes", ret);
	return 0;
}
