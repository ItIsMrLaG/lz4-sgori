// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 Alexander Bugaev
 *
 * This file is released under the GPL.
 */

#ifndef LZ4E_STATIC_H
#define LZ4E_STATIC_H

#include <linux/printk.h>

#define LZ4E_MODULE_NAME "lz4e"
#define LZ4E_DEVICE_NAME "lz4e-dev"

#define LZ4E_MAJOR 0
#define LZ4E_FIRST_MINOR 0

/* bio set pool size to use */
#define LZ4E_BIOSET_SIZE 1024

/* struct memory alignment attributes */
#define LZ4E_ALIGN_16 __attribute__((packed, aligned(16)))
#define LZ4E_ALIGN_32 __attribute__((packed, aligned(32)))
#define LZ4E_ALIGN_64 __attribute__((packed, aligned(64)))
#define LZ4E_ALIGN_128 __attribute__((packed, aligned(128)))

/* print formatted error to logs */
#define LZ4E_PR_ERR(fmt, ...) \
	pr_err("%s: " fmt "\n", LZ4E_MODULE_NAME, ##__VA_ARGS__)

/* print formatted warning to logs */
#define LZ4E_PR_WARN(fmt, ...) \
	pr_warn("%s: " fmt "\n", LZ4E_MODULE_NAME, ##__VA_ARGS__)

/* print formatted info to logs */
#define LZ4E_PR_INFO(fmt, ...) \
	pr_info("%s: " fmt "\n", LZ4E_MODULE_NAME, ##__VA_ARGS__)

/* print formatted debug info to logs */
#define LZ4E_PR_DEBUG(fmt, ...) \
	pr_debug("%s: " fmt "\n", LZ4E_MODULE_NAME, ##__VA_ARGS__)

#endif
