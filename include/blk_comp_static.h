// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2025 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef BLK_COMP_STATIC_H
#define BLK_COMP_STATIC_H

#include <linux/printk.h>

#define BLK_COMP_MODULE_NAME "blk_comp"
#define BLK_COMP_DEVICE_NAME "blk-comp-dev"

#define BLK_COMP_MAJOR	     0
#define BLK_COMP_FIRST_MINOR 0

// Bio set pool size to use
#define BLK_COMP_BIOSET_SIZE 1024

// Struct memory alignment attributes
#define BLK_COMP_ALIGN_16 __attribute__((packed, aligned(16)))
#define BLK_COMP_ALIGN_32 __attribute__((packed, aligned(32)))

// Print formatted error to logs
#define BLK_COMP_PR_ERR(fmt, ...) \
	pr_err("%s: " fmt "\n", BLK_COMP_MODULE_NAME, ##__VA_ARGS__)

// Print formatted info to logs
#define BLK_COMP_PR_INFO(fmt, ...) \
	pr_info("%s: " fmt "\n", BLK_COMP_MODULE_NAME, ##__VA_ARGS__)

// Print formatted debug info to logs
#define BLK_COMP_PR_DEBUG(fmt, ...) \
	pr_debug("%s: " fmt "\n", BLK_COMP_MODULE_NAME, ##__VA_ARGS__)

// Format string for request statistics
#define BLK_COMP_STATS_FORMAT \
	"\
read:\n\
	reqs_total: %lld\n\
	reqs_failed: %lld\n\
	vec_count: %lld\n\
	data_in_bytes: %lld\n\
write:\n\
	reqs_total: %lld\n\
	reqs_failed: %lld\n\
	vec_count: %lld\n\
	data_in_bytes: %lld\n\
all:\n\
	reqs_total: %lld\n\
	reqs_failed: %lld\n\
	vec_count: %lld\n\
	data_in_bytes: %lld\n\
"

#endif
