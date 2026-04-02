// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_STATS_H
#define LZ4E_STATS_H

#include <linux/blk_types.h>
#include <linux/types.h>

#include "lz4e_chunk.h"
#include "lz4e_static.h"

/* request statistics of a disk for one of the operations */
struct lz4e_stats {
	atomic64_t reqs_total;	/* how many reqs submitted */
	atomic64_t reqs_failed; /* how many reqs failed */

	atomic64_t min_vec; /* min size of multi-page vec */
	atomic64_t max_vec; /* max size of multi-page vec */
	atomic64_t vecs;    /* number of multi-page vecs */

	atomic64_t segments;	/* number of single-page segments */
	atomic64_t decomp_size; /* size of decompressed data */
	atomic64_t comp_size;	/* size of compressed data */
	atomic64_t mem_usage;	/* memory usage in bytes */

	atomic64_t copy_ns;   /* time for data copying */
	atomic64_t comp_ns;   /* time for compression */
	atomic64_t decomp_ns; /* time for decompression */
	atomic64_t total_ns;  /* total elapsed time */
} LZ4E_ALIGN_128;

/* allocate request statistics */
struct lz4e_stats *lz4e_stats_alloc(gfp_t gfp_mask);

/* update statistics using given bio and chunk */
void lz4e_stats_update(struct lz4e_stats *lzstats, struct bio *bio,
		       lz4e_chunk_t *chunk);

/* reset request statistics */
void lz4e_stats_reset(struct lz4e_stats *lzstats);

/* free request statistics */
void lz4e_stats_free(struct lz4e_stats *lzstats);

#endif
