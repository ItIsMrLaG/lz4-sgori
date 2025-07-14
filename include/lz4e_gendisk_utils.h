// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_GENDISK_UTILS_H
#define LZ4E_GENDISK_UTILS_H

#include <linux/blkdev.h>

#include "lz4e_dev.h"

// Allocate generic disk context
struct gendisk *LZ4E_gendisk_alloc(void);

// Add generic disk
int LZ4E_gendisk_add(struct gendisk *disk, struct LZ4E_dev *lzdev, int major,
		     int first_minor);

// Free generic disk context
void LZ4E_gendisk_free(struct gendisk *disk);

#endif
