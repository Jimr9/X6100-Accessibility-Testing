/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "settings_types.h"

#ifdef __cplusplus

#include <cstdarg>

extern "C" {
#include <stdint.h>
#endif

typedef struct {
    const char *name;
    const char *label;
    const char *welcome;
} voice_item_t;

extern voice_item_t voice_item[VOICES_NUM];

void voice_sure();
void voice_change_mode();

void voice_say_text_fmt(const char * fmt, ...);
void voice_delay_say_text_fmt(const char * fmt, ...);
void voice_say_prompted_fmt(const char *prompt, const char * fmt, ...);
void voice_say_freq(uint64_t freq);
void voice_say_freq_now(uint64_t freq);
void voice_say_prompted_freq_now(const char *prompt, uint64_t freq);

void voice_say_bool(const char *prompt, bool x);
void voice_say_int(const char *prompt, int32_t x);
void voice_say_float(const char *prompt, float x);
void voice_say_float2(const char *prompt, float x);
void voice_say_text(const char *prompt, const char *x);
void voice_say_lang();

void voice_wait_done();

/* ---- Accessibility helpers for windows with no speech of their own ----
 *
 * voice_say_count(): goes up on every attempt to speak (and on a pop-up
 * message about to be spoken), so a caller can tell whether something it
 * just ran said anything at all.
 *
 * voice_auto_set(): the "app speech helper", on while the JS8, WeFax or
 * SSTV window is open (main_screen.c), off when any window closes
 * (dialog.c). While on, focus changes, pop-up messages and otherwise
 * silent F-key presses are spoken (voice_focus.c, msg.c, buttons.cpp).
 * voice_auto_active() is false while transmitting: those windows transmit
 * on their own (JS8 heartbeats and replies), and speech must not go out
 * over the air.
 *
 * voice_stop(): cuts off any announcement, cleanly, before the app starts
 * a digital transmission (radio_set_modem()).
 *
 * voice_clean_text(): screen text made speakable - colour codes, symbols
 * and line breaks out. voice_say_clean() speaks it (delayed: debounced like
 * other knob speech). */
uint32_t voice_say_count();
void voice_count_bump();
void voice_auto_set(bool on);
bool voice_auto_active();
void voice_stop();
void voice_clean_text(const char *in, char *out, size_t size);
void voice_say_clean(const char *text, bool delayed);

#ifdef __cplusplus
}
#endif
