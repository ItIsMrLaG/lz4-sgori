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
struct LZ4E_module {
	struct LZ4E_dev *bcdev;
	int major;
} LZ4E_ALIGN_16;

#endif
