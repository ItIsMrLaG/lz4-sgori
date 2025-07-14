// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef BLK_COMP_MODULE_H
#define BLK_COMP_MODULE_H

#include "blk_comp_dev.h"
#include "blk_comp_static.h"

// Struct representing the block device module
struct blk_comp {
	int major;
	struct blk_comp_dev *bcdev;
} BLK_COMP_ALIGN_16;

#endif
