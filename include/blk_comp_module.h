// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef BLK_COMP_MODULE
#define BLK_COMP_MODULE

#include <linux/printk.h>

#include "blk_comp_dev.h"

#define BLK_COMP_MODULE_NAME "blk_comp"
#define BLK_COMP_DEVICE_NAME "blk-comp-dev"

#define BLK_COMP_MAJOR	     0
#define BLK_COMP_FIRST_MINOR 0

#define BLK_COMP_PR_ERR(fmt, ...) \
	pr_err("%s: " fmt "\n", BLK_COMP_MODULE_NAME, ##__VA_ARGS__)

#define BLK_COMP_PR_INFO(fmt, ...) \
	pr_info("%s: " fmt "\n", BLK_COMP_MODULE_NAME, ##__VA_ARGS__)

#define BLK_COMP_PR_DEBUG(fmt, ...) \
	pr_debug("%s: " fmt "\n", BLK_COMP_MODULE_NAME, ##__VA_ARGS__)

// Struct representing the block device module
struct blk_comp {
	int		     major;
	struct blk_comp_dev *bcdev;
};

#endif
