// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_MODULE_H
#define LZ4E_MODULE_H

#include "lz4e_dev.h"
#include "lz4e_static.h"

// Struct representing the block device module
struct lz4e_module {
	int major;
	struct lz4e_dev *lzdev;
} LZ4E_ALIGN_16;

#endif
