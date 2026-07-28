/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include "dialog.h"

#include "buttons.h"

#include "lvgl/lvgl.h"

typedef enum {
    MSG_VOICE_OFF = 0,
    MSG_VOICE_ARMED,  // Rec pressed once with voice on; waiting for a second
                       // press to actually start - see dialog_msg_voice_rec_cb()
    MSG_VOICE_RECORD,
    MSG_VOICE_PLAY
} msg_voice_state_t;

extern dialog_t *dialog_msg_voice;

msg_voice_state_t dialog_msg_voice_get_state();
void dialog_msg_voice_put_audio_samples(size_t nsamples, int16_t *samples);
