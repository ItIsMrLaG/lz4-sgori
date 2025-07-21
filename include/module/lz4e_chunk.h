// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_CHUNK_H
#define LZ4E_CHUNK_H

#include <linux/blk_types.h>
#include <linux/lz4.h>

#include "lz4e_static.h"

// Struct representing a contiguous data in memory
struct lz4e_buffer {
	char *data;
	int data_size;
	int buf_size;
} LZ4E_ALIGN_16;

// Struct representing data to be compressed
struct lz4e_chunk {
	struct lz4e_buffer src_buf;
	struct lz4e_buffer dst_buf;
	void *wrkmem;
} LZ4E_ALIGN_64;

// Copy data from the given bio
void lz4e_buf_copy_from_bio(struct lz4e_buffer *dst, struct bio *src);

// Allocate chunk for compression
struct lz4e_chunk *lz4e_chunk_alloc(int src_size);

// Compress data from source buffer into destination buffer
int lz4e_chunk_compress(struct lz4e_chunk *chunk);

// Decompress data from destination buffer into source buffer
int lz4e_chunk_decompress(struct lz4e_chunk *chunk);

// Free chunk for compression
void lz4e_chunk_free(struct lz4e_chunk *chunk);

#endif
