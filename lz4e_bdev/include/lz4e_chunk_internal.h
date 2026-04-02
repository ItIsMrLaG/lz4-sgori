// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_CHUNK_INTERNAL_H
#define LZ4E_CHUNK_INTERNAL_H

#include <linux/blk_types.h>
#include <linux/ktime.h>
#include <linux/preempt.h>
#include <linux/timekeeping.h>
#include <linux/types.h>

#include "lz4e_static.h"

/* contiguous buffer for compression */
struct lz4e_buffer {
	char *data;
	unsigned int buf_size;
	unsigned int data_size;
} LZ4E_ALIGN_16;

/* chunk for compression using contiguous buffers */
struct lz4e_chunk_cont {
	struct lz4e_buffer src;
	struct lz4e_buffer dst;
	void *wrkmem;
} LZ4E_ALIGN_64;

/* chunk for vectored compression */
struct lz4e_chunk_vect {
	struct bio *src_bio;
	struct lz4e_buffer *srcs;
	struct lz4e_buffer *dsts;
	void *wrkmem;
	struct bvec_iter src_iter;
	unsigned int dst_size;
	unsigned int buf_cnt;
} LZ4E_ALIGN_64;

/* chunk for compression using scatter-gather buffers */
struct lz4e_chunk_extd {
	struct bio *src_bio;
	struct bio *dst_bio;
	struct lz4e_buffer src_buf;
	struct lz4e_buffer dst_buf;
	void *wrkmem;
	struct bvec_iter src_iter;
} LZ4E_ALIGN_128;

#define LZ4E_MEM_VECT(buf_cnt) ((buf_cnt) * sizeof(struct lz4e_buffer))

/* wrapper for measuring time intervals */
#define LZ4E_KTIME_WRAP(func, duration, ret)        \
	do {                                        \
		ktime_t start;                      \
		ktime_t end;                        \
                                                    \
		preempt_disable();                  \
		start = ktime_get();                \
		(ret) = (func);                     \
		end = ktime_get();                  \
		preempt_enable();                   \
                                                    \
		(duration) = ktime_sub(end, start); \
	} while (0)

#endif
