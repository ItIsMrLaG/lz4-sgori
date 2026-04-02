// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_CHUNK_H
#define LZ4E_CHUNK_H

#include <linux/blk_types.h>
#include <linux/bvec.h>
#include <linux/types.h>

#include "lz4e_static.h"
#include "lz4e_under_dev.h"

typedef enum {
	LZ4E_COMP_CONT, /* compression on contiguous buffers */
	LZ4E_COMP_VECT, /* compression on each of bvecs */
	LZ4E_COMP_STRM, /* streamed compression on bvecs */
	LZ4E_COMP_EXTD, /* extended compression on scatter-gather buffers */
} lz4e_comp_t;

#define LZ4E_COMP_DEFAULT LZ4E_COMP_EXTD
#define LZ4E_COMP_TYPE_COUNT 4
#define LZ4E_COMP_STR_LEN 4

extern const lz4e_comp_t lz4e_comp_type[LZ4E_COMP_TYPE_COUNT];
extern const char *lz4e_comp_str[LZ4E_COMP_TYPE_COUNT];

typedef enum {
	LZ4E_READ,
	LZ4E_WRITE,
} lz4e_dir_t;

struct lz4e_chunk_operations {
	int (*init)(void *chunk_ptr, struct bio *src_bio, lz4e_dir_t data_dir);
	int (*run_comp)(void *chunk_ptr);
	int (*end)(void *chunk_ptr, struct bio *dst_bio, lz4e_dir_t data_dir);
	void (*free)(void *chunk_ptr);
} LZ4E_ALIGN_32;

/* generic chunk for compression */
typedef struct {
	void *internal;
	const struct lz4e_chunk_operations *ops;

	ktime_t copy_time;
	ktime_t comp_time;
	ktime_t decomp_time;
	ktime_t total_time;

	unsigned int comp_size;
	unsigned int decomp_size;
	unsigned int mem_usage;
	int acceleration;
} LZ4E_ALIGN_64 lz4e_chunk_t;

lz4e_chunk_t *lz4e_chunk_alloc(struct bio *orig_bio,
			       struct lz4e_under_dev *under_dev, gfp_t gfp_mask,
			       lz4e_comp_t comp_type);
int lz4e_chunk_init(lz4e_chunk_t *chunk, struct bio *src_bio,
		    lz4e_dir_t data_dir, int acceleration);
int lz4e_chunk_run_comp(lz4e_chunk_t *chunk);
int lz4e_chunk_end(lz4e_chunk_t *chunk, struct bio *dst_bio,
		   lz4e_dir_t data_dir);
void lz4e_chunk_free(lz4e_chunk_t *chunk);

ktime_t lz4e_chunk_start_timer(lz4e_chunk_t *chunk);
ktime_t lz4e_chunk_stop_timer(lz4e_chunk_t *chunk);

#endif
