// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#include <linux/bio.h>
#include <linux/blk_types.h>
#include <linux/bvec.h>
#include <linux/highmem.h>
#include <linux/ktime.h>
#include <linux/lz4.h>
#include <linux/minmax.h>
#include <linux/slab.h>
#include <linux/stddef.h>
#include <linux/timekeeping.h>
#include <linux/types.h>

#include "include/lz4e_chunk.h"

#include "include/lz4e.h"
#include "include/lz4e_chunk_internal.h"
#include "include/lz4e_static.h"
#include "include/lz4e_under_dev.h"

const lz4e_comp_t lz4e_comp_type[LZ4E_COMP_TYPE_COUNT] = {
	LZ4E_COMP_CONT,
	LZ4E_COMP_VECT,
	LZ4E_COMP_STRM,
	LZ4E_COMP_EXTD,
};
const char *lz4e_comp_str[LZ4E_COMP_TYPE_COUNT] = {
	"cont",
	"vect",
	"strm",
	"extd",
};

/* -------------------- helpers -------------------- */

static int lz4e_buf_copy_from_bio(struct lz4e_buffer *dst, struct bio *src)
{
	char *ptr = dst->data;
	struct bio_vec bvec;
	struct bvec_iter iter;

	bio_for_each_segment (bvec, src, iter) {
		memcpy_from_bvec(ptr, &bvec);
		ptr += bvec.bv_len;
	}

	LZ4E_PR_DEBUG("copied from bio to buffer");
	return 0;
}

static int lz4e_buf_copy_to_bio(struct bio *dst, struct lz4e_buffer *src)
{
	char *ptr = src->data;
	struct bio_vec bvec;
	struct bvec_iter iter;

	bio_for_each_segment (bvec, dst, iter) {
		memcpy_to_bvec(&bvec, ptr);
		ptr += bvec.bv_len;
	}

	LZ4E_PR_DEBUG("copied from buffer to bio");
	return 0;
}

static int lz4e_buf_add_to_bio(struct bio *bio, struct lz4e_buffer *buf)
{
	char *data = buf->data;
	unsigned int buf_len = buf->buf_size;
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

static int lz4e_bio_alloc_pages(struct bio *bio, unsigned int npages,
				unsigned int nvecs, gfp_t gfp_mask)
{
	unsigned int npages_allocd;
	struct page *first_page;
	int ipage;
	int ret;

	while (npages) {
		unsigned int order;

		order = order_base_2(DIV_ROUND_UP(npages, nvecs));
		npages_allocd = (1 << order);

		first_page = alloc_pages(gfp_mask, order);
		if (!first_page) {
			LZ4E_PR_ERR("failed to allocate pages: order %u",
				    order);
			ret = -ENOMEM;
			goto free_pages_bio;
		}

		for (ipage = 0; ipage < npages_allocd; ++ipage) {
			ret = bio_add_page(bio, first_page + ipage, PAGE_SIZE,
					   0);
			if (ret) {
				LZ4E_PR_ERR("failed to add page to bio");
				ret = -EAGAIN;
				goto free_pages_rem;
			}
		}

		npages -= min_t(unsigned int, npages_allocd, npages);
		nvecs--;
	}

	LZ4E_PR_DEBUG("allocated pages for bio");
	return 0;

free_pages_rem:
	for (; ipage < npages_allocd; ++ipage)
		__free_page(first_page + ipage);
free_pages_bio:
	bio_free_pages(bio);
	return ret;
}

static inline void lz4e_chunk_vect_map_srcs(struct lz4e_chunk_vect *chunk)
{
	struct bio_vec bvec;
	struct bvec_iter iter;
	int ibuf = 0;

	/* restore iter in case of read */
	chunk->src_bio->bi_iter = chunk->src_iter;

	bio_for_each_segment (bvec, chunk->src_bio, iter) {
		chunk->srcs[ibuf].data =
			kmap_local_page(bvec.bv_page) + bvec.bv_offset;
		ibuf++;
	}
}

static inline void lz4e_chunk_vect_unmap_srcs(struct lz4e_chunk_vect *chunk)
{
	for (int i = (int)chunk->buf_cnt - 1; i >= 0; --i) {
		kunmap_local(chunk->srcs[i].data);
		chunk->srcs[i].data = NULL;
	}
}

static inline int lz4e_chunk_vect_run_comp_generic(
	void *chunk_ptr,
	int (*compress)(void *wrkmem, struct lz4e_buffer *src,
			struct lz4e_buffer *dst, int acceleration),
	int (*decompress)(void *wrkmem, struct lz4e_buffer *src,
			  struct lz4e_buffer *dst))
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_vect *internal = chunk->internal;
	int ret;

	unsigned int comp_size = 0;
	unsigned int decomp_size = 0;
	ktime_t comp_time = ktime_set(0, 0);
	ktime_t decomp_time = ktime_set(0, 0);

	/* map buffers to compress from */
	lz4e_chunk_vect_map_srcs(internal);

	LZ4E_PR_INFO("vect: compressing %u bytes", internal->src_iter.bi_size);

	for (int i = 0; i < internal->buf_cnt; ++i) {
		struct lz4e_buffer *decomp_buf = &internal->srcs[i];
		struct lz4e_buffer *comp_buf = &internal->dsts[i];
		ktime_t duration;

		LZ4E_KTIME_WRAP(compress(internal->wrkmem, decomp_buf, comp_buf,
					 chunk->acceleration),
				duration, ret);
		if (!ret) {
			LZ4E_PR_ERR("vect: compression failed: returned %d",
				    ret);
			ret = -EIO;
			goto end;
		}

		comp_buf->data_size = ret;
		comp_size += ret;
		comp_time = ktime_add(comp_time, duration);
	}

	LZ4E_PR_INFO("vect: compressed data: %u bytes", comp_size);

	/* reset stream in case of streamed decompression */
	LZ4_setStreamDecode(internal->wrkmem, NULL, 0);

	for (int i = 0; i < internal->buf_cnt; ++i) {
		struct lz4e_buffer *comp_buf = &internal->dsts[i];
		struct lz4e_buffer *decomp_buf = &internal->srcs[i];
		ktime_t duration;

		LZ4E_KTIME_WRAP(decompress(internal->wrkmem, comp_buf,
					   decomp_buf),
				duration, ret);
		if (ret < 0 || ret != decomp_buf->data_size) {
			LZ4E_PR_ERR("vect: decompression failed: returned %d",
				    ret);
			ret = -EIO;
			goto end;
		}

		decomp_size += ret;
		decomp_time = ktime_add(decomp_time, duration);
	}

	LZ4E_PR_INFO("vect: decompressed data: %u bytes", decomp_size);

	chunk->comp_size = comp_size;
	chunk->decomp_size = decomp_size;
	chunk->comp_time = comp_time;
	chunk->decomp_time = decomp_time;

	LZ4E_PR_DEBUG("vect: completed compression");
	ret = 0;
end:
	/* finally unmap src buffers */
	lz4e_chunk_vect_unmap_srcs(internal);
	return ret;
}

// Callbacks can have unused parameters
// NOLINTBEGIN(misc-unused-parameters)

/* ---------------- for compression on contiguous buffers ---------------- */

static void lz4e_chunk_free_cont(void *chunk_ptr)
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_cont *internal = chunk->internal;

	kfree(internal->src.data);
	kfree(internal->dst.data);
	kfree(internal->wrkmem);

	kfree(internal);
	kfree(chunk);

	LZ4E_PR_DEBUG("cont: released chunk");
}

static struct lz4e_chunk_cont *
lz4e_chunk_alloc_cont(struct bio *orig_bio, struct lz4e_under_dev *under_dev,
		      gfp_t gfp_mask)
{
	struct lz4e_chunk_cont *internal;
	char *src_buf;
	char *dst_buf;
	void *wrkmem;

	unsigned int src_size = orig_bio->bi_iter.bi_size;
	unsigned int dst_size = LZ4_COMPRESSBOUND(src_size);

	internal = kzalloc(sizeof(*internal), gfp_mask);
	if (!internal) {
		LZ4E_PR_ERR("cont: failed to allocate chunk struct: %zu bytes",
			    sizeof(*internal));
		goto error;
	}

	src_buf = kzalloc(src_size, gfp_mask);
	if (!src_buf) {
		LZ4E_PR_ERR("cont: failed to allocate src buffer: %u bytes",
			    src_size);
		goto free_internal;
	}

	dst_buf = kzalloc(dst_size, gfp_mask);
	if (!dst_buf) {
		LZ4E_PR_ERR("cont: failed to allocate dst buffer: %u bytes",
			    dst_size);
		goto free_src;
	}

	wrkmem = kzalloc(LZ4_MEM_COMPRESS, gfp_mask);
	if (!wrkmem) {
		LZ4E_PR_ERR("cont: failed to allocate wrkmem: %zu bytes",
			    LZ4_MEM_COMPRESS);
		goto free_dst;
	}

	internal->src.data = src_buf;
	internal->src.buf_size = src_size;
	internal->src.data_size = src_size;

	internal->dst.data = dst_buf;
	internal->dst.buf_size = dst_size;

	internal->wrkmem = wrkmem;

	LZ4E_PR_DEBUG("cont: allocated chunk");
	return internal;

free_dst:
	kfree(dst_buf);
free_src:
	kfree(src_buf);
free_internal:
	kfree(internal);
error:
	return NULL;
}

static int lz4e_chunk_init_cont(void *chunk_ptr, struct bio *src_bio,
				lz4e_dir_t data_dir)
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_cont *internal = chunk->internal;
	ktime_t copy_time;
	int ret;

	switch (data_dir) {
	case LZ4E_READ:
		ret = lz4e_buf_add_to_bio(src_bio, &internal->src);
		if (ret) {
			LZ4E_PR_ERR("cont: failed to add buffer to bio");
			return ret;
		}
		copy_time = ktime_set(0, 0);
		break;
	case LZ4E_WRITE:
		LZ4E_KTIME_WRAP(lz4e_buf_copy_from_bio(&internal->src, src_bio),
				copy_time, ret);
		break;
	}

	chunk->mem_usage = internal->src.buf_size + internal->dst.buf_size +
			   LZ4_MEM_COMPRESS;
	chunk->copy_time = copy_time;

	LZ4E_PR_DEBUG("cont: initialized chunk");
	return 0;
}

static int lz4e_chunk_end_cont(void *chunk_ptr, struct bio *dst_bio,
			       lz4e_dir_t data_dir)
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_cont *internal = chunk->internal;
	ktime_t copy_time;
	int ret;

	switch (data_dir) {
	case LZ4E_READ:
		LZ4E_KTIME_WRAP(lz4e_buf_copy_to_bio(dst_bio, &internal->src),
				copy_time, ret);
		break;
	case LZ4E_WRITE:
		ret = lz4e_buf_add_to_bio(dst_bio, &internal->src);
		if (ret) {
			LZ4E_PR_ERR("cont: failed to add buffer to bio");
			return ret;
		}
		copy_time = ktime_set(0, 0);
		break;
	}

	chunk->copy_time = ktime_add(chunk->copy_time, copy_time);

	LZ4E_PR_DEBUG("cont: finished chunk");
	return 0;
}

static inline int lz4e_compress_cont(void *wrkmem, struct lz4e_buffer *src,
				     struct lz4e_buffer *dst, int acceleration)
{
	return LZ4_compress_fast(src->data, dst->data, (int)src->data_size,
				 (int)dst->buf_size, acceleration, wrkmem);
}

static inline int lz4e_decompress_cont(void *wrkmem, struct lz4e_buffer *src,
				       struct lz4e_buffer *dst)
{
	return LZ4_decompress_safe(src->data, dst->data, (int)src->data_size,
				   (int)dst->buf_size);
}

static int lz4e_chunk_run_comp_cont(void *chunk_ptr)
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_cont *internal = chunk->internal;
	int ret;

	unsigned int comp_size;
	unsigned int decomp_size;
	ktime_t comp_time;
	ktime_t decomp_time;

	LZ4E_PR_INFO("cont: compressing %u bytes", internal->src.data_size);

	LZ4E_KTIME_WRAP(lz4e_compress_cont(internal->wrkmem, &internal->src,
					   &internal->dst, chunk->acceleration),
			comp_time, ret);
	if (!ret) {
		LZ4E_PR_ERR("cont: compression failed: returned %d", ret);
		return -EIO;
	}

	comp_size = ret;
	internal->dst.data_size = comp_size;
	LZ4E_PR_INFO("cont: compressed data: %u", comp_size);

	LZ4E_KTIME_WRAP(lz4e_decompress_cont(internal->wrkmem, &internal->dst,
					     &internal->src),
			decomp_time, ret);
	if (ret < 0 || ret != internal->src.data_size) {
		LZ4E_PR_ERR("cont: decompression failed: returned %d", ret);
		return -EIO;
	}

	decomp_size = ret;
	LZ4E_PR_INFO("cont: decompressed data: %u", decomp_size);

	chunk->comp_size = comp_size;
	chunk->decomp_size = decomp_size;
	chunk->comp_time = comp_time;
	chunk->decomp_time = decomp_time;

	LZ4E_PR_DEBUG("cont: completed compression");
	return 0;
}

/* ---------------------- for vectored compression ----------------------- */

static void lz4e_chunk_free_vect(void *chunk_ptr)
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_vect *internal = chunk->internal;

	/* free dst buffers */
	for (int i = 0; i < internal->buf_cnt; ++i)
		kfree(internal->dsts[i].data);

	kfree(internal->srcs);
	kfree(internal->dsts);
	kfree(internal->wrkmem);

	kfree(internal);
	kfree(chunk);

	LZ4E_PR_DEBUG("vect: released chunk");
}

static struct lz4e_chunk_vect *
lz4e_chunk_alloc_vect(struct bio *orig_bio, struct lz4e_under_dev *under_dev,
		      gfp_t gfp_mask)
{
	struct lz4e_chunk_vect *internal;
	struct lz4e_buffer *srcs;
	struct lz4e_buffer *dsts;
	void *wrkmem;
	int ibuf;

	unsigned int buf_cnt = bio_segments(orig_bio);

	internal = kzalloc(sizeof(*internal), gfp_mask);
	if (!internal) {
		LZ4E_PR_ERR("vect: failed to allocate chunk struct: %zu bytes",
			    sizeof(*internal));
		goto error;
	}

	srcs = kzalloc(LZ4E_MEM_VECT(buf_cnt), gfp_mask);
	if (!srcs) {
		LZ4E_PR_ERR("vect: failed to allocate srcs vector: %zu bytes",
			    LZ4E_MEM_VECT(buf_cnt));
		goto free_internal;
	}

	dsts = kzalloc(LZ4E_MEM_VECT(buf_cnt), gfp_mask);
	if (!dsts) {
		LZ4E_PR_ERR("vect: failed to allocate dsts vector: %zu bytes",
			    LZ4E_MEM_VECT(buf_cnt));
		goto free_srcs;
	}

	wrkmem = kzalloc(LZ4_MEM_COMPRESS, gfp_mask);
	if (!wrkmem) {
		LZ4E_PR_ERR("vect: failed to allocate wrkmem: %zu bytes",
			    LZ4_MEM_COMPRESS);
		goto free_dsts;
	}

	internal->buf_cnt = buf_cnt;
	internal->srcs = srcs;
	internal->dsts = dsts;
	internal->wrkmem = wrkmem;

	/* allocate dst buffers */
	{
		struct bio_vec bvec;
		struct bvec_iter iter;

		internal->dst_size = 0;
		ibuf = 0;

		bio_for_each_segment (bvec, orig_bio, iter) {
			size_t src_size = bvec.bv_len;
			size_t dst_size = LZ4_COMPRESSBOUND(src_size);
			char *dst_data;

			dst_data = kzalloc(dst_size, gfp_mask);
			if (!dst_data) {
				LZ4E_PR_ERR(
					"vect: failed to allocate dst buffer: %zu bytes",
					dst_size);
				goto free_dst_bufs;
			}

			internal->srcs[ibuf].buf_size = src_size;
			internal->srcs[ibuf].data_size = src_size;
			internal->dsts[ibuf].data = dst_data;
			internal->dsts[ibuf].buf_size = dst_size;
			internal->dst_size += dst_size;

			ibuf++;
		}
	}

	LZ4E_PR_DEBUG("vect: allocated chunk");
	return internal;

free_dst_bufs:
	for (ibuf--; ibuf >= 0; --ibuf)
		kfree(internal->dsts[ibuf].data);
	kfree(wrkmem);
free_dsts:
	kfree(dsts);
free_srcs:
	kfree(srcs);
free_internal:
	kfree(internal);
error:
	return NULL;
}

static int lz4e_chunk_init_vect(void *chunk_ptr, struct bio *src_bio,
				lz4e_dir_t data_dir)
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_vect *internal = chunk->internal;

	internal->src_bio = src_bio;
	/* save initial iter in case of read */
	internal->src_iter = src_bio->bi_iter;

	chunk->mem_usage = internal->dst_size + LZ4_MEM_COMPRESS;
	chunk->copy_time = ktime_set(0, 0);

	LZ4E_PR_DEBUG("vect: initialized chunk");
	return 0;
}

static int lz4e_chunk_end_vect(void *chunk_ptr, struct bio *dst_bio,
			       lz4e_dir_t data_dir)
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_vect *internal = chunk->internal;

	internal->src_bio = NULL;

	LZ4E_PR_DEBUG("vect: finished chunk");
	return 0;
}

static int lz4e_chunk_run_comp_vect(void *chunk_ptr)
{
	return lz4e_chunk_vect_run_comp_generic(chunk_ptr, lz4e_compress_cont,
						lz4e_decompress_cont);
}

/* --------------- for vectored compression (using stream) --------------- */

static inline int lz4e_compress_strm(void *wrkmem, struct lz4e_buffer *src,
				     struct lz4e_buffer *dst, int acceleration)
{
	return LZ4_compress_fast_continue(wrkmem, src->data, dst->data,
					  (int)src->data_size,
					  (int)dst->buf_size, acceleration);
}

static inline int lz4e_decompress_strm(void *wrkmem, struct lz4e_buffer *src,
				       struct lz4e_buffer *dst)
{
	return LZ4_decompress_safe_continue(wrkmem, src->data, dst->data,
					    (int)src->data_size,
					    (int)dst->buf_size);
}

static int lz4e_chunk_run_comp_strm(void *chunk_ptr)
{
	return lz4e_chunk_vect_run_comp_generic(chunk_ptr, lz4e_compress_strm,
						lz4e_decompress_strm);
}

/* -------------- for compression on scatter-gather buffers -------------- */

static void lz4e_chunk_free_extd(void *chunk_ptr)
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_extd *internal = chunk->internal;

	bio_put(internal->dst_bio);

	kfree(internal->src_buf.data);
	kfree(internal->dst_buf.data);
	kfree(internal->wrkmem);

	kfree(internal);
	kfree(chunk);

	LZ4E_PR_DEBUG("extd: released chunk");
}

static struct lz4e_chunk_extd *
lz4e_chunk_alloc_extd(struct bio *orig_bio, struct lz4e_under_dev *under_dev,
		      gfp_t gfp_mask)
{
	struct lz4e_chunk_extd *internal;
	char *src_data;
	char *dst_data;
	void *wrkmem;
	struct bio *dst_bio;

	unsigned int src_size = orig_bio->bi_iter.bi_size;
	unsigned int dst_size = LZ4E_COMPRESSBOUND(src_size);
	unsigned int npages = (dst_size >> PAGE_SHIFT) + 1;
	unsigned short nvecs = min_t(unsigned int, npages, BIO_MAX_VECS);

	internal = kzalloc(sizeof(*internal), gfp_mask);
	if (!internal) {
		LZ4E_PR_ERR("extd: failed to allocate chunk struct: %zu bytes",
			    sizeof(*internal));
		goto error;
	}

	src_data = kzalloc(src_size, gfp_mask);
	if (!src_data) {
		LZ4E_PR_ERR("extd: failed to allocate src buffer: %u bytes",
			    src_size);
		goto free_internal;
	}

	dst_data = kzalloc(dst_size, gfp_mask);
	if (!dst_data) {
		LZ4E_PR_ERR("extd: failed to allocate dst buffer: %u bytes",
			    src_size);
		goto free_src_data;
	}

	wrkmem = kzalloc(LZ4E_MEM_COMPRESS, gfp_mask);
	if (!wrkmem) {
		LZ4E_PR_ERR("extd: failed to allocate wrkmem: %zu bytes",
			    LZ4E_MEM_COMPRESS);
		goto free_dst_data;
	}

	dst_bio = bio_alloc_bioset(under_dev->bdev, nvecs, orig_bio->bi_opf,
				   gfp_mask, under_dev->bset);
	if (!dst_bio) {
		LZ4E_PR_ERR("failed to allocate dst bio");
		goto free_wrkmem;
	}

	internal->src_buf.data = src_data;
	internal->src_buf.buf_size = src_size;
	internal->src_buf.data_size = src_size;

	internal->dst_buf.data = dst_data;
	internal->dst_buf.buf_size = dst_size;
	internal->dst_bio = dst_bio;

	internal->wrkmem = wrkmem;

	LZ4E_PR_DEBUG("extd: allocated chunk");
	return internal;

free_wrkmem:
	kfree(wrkmem);
free_dst_data:
	kfree(dst_data);
free_src_data:
	kfree(src_data);
free_internal:
	kfree(internal);
error:
	return NULL;
}

static int lz4e_chunk_init_extd(void *chunk_ptr, struct bio *src_bio,
				lz4e_dir_t data_dir)
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_extd *internal = chunk->internal;
	int ret;

	internal->src_bio = src_bio;
	/* save initial iter in case of read */
	internal->src_iter = src_bio->bi_iter;

	ret = lz4e_buf_add_to_bio(internal->dst_bio, &internal->dst_buf);
	if (ret) {
		LZ4E_PR_ERR("extd: failed to add buffer to bio");
		return ret;
	}

	chunk->mem_usage = internal->dst_buf.buf_size + LZ4E_MEM_COMPRESS;
	chunk->copy_time = ktime_set(0, 0);

	LZ4E_PR_DEBUG("extd: initialized chunk");
	return 0;
}

static int lz4e_chunk_end_extd(void *chunk_ptr, struct bio *dst_bio,
			       lz4e_dir_t data_dir)
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_extd *internal = chunk->internal;

	/* will remove copying when decompression is done ... */
	lz4e_buf_copy_to_bio(dst_bio, &internal->src_buf);

	LZ4E_PR_DEBUG("extd: finished chunk");
	return 0;
}

static int lz4e_chunk_run_comp_extd(void *chunk_ptr)
{
	lz4e_chunk_t *chunk = (lz4e_chunk_t *)chunk_ptr;
	struct lz4e_chunk_extd *internal = chunk->internal;
	struct bvec_iter src_iter;
	struct bvec_iter dst_iter;
	int ret;

	unsigned int comp_size;
	unsigned int decomp_size;
	ktime_t comp_time;
	ktime_t decomp_time;

	src_iter = internal->src_iter;
	dst_iter = internal->dst_bio->bi_iter;

	LZ4E_PR_INFO("extd: compressing %u bytes", src_iter.bi_size);

	LZ4E_KTIME_WRAP(LZ4E_compress_fast(internal->src_bio->bi_io_vec,
					   internal->dst_bio->bi_io_vec,
					   &src_iter, &dst_iter,
					   chunk->acceleration,
					   internal->wrkmem),
			comp_time, ret);
	if (!ret) {
		LZ4E_PR_ERR("extd: compression failed: returned %d", ret);
		return -EIO;
	}

	comp_size = ret;
	internal->dst_buf.data_size = comp_size;
	LZ4E_PR_INFO("extd: compressed data: %u bytes", comp_size);

	LZ4E_KTIME_WRAP(lz4e_decompress_cont(internal->wrkmem,
					     &internal->dst_buf,
					     &internal->src_buf),
			decomp_time, ret);
	if (ret < 0 || ret != internal->src_buf.data_size) {
		LZ4E_PR_ERR("extd: decompression failed: returned %d", ret);
		return -EIO;
	}

	decomp_size = ret;
	LZ4E_PR_INFO("extd: decompressed data: %u bytes", decomp_size);

	chunk->comp_size = comp_size;
	chunk->decomp_size = decomp_size;
	chunk->comp_time = comp_time;
	chunk->decomp_time = decomp_time;

	LZ4E_PR_DEBUG("extd: completed compression");
	return 0;
}

// Callbacks can have unused parameters
// NOLINTEND(misc-unused-parameters)

/* -------------------- generic chunk -------------------- */

static const struct lz4e_chunk_operations lz4e_chunk_cont_ops = {
	.init = lz4e_chunk_init_cont,
	.run_comp = lz4e_chunk_run_comp_cont,
	.end = lz4e_chunk_end_cont,
	.free = lz4e_chunk_free_cont,
};

static const struct lz4e_chunk_operations lz4e_chunk_vect_ops = {
	.init = lz4e_chunk_init_vect,
	.run_comp = lz4e_chunk_run_comp_vect,
	.end = lz4e_chunk_end_vect,
	.free = lz4e_chunk_free_vect,
};

static const struct lz4e_chunk_operations lz4e_chunk_strm_ops = {
	.init = lz4e_chunk_init_vect,
	.run_comp = lz4e_chunk_run_comp_strm,
	.end = lz4e_chunk_end_vect,
	.free = lz4e_chunk_free_vect,
};

static const struct lz4e_chunk_operations lz4e_chunk_extd_ops = {
	.init = lz4e_chunk_init_extd,
	.run_comp = lz4e_chunk_run_comp_extd,
	.end = lz4e_chunk_end_extd,
	.free = lz4e_chunk_free_extd,
};

lz4e_chunk_t *lz4e_chunk_alloc(struct bio *orig_bio,
			       struct lz4e_under_dev *under_dev, gfp_t gfp_mask,
			       lz4e_comp_t comp_type)
{
	lz4e_chunk_t *chunk;

	chunk = kzalloc(sizeof(*chunk), gfp_mask);
	if (!chunk) {
		LZ4E_PR_ERR("failed to allocate chunk struct: %zu bytes",
			    sizeof(*chunk));
		goto error;
	}

	switch (comp_type) {
	case LZ4E_COMP_CONT:
		chunk->ops = &lz4e_chunk_cont_ops;
		chunk->internal =
			lz4e_chunk_alloc_cont(orig_bio, under_dev, gfp_mask);
		break;
	case LZ4E_COMP_VECT:
		chunk->ops = &lz4e_chunk_vect_ops;
		chunk->internal =
			lz4e_chunk_alloc_vect(orig_bio, under_dev, gfp_mask);
		break;
	case LZ4E_COMP_STRM:
		chunk->ops = &lz4e_chunk_strm_ops;
		chunk->internal =
			lz4e_chunk_alloc_vect(orig_bio, under_dev, gfp_mask);
		break;
	case LZ4E_COMP_EXTD:
		chunk->ops = &lz4e_chunk_extd_ops;
		chunk->internal =
			lz4e_chunk_alloc_extd(orig_bio, under_dev, gfp_mask);
		break;
	}

	if (!chunk->internal) {
		LZ4E_PR_ERR("failed to allocate internal chunk");
		goto free_chunk;
	}

	LZ4E_PR_DEBUG("allocated chunk");
	return chunk;

free_chunk:
	kfree(chunk);
error:
	return NULL;
}

inline int lz4e_chunk_init(lz4e_chunk_t *chunk, struct bio *src_bio,
			   lz4e_dir_t data_dir, int acceleration)
{
	chunk->acceleration = acceleration;
	return chunk->ops->init(chunk, src_bio, data_dir);
}

inline int lz4e_chunk_run_comp(lz4e_chunk_t *chunk)
{
	return chunk->ops->run_comp(chunk);
}

inline int lz4e_chunk_end(lz4e_chunk_t *chunk, struct bio *dst_bio,
			  lz4e_dir_t data_dir)
{
	return chunk->ops->end(chunk, dst_bio, data_dir);
}

inline void lz4e_chunk_free(lz4e_chunk_t *chunk)
{
	if (!chunk)
		return;

	chunk->ops->free(chunk);
}

inline ktime_t lz4e_chunk_start_timer(lz4e_chunk_t *chunk)
{
	chunk->total_time = ktime_get();
	return chunk->total_time;
}

inline ktime_t lz4e_chunk_stop_timer(lz4e_chunk_t *chunk)
{
	ktime_t end_time = ktime_get();
	ktime_t start_time = chunk->total_time;
	chunk->total_time = ktime_sub(end_time, start_time);
	return chunk->total_time;
}
