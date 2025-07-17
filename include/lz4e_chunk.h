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
struct LZ4E_buffer {
	char *data;
	int data_size;
	int buf_size;
} LZ4E_ALIGN_16;

// Struct representing data to be compressed
struct LZ4E_chunk {
	struct LZ4E_buffer src_buf;
	struct LZ4E_buffer dst_buf;
	void *wrkmem;
} LZ4E_ALIGN_64;

// Copy data from the given bio
void LZ4E_buf_copy_from_bio(struct LZ4E_buffer *dst, struct bio *src);

// Allocate chunk for compression
struct LZ4E_chunk *LZ4E_chunk_alloc(int src_size);

// Compress data from source buffer into destination buffer
int LZ4E_chunk_compress(struct LZ4E_chunk *chunk);

// Decompress data from destination buffer into source buffer
int LZ4E_chunk_decompress(struct LZ4E_chunk *chunk);

// Free chunk for compression
void LZ4E_chunk_free(struct LZ4E_chunk *chunk);

#endif
