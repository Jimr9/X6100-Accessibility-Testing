/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#pragma once

#include <stdbool.h>
#include <aether_radio/x6100_control/control.h>

#include "lvgl/lvgl.h"

#define RADIO_SAMPLES   (512)

typedef enum {
    RADIO_RX = 0,
    RADIO_TX,
    RADIO_ATU_START,
    RADIO_ATU_WAIT,
    RADIO_ATU_RUN,
    RADIO_SWRSCAN,

    RADIO_POWEROFF,
    RADIO_OFF
} radio_state_t;

typedef enum {
    RADIO_CHARGER_OFF = 0,
    RADIO_CHARGER_ON,
    RADIO_CHARGER_SHADOW
} radio_charger_t;

typedef void (*radio_rx_tx_change_t) (bool tx);

void radio_init();
void radio_start();
void radio_set_rx_tx_notify_fn(radio_rx_tx_change_t cb);
void radio_set_low_power_cb(void(*cb)(bool));
void radio_bb_reset();
radio_state_t radio_get_state();

/**
 * Set freq for radio without updating corresponding subject.
 * Useful for FT8 TX freq change and SWR scan
 */
void radio_set_freq(int32_t freq);
bool radio_check_freq(int32_t freq);

x6100_vfo_t radio_toggle_vfo();

uint16_t radio_change_vol(int16_t df);
bool radio_change_spmode(int16_t df);

void radio_change_mute();

void radio_set_pwr(float d);

void radio_set_charger(bool on);

void radio_start_atu();

/**
 * If an ATU tuning result is waiting to be announced (tuning finished but
 * not yet spoken), speak it now and return true. Otherwise return false -
 * the tuner hardware needs an unpredictable settle time after tuning
 * (varies with how far off the match was) before the audio path is safe
 * to use, so the result is deliberately not spoken automatically; the
 * caller should fall back to its normal action (e.g. ATU on/off toggle)
 * when this returns false.
 */
bool radio_atu_announce_pending();

bool radio_start_swrscan();
void radio_stop_swrscan();

void radio_poweroff();
void radio_set_ptt(bool tx);
void radio_set_modem(bool tx);

void radio_set_line_in(uint8_t d);
void radio_set_line_out(uint8_t d);

void radio_set_morse_key(bool on);

int8_t radio_get_audio_in_lvl();
