/* @lint-ignore-every TXT2 Tab Literal */
/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */
#ifndef _DIRECT_VPP_DEBUG_H
#define _DIRECT_VPP_DEBUG_H

#include <linux/kern_levels.h>

extern uint dvpp_log_level;
extern bool dvpp_dyn_debug;
#define dvpp_do_log(level, fmt, arg...)                                        \
	do {                                                                   \
		if (level <= dvpp_log_level)                                   \
			printk(fmt, ##arg);                                    \
	} while (0)

#define dvpp_log_txrx(fmt, arg...)                                            \
	do {                                                                  \
		if (dvpp_dyn_debug) {                                          \
			dvpp_do_log(LOGLEVEL_DEBUG, fmt, ##arg);                       \
		}                                                 \
	} while (0)

#define dvpp_log_debug(fmt, arg...)                                            \
	do {                                                                   \
		dvpp_do_log(LOGLEVEL_DEBUG, fmt, ##arg);                       \
	} while (0)

#define dvpp_log_warn(fmt, arg...)                                             \
	do {                                                                   \
		dvpp_do_log(LOGLEVEL_WARN, fmt, ##arg);                        \
	} while (0)

#define dvpp_log_notice(fmt, arg...)                                           \
	do {                                                                   \
		dvpp_do_log(LOGLEVEL_NOTICE, fmt, ##arg);                      \
	} while (0)

#define dvpp_log_error(fmt, arg...)                                            \
	do {                                                                   \
		dvpp_do_log(LOGLEVEL_ERR, fmt, ##arg);                         \
	} while (0)

#endif
