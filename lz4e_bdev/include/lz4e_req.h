// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_REQ_H
#define LZ4E_REQ_H

#include <linux/blk_types.h>
#include <linux/types.h>

#include "lz4e_chunk.h"
#include "lz4e_dev.h"
#include "lz4e_static.h"
#include "lz4e_stats.h"
#include "lz4e_under_dev.h"

/* request to the underlying device */
struct lz4e_req {
	struct bio *orig_bio;
	struct bio *new_bio;
	struct lz4e_stats *stats_to_update;
	lz4e_chunk_t *chunk;
} LZ4E_ALIGN_32;

/* allocate request */
struct lz4e_req *lz4e_req_alloc(struct bio *orig_bio,
				struct lz4e_under_dev *under_dev,
				gfp_t gfp_mask, lz4e_comp_t comp_type);

/* initialize request to device with given bio */
blk_status_t lz4e_req_init(struct lz4e_req *lzreq, struct bio *orig_bio,
			   struct lz4e_dev *lzdev);

/* submit request to underlying device */
void lz4e_req_submit(struct lz4e_req *lzreq);

/* free request */
void lz4e_req_free(struct lz4e_req *lzreq);

#endif
