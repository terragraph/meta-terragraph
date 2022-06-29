/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 */

/*******************************************************************************
 * The driver module should add the following lines before using the DBG macro
 * #include "fb_tgd_debug.h"
 * E_DBG_ENABLE_VALUE tgd_dbg_enable_level = 0xffffffff; //enable all dbg
 *messages
 *                                       = 0x00000000; //disable all dbg
 *messages
 *                                       = <bitEnableValues> to selectivly
 *enable
 *******************************************************************************/

#ifndef _DBG_LVL_HDR_
#define _DBG_LVL_HDR_

//#define DISABLE_ALL_DBG

#define ENABLE_ALL_DBG_MSG 0x7004f

typedef enum {
	DBG_LVL_CTRL_ERROR = 0x00000001,
	DBG_LVL_CTRL_DBG = 0x00000002,
	DBG_LVL_CTRL_INFO = 0x00000004,
	DBG_LVL_GPS_DBG = 0x00000008,
	DBG_LVL_QUEUE_STATS_DBG = 0x00000010,

	// When set, debug queue stats are logged every time
	// they are collected. This flag is deliberately excluded
	// from ENABLE_ALL_DBG_MSG as it does not turn on a new
	// type of log message by itself.
	DBG_LVL_QUEUE_STATS_DISABLE_THROTTLE = 0x00000020,

	DBG_LVL_CFG80211_DBG = 0x00000040,

	DBG_LVL_DATA_ERROR = 0x00010000,
	DBG_LVL_DATA_DBG = 0x00020000,
	DBG_LVL_DATA_INFO = 0x00040000,
} E_DBG_ENABLE_VALUE;

extern u32 tgd_dbg_enable_level;

/* Define to disable function/line info in debug messages */
#define TGD_DISABLE_LINE_INFO
/* Define to disable debug messages completely */
#undef TGD_DISABLE_ALL_DBG

/*-----------------------------------------------------------------------------
-----------------------------------------------------------------------------*/
#ifndef TGD_DISABLE_ALL_DBG
#define TGD_DBG_ENABLED(mask) ((tgd_dbg_enable_level & (mask)) != 0)
#ifdef TGD_DISABLE_LINE_INFO
#define TGD_DBG_PRINT(mask, format, args...)                                   \
	do {                                                                   \
		if (TGD_DBG_ENABLED(mask)) {                                   \
			printk(KERN_WARNING format, ##args);                   \
		}                                                              \
	} while (0)
#else /* TGD_DISABLE_LINE_INFO */
#define TGD_DBG_PRINT(mask, format, args...)                                   \
	do {                                                                   \
		if (TGD_DBG_ENABLED(mask)) {                                   \
			printk(KERN_WARNING "%s Line: %d " format,             \
			       __FUNCTION__, __LINE__, ##args);                \
		}                                                              \
	} while (0)
#endif /* TGD_DISABLE_LINE_INFO */
#else  /* TGD_DISABLE_ALL_DBG */
#define TGD_DBG_ENABLED(mask) (0)
#define TGD_DBG_PRINT(mask, format, args...)
#endif /* TGD_DISABLE_ALL_DBG */

#define TGD_DBG_CTRL_ERROR(format, args...)                                    \
	TGD_DBG_PRINT(DBG_LVL_CTRL_ERROR, format, ##args)
#define TGD_DBG_CTRL_DBG(format, args...)                                      \
	TGD_DBG_PRINT(DBG_LVL_CTRL_DBG, format, ##args)
#define TGD_DBG_CTRL_INFO(format, args...)                                     \
	TGD_DBG_PRINT(DBG_LVL_CTRL_INFO, format, ##args)
#define TGD_DBG_GPS_DBG(format, args...)                                       \
	TGD_DBG_PRINT(DBG_LVL_GPS_DBG, format, ##args)
#define TGD_DBG_QUEUE_STATS_DBG(format, args...)                               \
	TGD_DBG_PRINT(DBG_LVL_QUEUE_STATS_DBG, format, ##args)
#define TGD_DBG_CFG80211_DBG(format, args...)                                  \
	TGD_DBG_PRINT(DBG_LVL_CFG80211_DBG, format, ##args)
#define TGD_DBG_DATA_ERROR(format, args...)                                    \
	TGD_DBG_PRINT(DBG_LVL_DATA_ERROR, format, ##args)
#define TGD_DBG_DATA_DBG(format, args...)                                      \
	TGD_DBG_PRINT(DBG_LVL_DATA_DBG, format, ##args)
#define TGD_DBG_DATA_INFO(format, args...)                                     \
	TGD_DBG_PRINT(DBG_LVL_DATA_INFO, format, ##args)
#define TGD_DBG_QUEUE_STATS_DISABLE_THROTTLE                                   \
	TGD_DBG_ENABLED(DBG_LVL_QUEUE_STATS_DISABLE_THROTTLE)

#endif // ifdef _DBG_LVL_HDR_
