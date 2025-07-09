// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef GENDISK_UTILS
#define GENDISK_UTILS

#include "blk_comp_dev.h"

// Allocate generic disk context
int gendisk_alloc(struct gendisk **disk_ptr);

// Initialize generic disk
int gendisk_init(struct gendisk *disk, struct blk_comp_dev *bcdev, int major, int first_minor);

// Free generic disk context
void gendisk_free(struct gendisk **disk_ptr);

#endif
