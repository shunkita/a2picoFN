/*
 * Copyright (c) 2026 a2picoAFP / a2picoFN
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUD_ENABLED                 1
#define CFG_TUD_NET                     1
#define CFG_TUD_ECM_RNDIS               1
#define CFG_TUH_ENABLED                 0

#define CFG_TUSB_RHPORT0_MODE           (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS                     OPT_OS_NONE
#endif

#define BOARD_TUD_MAX_SPEED             OPT_MODE_DEFAULT_SPEED
#define CFG_TUD_MAX_SPEED               BOARD_TUD_MAX_SPEED

#define CFG_TUD_CDC                     0
#define CFG_TUD_MSC                     0

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
