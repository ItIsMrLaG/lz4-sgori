// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef BLK_COMP_MODULE
#define BLK_COMP_MODULE

#include "blk_comp_dev.h"

// Struct representing the block device module
struct blk_comp {
	int major;
	struct blk_comp_dev *bcdev;
};

#endif
