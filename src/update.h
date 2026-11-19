/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - updates from inside the radio (rootfs/usr/sbin/x6100-update)
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* At start: a version that was taken back is said once; a minute later the running version is confirmed */
void update_init(void);

/* Settings, F5 Update: the first press checks GitHub; when a newer version is there, the next press installs it */
void update_key(void);

#ifdef __cplusplus
}
#endif
