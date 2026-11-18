/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - speaks what the knob lands on (accessibility)
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "lvgl/lvgl.h"

/* Speaks the newly focused object of `group` while the app speech helper
 * is on (voice_auto_active(), see voice.cpp). */
void voice_focus_init(lv_group_t *group);

/* Plain-text description of `obj` for speech: a list row, a text field,
 * a switch... False when there is nothing to say. */
bool voice_focus_describe(lv_obj_t *obj, char *buf, size_t size);

/* An instruction a window shows on screen (SSTV's transmit steps). Spoken
 * once, together with the focus change that usually comes with it in the
 * same pass ("<hint>. <focused item>"), or alone if none comes - so the two
 * never cut each other off. Only while the app speech helper is on. */
void voice_focus_hint(const char *text);
