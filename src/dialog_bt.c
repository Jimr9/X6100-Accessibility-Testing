/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *  Copyright (c) 2026 Krzysztof Myszkowski SP2KMO
 *  Part of OpenX6100 (github.com/OpenX6100)
 *
 *  Xiegu X6100 LVGL GUI - Bluetooth window
 *
 *  Adapter power, the device list, and the listening loopback to
 *  Bluetooth headphones, in one place: status on the left, a table on
 *  the right, actions on the button row.
 *
 *  PTT keys the radio from the Bluetooth headset microphone (HFP).
 *  It is a toggle: press to transmit, press again to receive. A short
 *  press of the headset's own button does the same (bt_audio.c). The
 *  worker in bt_audio drops TX after BT_PTT_TIMEOUT_S, when the headset
 *  goes away, when Bluetooth is switched off and when this window
 *  closes.
 *
 *  The UDP switch (WSJT-X datagrams over WiFi, ft8_udp.c) lives here as
 *  well: the FT8 window has no free button and this is the window that
 *  already deals with where the FT8 packets go.
 *
 *  Buttons, three pages:
 *    1: (Page 1:3)  BT  Audio  PTT  UDP      - used while operating
 *    2: (Page 2:3)  Scan  Connect  Forget  Exit  - pairing and devices
 *    3: (Page 3:3)  Mic gain                 - headset mic level for PTT
 *
 *  Mic gain: press +1 dB, hold -1 dB. Set it so the ALC just moves on
 *  voice peaks while transmitting from the headset.
 *
 *  Scan opens pairing for 3 minutes: the radio is visible and a phone
 *  can pair from its side; the rest of the time it is hidden and refuses
 *  pairing. A keyboard: press its pairing key, then Connect, and type
 *  the code shown here if it asks for one.
 *
 *  Pairing is part of Connect (bt_dev pairs first when needed). Forget
 *  removes the pairing; it acts on hold only, a press just says so.
 *
 *  Nothing here blocks. Every D-Bus and pactl call belongs to
 *  bt_dev or bt_audio and runs on their worker threads; this file only
 *  reads cached values and asks for work.
 */

#include "dialog_bt.h"

#include "lvgl/lvgl.h"

#include "buttons.h"
#include "dialog.h"
#include "events.h"
#include "keyboard.h"
#include "main_screen.h"
#include "msg.h"
#include "radio.h"
#include "styles.h"
#include "voice.h"
#include "wifi.h"
#include "cfg/cfg_api.h"

#include "bt_audio.h"
#include "bt_ctl.h"
#include "bt_dev.h"
#include "bt_spp.h"
#include "ft8_udp.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* Window size: DIALOG_WIDTH/HEIGHT from styles.h, as the WiFi window
 * uses. */
#define PARAMS_WIDTH  300

/* A device refresh is a D-Bus round trip plus one pactl, so it does not
 * get to run every tick. pactl is not free either. */
#define TICK_MS       1000
#define DEV_EVERY     3
#define AUDIO_EVERY   2

static void construct_cb(lv_obj_t *parent);
static void destruct_cb(void);
static void key_cb(lv_event_t *e);

static void bt_toggle_cb(button_data_t *item);
static void audio_toggle_cb(button_data_t *item);
static void connect_cb(button_data_t *item);
static void scan_cb(button_data_t *item);
static void ptt_cb(button_data_t *item);
static void udp_toggle_cb(button_data_t *item);
static void forget_cb(button_data_t *item);
static void forget_hold_cb(button_data_t *item);
static void exit_cb(button_data_t *item);
static void mic_gain_up_cb(button_data_t *item);
static void mic_gain_down_cb(button_data_t *item);
static void status_say_cb(button_data_t *item);

static const char *bt_label_getter(void);
static const char *audio_label_getter(void);
static const char *connect_label_getter(void);
static const char *scan_label_getter(void);
static const char *ptt_label_getter(void);
static const char *udp_label_getter(void);
static const char *mic_gain_label_getter(void);

static button_data_t btn_bt = {
    .type = BTN_TEXT_FN, .label_fn = bt_label_getter, .press = bt_toggle_cb,
};
static button_data_t btn_audio = {
    .type = BTN_TEXT_FN, .label_fn = audio_label_getter,
    .press = audio_toggle_cb,
};
static button_data_t btn_ptt = {
    .type = BTN_TEXT_FN, .label_fn = ptt_label_getter, .press = ptt_cb,
};
static button_data_t btn_udp = {
    .type = BTN_TEXT_FN, .label_fn = udp_label_getter,
    .press = udp_toggle_cb,
};
/* Exit stays on the hold of Scan too, as in the one-page layout.
 * ESC closes the window as well. */
static button_data_t btn_scan = {
    .type = BTN_TEXT_FN, .label_fn = scan_label_getter, .press = scan_cb,
    .hold = exit_cb,
};
static button_data_t btn_connect = {
    .type = BTN_TEXT_FN, .label_fn = connect_label_getter,
    .press = connect_cb,
};
static button_data_t btn_forget = {
    .type = BTN_TEXT, .label = "Forget\n(hold)", .press = forget_cb,
    .hold = forget_hold_cb,
};
static button_data_t btn_exit = {
    .type = BTN_TEXT, .label = "Exit", .press = exit_cb,
};
static button_data_t btn_mic = {
    .type = BTN_TEXT_FN, .label_fn = mic_gain_label_getter,
    .press = mic_gain_up_cb, .hold = mic_gain_down_cb,
};
/* The status panel on the left is plain labels, out of reach of the
 * knob: this reads it out on demand. */
static button_data_t btn_status = {
    .type = BTN_TEXT, .label = "Status", .press = status_say_cb,
};

static buttons_page_t btn_page_1;
static buttons_page_t btn_page_2;
static buttons_page_t btn_page_3;

static button_data_t btn_next_1 = {
    .type = BTN_TEXT, .label = "(Page: 1:3)", .press = button_next_page_cb,
    .next = &btn_page_2, .voice = "Bluetooth page 1",
};
static button_data_t btn_next_2 = {
    .type = BTN_TEXT, .label = "(Page: 2:3)", .press = button_next_page_cb,
    .next = &btn_page_3, .voice = "Bluetooth page 2",
};
static button_data_t btn_next_3 = {
    .type = BTN_TEXT, .label = "(Page: 3:3)", .press = button_next_page_cb,
    .next = &btn_page_1, .voice = "Bluetooth page 3",
};

static buttons_page_t btn_page_1 = {
    {&btn_next_1, &btn_bt, &btn_audio, &btn_ptt, &btn_udp}
};
static buttons_page_t btn_page_2 = {
    {&btn_next_2, &btn_scan, &btn_connect, &btn_forget, &btn_exit}
};
static buttons_page_t btn_page_3 = {
    {&btn_next_3, &btn_mic, &btn_status}
};

static dialog_t dialog = {
    .run = false,
    .construct_cb = construct_cb,
    .destruct_cb = destruct_cb,
    .key_cb = key_cb,
};

dialog_t *dialog_bt = &dialog;

static lv_obj_t   *table = NULL;
static lv_obj_t   *label_adapter = NULL;
static lv_obj_t   *label_phone = NULL;
static lv_obj_t   *label_audio = NULL;
static lv_timer_t *timer_status = NULL;
static uint32_t    tick = 0;
static bool        udp_on = false;   /* cached ft8_udp_is_enabled() */
static char        last_activity[48];

/* ---- Table ------------------------------------------------------------- */

/* Address shown in each row, as last drawn. bt_dev's list changes order
 * while a Scan finds devices, so the highlight is kept on a device, not
 * on a row number: Connect must act on the device the operator picked. */
static char row_addr[BT_DEV_MAX][18];
static int  row_count = 0;

/* The device in the highlighted row, looked up by its address in the
 * current list. */
static bool selected_device(bt_dev_t *out)
{
    uint16_t row, col;
    int      i;

    if (table == NULL) {
        return false;
    }
    lv_table_get_selected_cell(table, &row, &col);
    if (row >= row_count) {
        return false;
    }
    for (i = 0; i < bt_dev_count(); i++) {
        if (bt_dev_get(i, out) && strcmp(out->addr, row_addr[row]) == 0) {
            return true;
        }
    }
    return false;
}

static void table_refresh(void)
{
    char     buf[96];
    char     sel[18] = "";
    int      n = bt_dev_count();
    int      i;
    uint16_t row, col;
    bool     seen = false;
    bt_dev_t dev;

    if (table == NULL) {
        return;
    }

    lv_table_get_selected_cell(table, &row, &col);
    if (row < row_count) {
        snprintf(sel, sizeof(sel), "%s", row_addr[row]);
    }

    if (n == 0) {
        row_count = 0;
        lv_table_set_row_cnt(table, 1);
        lv_table_set_cell_value(table, 0, 0,
                                "No devices - press Scan");
        return;
    }

    lv_table_set_row_cnt(table, (uint16_t)n);

    row_count = 0;
    for (i = 0; i < n; i++) {
        if (!bt_dev_get(i, &dev)) {
            break;
        }
        snprintf(row_addr[i], sizeof(row_addr[i]), "%s", dev.addr);
        row_count = i + 1;
        snprintf(buf, sizeof(buf), "%s%s%s",
                 dev.connected ? "* " : "  ",
                 dev.name,
                 dev.audio ? "  [audio]" : dev.input ? "  [kbd]" : "");
        lv_table_set_cell_value(table, (uint16_t)i, 0, buf);
        /* The highlighted device moved: the highlight goes with it.
         * LVGL 8.3 has no setter for the selected cell. */
        if (sel[0] != '\0' && strcmp(dev.addr, sel) == 0) {
            seen = true;
            if (i != row) {
                ((lv_table_t *)table)->row_act = (uint16_t)i;
                lv_obj_invalidate(table);
            }
        }
    }
    /* It left the list: no highlight rather than whatever moved into
     * its row. The next turn of the knob starts again at the top. */
    if (sel[0] != '\0' && !seen) {
        ((lv_table_t *)table)->row_act = LV_TABLE_CELL_NONE;
        ((lv_table_t *)table)->col_act = LV_TABLE_CELL_NONE;
        lv_obj_invalidate(table);
    }
}

/* ---- Speech ------------------------------------------------------------ */

/*
 * Same conventions as the WiFi window and the message dialogs:
 *  - each action speaks for itself when pressed; the status timer only
 *    speaks settled changes, once each, never the "..." in-between
 *    states (those would cut off the action's own words);
 *  - one utterance per timer tick, the changes joined together, so they
 *    don't interrupt one another;
 *  - a list row is spoken when the highlight lands on another device.
 *    The list reorders itself during a Scan and the highlight follows the
 *    device, so "another" means another address, not another row.
 *  - nothing at all while the headset PTT is keying or transmitting:
 *    with listening off, speech plays through the codec, which is the
 *    transmit audio path while the BASE is keyed.
 */

#define NAV_QUIET_MS  2000  /* hold back "Found ..." this long after a knob turn */
#define SETTLE_TICKS  2     /* a "..." activity must last this long to be said */

static bool             ptt_armed = false;
static char             spoken_addr[18];   /* "-" = the empty-list row */
static bt_ctl_state_t   said_ctl;
static bt_audio_state_t said_audio;
static bt_ptt_state_t   said_ptt;
static bt_spp_state_t   said_spp;
static bool             said_scanning;
static bool             said_pairing_open;
static char             said_activity[48];
static const char      *settling_text = NULL;
static int              settling_ticks = 0;
static uint32_t         last_nav_ms = 0;
static bt_dev_t         known[BT_DEV_MAX];
static int              known_count = 0;
static char             forget_addr[18];
static char             found_first[64];
static int              found_count = 0;

static bool ptt_busy(void)
{
    bt_ptt_state_t st = bt_ptt_state();

    return st == BT_PTT_KEYING || st == BT_PTT_TX || st == BT_PTT_UNKEYING;
}

static void bt_say(const char *fmt, ...)
{
    char    buf[256];
    va_list ap;

    if (ptt_busy()) {
        return;
    }
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    voice_say_text_fmt("%s", buf);
}

/* Name to speak: bt_dev appends " (off)" to blocked devices and uses the
 * address when a device has no name - an address read out in full is
 * twenty-odd syllables, so only its last two bytes. */
static void spoken_name(const bt_dev_t *dev, char *out, size_t len, bool *off)
{
    size_t n = strlen(dev->name);

    *off = n > 6 && strcmp(dev->name + n - 6, " (off)") == 0;
    if (*off) {
        n -= 6;
    }
    if (strncmp(dev->name, dev->addr, 17) == 0) {
        snprintf(out, len, "Unnamed device ending %c %c %c %c",
                 dev->addr[12], dev->addr[13], dev->addr[15], dev->addr[16]);
    } else {
        snprintf(out, len, "%.*s", (int)n, dev->name);
    }
}

static const char *dev_kind(const bt_dev_t *dev)
{
    return dev->audio ? ", headset" : dev->input ? ", keyboard" : "";
}

static void describe_device(const bt_dev_t *dev, char *out, size_t len)
{
    char name[48];
    bool off;

    spoken_name(dev, name, sizeof(name), &off);
    snprintf(out, len, "%s%s, %s", name, dev_kind(dev),
             dev->connected ? "connected" : off ? "off" : "not connected");
}

static bool find_known(const char *addr, bt_dev_t *out)
{
    int i;

    for (i = 0; i < known_count; i++) {
        if (strcmp(known[i].addr, addr) == 0) {
            if (out) {
                *out = known[i];
            }
            return true;
        }
    }
    return false;
}

static void snapshot_devices(void)
{
    int i;

    known_count = 0;
    for (i = 0; i < bt_dev_count() && i < BT_DEV_MAX; i++) {
        if (bt_dev_get(i, &known[known_count])) {
            known_count++;
        }
    }
}

/* The highlighted row, unless it was the last one spoken (force: always). */
static void announce_selection(bool force)
{
    uint16_t row, col;
    bt_dev_t dev;
    char     text[128];

    if (table == NULL) {
        return;
    }
    lv_table_get_selected_cell(table, &row, &col);
    if (row == LV_TABLE_CELL_NONE) {
        return;
    }

    if (row_count == 0) {
        if (!force && strcmp(spoken_addr, "-") == 0) {
            return;
        }
        snprintf(spoken_addr, sizeof(spoken_addr), "-");
        bt_say(bt_ctl_state() == BT_CTL_ON ? "No devices, press Scan"
                                           : "No devices, Bluetooth is off");
        return;
    }
    if (!selected_device(&dev)) {
        return;
    }
    if (!force && strcmp(spoken_addr, dev.addr) == 0) {
        return;
    }
    snprintf(spoken_addr, sizeof(spoken_addr), "%s", dev.addr);
    describe_device(&dev, text, sizeof(text));
    bt_say("%s, %d of %d", text, row + 1, row_count);
}

static void table_value_changed_cb(lv_event_t *e)
{
    (void)e;
    announce_selection(false);
}

static void append(char *buf, size_t len, const char *fmt, ...)
{
    size_t  used = strlen(buf);
    va_list ap;

    if (used >= len - 1) {
        return;
    }
    if (used > 0) {
        snprintf(buf + used, len - used, ", ");
        used = strlen(buf);
    }
    va_start(ap, fmt);
    vsnprintf(buf + used, len - used, fmt, ap);
    va_end(ap);
}

static const char *ctl_word(bt_ctl_state_t st)
{
    switch (st) {
        case BT_CTL_ON:       return "on";
        case BT_CTL_STARTING: return "starting";
        case BT_CTL_STOPPING: return "stopping";
        case BT_CTL_FAILED:   return "failed";
        default:              return "off";
    }
}

static const char *audio_word(bt_audio_state_t st)
{
    switch (st) {
        case BT_AUDIO_ON:       return "on";
        case BT_AUDIO_STARTING: return "starting";
        case BT_AUDIO_STOPPING: return "stopping";
        case BT_AUDIO_NO_SINK:  return "waiting, no headphones connected";
        case BT_AUDIO_FAILED:   return "failed";
        default:                return "off";
    }
}

/* Everything the status panel shows, as one sentence. */
static void status_summary(char *out, size_t len)
{
    bt_ctl_state_t ctl = bt_ctl_state();
    int            left = bt_dev_visible_s();
    int            connected = 0;
    char           name[48] = "";
    bool           off;
    bt_dev_t       dev;
    int            i;

    out[0] = '\0';
    append(out, len, "Bluetooth %s", ctl_word(ctl));
    if (ctl == BT_CTL_OFF && !param_i_get(cfg.network.wifi_enabled())) {
        append(out, len, "Wi-Fi and Bluetooth chip off");
    }
    if (ctl != BT_CTL_ON) {
        return;
    }
    if (bt_dev_scanning()) {
        append(out, len, "scanning");
    }
    if (left > 0) {
        if (left >= 60) {
            append(out, len, "pairing open, %d minute%s %d seconds left",
                   left / 60, left / 60 == 1 ? "" : "s", left % 60);
        } else {
            append(out, len, "pairing open, %d seconds left", left);
        }
    }

    for (i = 0; i < bt_dev_count(); i++) {
        if (bt_dev_get(i, &dev) && dev.connected) {
            if (connected == 0) {
                spoken_name(&dev, name, sizeof(name), &off);
            }
            connected++;
        }
    }
    append(out, len, "%d device%s", bt_dev_count(), bt_dev_count() == 1 ? "" : "s");
    if (connected == 1) {
        append(out, len, "%s connected", name);
    } else if (connected > 1) {
        append(out, len, "%d connected", connected);
    }

    append(out, len, "listening audio %s", audio_word(bt_audio_state()));
    if (bt_spp_state() == BT_SPP_CONNECTED) {
        append(out, len, "phone connected for F T 8 packets");
    }
    append(out, len, "U D P %s", udp_on ? "on" : "off");
}

/* "Type 123456 on the keyboard, Enter": the digits one by one. */
static void spell_code(const char *activity, char *out, size_t len)
{
    const char *p = activity + 5;
    char        digits[32] = "";
    size_t      n = 0;

    while (*p != '\0' && *p != ' ' && n + 3 < sizeof(digits)) {
        digits[n++] = *p++;
        digits[n++] = ' ';
    }
    digits[n ? n - 1 : 0] = '\0';
    snprintf(out, len, "Type %s on the keyboard, then Enter", digits);
}

/* Called by the status timer after the refresh: speaks what changed. */
static void speak_changes(void)
{
    char             text[256] = "";
    bt_ctl_state_t   ctl = bt_ctl_state();
    bt_audio_state_t audio = bt_audio_state();
    bt_ptt_state_t   ptt = bt_ptt_state();
    bt_spp_state_t   spp = bt_spp_state();
    bool             scanning = bt_dev_scanning();
    bool             pairing_open = bt_dev_visible_s() > 0;
    const char      *act = bt_dev_activity();
    char             name[48];
    bool             off;
    bt_dev_t         dev, old;
    int              i;

    /* Adapter: the press already said "turning on" / "off". */
    if (ctl != said_ctl) {
        if (ctl == BT_CTL_ON) {
            append(text, sizeof(text), "Bluetooth on");
        } else if (ctl == BT_CTL_FAILED) {
            append(text, sizeof(text), "Bluetooth failed to start");
        }
        said_ctl = ctl;
    }

    /* Pairing and connecting, from bt_dev's worker. */
    if (strcmp(act, said_activity) != 0) {
        snprintf(said_activity, sizeof(said_activity), "%s", act);
        settling_text = NULL;
        if (strncmp(act, "Type ", 5) == 0) {
            char code[96];

            spell_code(act, code, sizeof(code));
            append(text, sizeof(text), "%s", code);
        } else if (strcmp(act, "Press the keyboard's pairing key") == 0) {
            append(text, sizeof(text), "Press the keyboard's pairing key");
        } else if (strcmp(act, "Not found") == 0) {
            append(text, sizeof(text), "Not found, is it in pairing mode?");
        } else if (strcmp(act, "Pairing failed") == 0) {
            append(text, sizeof(text), "Pairing failed, is it in pairing mode?");
        } else if (strcmp(act, "Connect failed") == 0) {
            append(text, sizeof(text), "Connect failed, device busy? Hold Forget, then pair again");
        } else if (strcmp(act, "Phone: connect from app") == 0) {
            append(text, sizeof(text), "Phone allowed, connect from the phone app");
        } else if (strcmp(act, "pairing...") == 0) {
            settling_text = "Pairing";
            settling_ticks = 0;
        } else if (strcmp(act, "searching...") == 0) {
            settling_text = "Searching for the device";
            settling_ticks = 0;
        }
    } else if (settling_text != NULL && ++settling_ticks >= SETTLE_TICKS) {
        append(text, sizeof(text), "%s", settling_text);
        settling_text = NULL;
    }

    /* Devices: connected / disconnected / found / gone. */
    for (i = 0; i < bt_dev_count(); i++) {
        if (!bt_dev_get(i, &dev)) {
            break;
        }
        spoken_name(&dev, name, sizeof(name), &off);
        if (find_known(dev.addr, &old)) {
            if (dev.connected != old.connected) {
                append(text, sizeof(text), "%s %s", name,
                       dev.connected ? "connected" : "disconnected");
            }
        } else if (dev.connected) {
            /* A keyboard pairs under a fresh entry (bt_dev drops the old
             * one first), so its success shows up as a new device. */
            append(text, sizeof(text), "%s connected", name);
        } else if (scanning || said_scanning) {
            if (found_count == 0) {
                snprintf(found_first, sizeof(found_first), "%s%s", name, dev_kind(&dev));
            }
            found_count++;
        }
    }
    for (i = 0; i < known_count; i++) {
        bool still = false;
        int  j;

        for (j = 0; j < bt_dev_count(); j++) {
            if (bt_dev_get(j, &dev) && strcmp(dev.addr, known[i].addr) == 0) {
                still = true;
                break;
            }
        }
        if (still) {
            continue;
        }
        spoken_name(&known[i], name, sizeof(name), &off);
        if (strcmp(known[i].addr, forget_addr) == 0) {
            append(text, sizeof(text), "%s forgotten", name);
            forget_addr[0] = '\0';
        } else if (strcmp(known[i].addr, spoken_addr) == 0 && act[0] == '\0' &&
                   ctl == BT_CTL_ON) {
            /* Not while the worker is busy (a keyboard being paired leaves
             * and comes back on purpose), nor as Bluetooth goes off. */
            append(text, sizeof(text), "%s left the list", name);
        }
        if (strcmp(known[i].addr, spoken_addr) == 0) {
            spoken_addr[0] = '\0';
        }
    }
    snapshot_devices();

    /* Found devices wait while the knob is being turned, so they don't cut
     * off the row being read; the list is still settling then anyway. */
    if (found_count > 0 && lv_tick_elaps(last_nav_ms) > NAV_QUIET_MS) {
        if (found_count == 1) {
            append(text, sizeof(text), "Found %s", found_first);
        } else {
            append(text, sizeof(text), "Found %s and %d more", found_first, found_count - 1);
        }
        found_count = 0;
    }

    if (said_scanning && !scanning) {
        append(text, sizeof(text), "Scan complete, %d device%s", bt_dev_count(),
               bt_dev_count() == 1 ? "" : "s");
    }
    said_scanning = scanning;

    if (said_pairing_open && !pairing_open) {
        append(text, sizeof(text), "Pairing closed, radio hidden");
    }
    said_pairing_open = pairing_open;

    /* Listening audio: the press already said "turning on" / "off". */
    if (audio != said_audio) {
        if (audio == BT_AUDIO_ON) {
            append(text, sizeof(text), "Listening audio on");
        } else if (audio == BT_AUDIO_NO_SINK || audio == BT_AUDIO_FAILED) {
            append(text, sizeof(text), "Listening audio %s", audio_word(audio));
        }
        said_audio = audio;
    }

    /* PTT: keying is silent (see bt_say); the outcome is said once the
     * transmitter is off again. The headset button toggles it too. */
    if (ptt != said_ptt) {
        switch (ptt) {
            case BT_PTT_OFF:
                if (said_ptt == BT_PTT_TX || said_ptt == BT_PTT_KEYING ||
                    said_ptt == BT_PTT_UNKEYING) {
                    append(text, sizeof(text), "P T T off, receiving");
                }
                break;
            case BT_PTT_TIMEOUT:
                append(text, sizeof(text), "P T T timed out, receiving");
                break;
            case BT_PTT_NO_MIC:
                append(text, sizeof(text), "P T T failed, no headset microphone");
                break;
            case BT_PTT_FAILED:
                append(text, sizeof(text), "P T T failed");
                break;
            default:
                break;
        }
        said_ptt = ptt;
    }

    /* Phone link for the FT8 packets. */
    if (spp != said_spp) {
        if (spp == BT_SPP_CONNECTED) {
            append(text, sizeof(text), "Phone connected for F T 8 packets");
        } else if (said_spp == BT_SPP_CONNECTED) {
            append(text, sizeof(text), "Phone disconnected");
        } else if (spp == BT_SPP_ERROR) {
            append(text, sizeof(text), "F T 8 packet link error");
        }
        said_spp = spp;
    }

    if (text[0] != '\0') {
        bt_say("%s", text);
    }
}

/* Start every "said" state from what is true now, so opening the window
 * doesn't announce old news. */
static void speech_reset(void)
{
    ptt_armed = false;
    spoken_addr[0] = '\0';
    said_ctl = bt_ctl_state();
    said_audio = bt_audio_state();
    said_ptt = bt_ptt_state();
    said_spp = bt_spp_state();
    said_scanning = bt_dev_scanning();
    said_pairing_open = bt_dev_visible_s() > 0;
    snprintf(said_activity, sizeof(said_activity), "%s", bt_dev_activity());
    settling_text = NULL;
    forget_addr[0] = '\0';
    found_count = 0;
    last_nav_ms = 0;
    snapshot_devices();
}

/* ---- Status ------------------------------------------------------------ */

/* bluez_sink.50_C2_ED_96_B6_72.a2dp_sink is wider than the panel and
 * the interesting part is at the end. */
static const char *short_sink(const char *sink)
{
    const char *dot;

    if (sink == NULL || sink[0] == '\0') {
        return "none";
    }
    dot = strchr(sink, '.');
    return (dot != NULL) ? dot + 1 : sink;
}

static void status_refresh(void)
{
    uint32_t sent = 0;
    uint32_t dropped = 0;

    if (label_adapter == NULL) {
        return;
    }

    switch (bt_ctl_state()) {
        case BT_CTL_STARTING:
            lv_label_set_text(label_adapter, "Starting...");
            break;
        case BT_CTL_STOPPING:
            lv_label_set_text(label_adapter, "Stopping...");
            break;
        case BT_CTL_FAILED:
            lv_label_set_text(label_adapter, "Failed");
            break;
        case BT_CTL_ON: {
            /* Visible and pairable only for a few minutes after Scan. */
            int left = bt_dev_visible_s();

            if (left > 0) {
                lv_label_set_text_fmt(label_adapter, "%s\nPairing %d:%02d",
                                      bt_ctl_addr(), left / 60, left % 60);
            } else {
                lv_label_set_text_fmt(label_adapter, "%s\nHidden", bt_ctl_addr());
            }
            break;
        }
        default:
            /* WiFi off in the WiFi window cuts power to the shared
             * WiFi/BT chip; BT On powers it up again. */
            lv_label_set_text(label_adapter,
                              param_i_get(cfg.network.wifi_enabled())
                                  ? "Off" : "Off\nWiFi/BT chip off");
            break;
    }

    /* The UDP state comes from a cached flag; ft8_udp_is_enabled()
     * would re-read the config file every second. */
    bt_spp_stats(&sent, &dropped);
    if (bt_spp_state() == BT_SPP_CONNECTED) {
        lv_label_set_text_fmt(label_phone, "%s\nsent %u drop %u\nUDP: %s",
                              bt_spp_peer(), (unsigned)sent,
                              (unsigned)dropped, udp_on ? "On" : "Off");
    } else if (bt_spp_state() == BT_SPP_LISTENING) {
        lv_label_set_text_fmt(label_phone, "BT: waiting\nUDP: %s",
                              udp_on ? "On" : "Off");
    } else {
        lv_label_set_text_fmt(label_phone, "BT: -\nUDP: %s",
                              udp_on ? "On" : "Off");
    }

    /* While bt_dev pairs or connects, or after it failed, say so here
     * whatever the loopback state; before, it only showed with audio
     * Off and "No headphones" hid it. */
    if (bt_dev_activity()[0] != '\0' && bt_audio_state() != BT_AUDIO_ON) {
        lv_label_set_text(label_audio, bt_dev_activity());
        return;
    }

    switch (bt_audio_state()) {
        case BT_AUDIO_STARTING:
            lv_label_set_text(label_audio, "Starting...");
            break;
        case BT_AUDIO_STOPPING:
            lv_label_set_text(label_audio, "Stopping...");
            break;
        case BT_AUDIO_NO_SINK:
            lv_label_set_text(label_audio, "No headphones");
            break;
        case BT_AUDIO_FAILED:
            lv_label_set_text(label_audio, "Failed");
            break;
        case BT_AUDIO_ON:
            lv_label_set_text_fmt(label_audio, "%s\n%u ms",
                                  short_sink(bt_audio_sink()),
                                  (unsigned)bt_audio_measured_ms());
            break;
        default:
            if (bt_dev_activity()[0] != '\0') {
                lv_label_set_text(label_audio, bt_dev_activity());
            } else {
                lv_label_set_text(label_audio, "Off");
            }
            break;
    }
}

static void page_refresh(void)
{
    buttons_page_t *page = buttons_get_cur_page();
    int             i;

    if (page == NULL) {
        return;
    }
    for (i = 0; i < BUTTONS; i++) {
        if (page->items[i] != NULL && page->items[i]->disp_btn != NULL) {
            buttons_refresh(page->items[i]);
        }
    }
}

static void status_timer_cb(lv_timer_t *t)
{
    (void)t;

    tick++;

    if (bt_ctl_state() == BT_CTL_ON) {
        if ((tick % DEV_EVERY) == 0) {
            bt_dev_refresh();
        }
        if ((tick % AUDIO_EVERY) == 0) {
            bt_audio_poll();
        }
    }

    table_refresh();
    status_refresh();
    speak_changes();

    /* bt_dev works in the background; a failed pair/connect would only
     * show in the small audio line, so say it out loud once. */
    if (strcmp(bt_dev_activity(), last_activity) != 0) {
        snprintf(last_activity, sizeof(last_activity), "%s",
                 bt_dev_activity());
        if (strncmp(last_activity, "Type ", 5) == 0 ||
            strncmp(last_activity, "Press the keyboard", 18) == 0) {
            /* Keyboard pairing: the code to type, or the pairing key. */
            msg_update_text_fmt("%s", last_activity);
        } else if (strcmp(last_activity, "Not found") == 0) {
            msg_update_text_fmt("Not found - is it in pairing mode?");
        } else if (strcmp(last_activity, "Pairing failed") == 0) {
            msg_update_text_fmt("Pairing failed - headset in pairing mode?");
        } else if (strcmp(last_activity, "Connect failed") == 0) {
            msg_update_text_fmt("Connect failed - headset busy? Try Forget");
        }
    }

    /* BTN_TEXT_FN without .subj does not repaint itself. Only the
     * buttons of the page on screen have a widget; buttons_refresh()
     * on the others would log a warning every tick. */
    page_refresh();
}

/* ---- Button labels ----------------------------------------------------- */

static const char *bt_label_getter(void)
{
    static char buf[24];
    const char *st;

    switch (bt_ctl_state()) {
        case BT_CTL_ON:       st = "On";    break;
        case BT_CTL_STARTING: st = "...";   break;
        case BT_CTL_STOPPING: st = "...";   break;
        case BT_CTL_FAILED:   st = "Error"; break;
        default:              st = "Off";   break;
    }
    snprintf(buf, sizeof(buf), "BT:\n%s", st);
    return buf;
}

static const char *audio_label_getter(void)
{
    static char buf[24];
    const char *st;

    switch (bt_audio_state()) {
        case BT_AUDIO_ON:       st = "On";    break;
        case BT_AUDIO_STARTING: st = "...";   break;
        case BT_AUDIO_STOPPING: st = "...";   break;
        case BT_AUDIO_NO_SINK:  st = "No BT"; break;
        case BT_AUDIO_FAILED:   st = "Error"; break;
        default:                st = "Off";   break;
    }
    snprintf(buf, sizeof(buf), "Audio:\n%s", st);
    return buf;
}

static const char *connect_label_getter(void)
{
    bt_dev_t dev;

    if (!selected_device(&dev)) {
        return "Connect";
    }
    return dev.connected ? "Disconnect" : "Connect";
}

static const char *scan_label_getter(void)
{
    return bt_dev_scanning() ? "Scanning" : "Scan";
}

static const char *ptt_label_getter(void)
{
    static char buf[24];

    switch (bt_ptt_state()) {
        case BT_PTT_KEYING:
        case BT_PTT_UNKEYING:
            snprintf(buf, sizeof(buf), "PTT:\n...");
            break;
        case BT_PTT_TX:
            snprintf(buf, sizeof(buf), "PTT:\nTX %us",
                     (unsigned)bt_ptt_seconds());
            break;
        case BT_PTT_NO_MIC:
            snprintf(buf, sizeof(buf), "PTT:\nNo mic");
            break;
        case BT_PTT_FAILED:
            snprintf(buf, sizeof(buf), "PTT:\nError");
            break;
        case BT_PTT_TIMEOUT:
            snprintf(buf, sizeof(buf), "PTT:\nTimeout");
            break;
        default:
            snprintf(buf, sizeof(buf), "PTT:\nRX");
            break;
    }
    return buf;
}

static const char *udp_label_getter(void)
{
    return udp_on ? "UDP:\nOn" : "UDP:\nOff";
}

/* ---- Actions ----------------------------------------------------------- */

/* Any other action cancels a PTT that was armed and not started. */
static void disarm_ptt(void)
{
    ptt_armed = false;
}

static bool require_bt_on(void)
{
    if (bt_ctl_state() != BT_CTL_ON) {
        msg_update_text_fmt("Turn Bluetooth on first");
        bt_say("Turn Bluetooth on first");
        return false;
    }
    return true;
}

static bool require_selection(bt_dev_t *dev)
{
    if (!selected_device(dev)) {
        msg_update_text_fmt("No device selected");
        bt_say("No device selected");
        return false;
    }
    return true;
}

static void bt_toggle_cb(button_data_t *item)
{
    bt_ctl_state_t st = bt_ctl_state();

    disarm_ptt();
    if (st == BT_CTL_OFF || st == BT_CTL_FAILED) {
        /* WiFi and Bluetooth are one chip on one power pin
         * (x6100_pin_wifi). With WiFi switched off in the WiFi window
         * the chip is unpowered, hci0 never appears and bt_up.sh used to
         * give up after 30 s with "Error". Power it up first, the way
         * the WiFi window does; bt_up.sh waits for hci0. */
        if (!param_i_get(cfg.network.wifi_enabled())) {
            wifi_power_on();
            msg_update_text_fmt("Bluetooth on - powering up the WiFi/BT chip");
            bt_say("Bluetooth turning on, powering up the Wi-Fi and Bluetooth chip");
        } else {
            msg_update_text_fmt("Bluetooth on");
            bt_say("Bluetooth turning on");
        }
        bt_ctl_request(true);
    } else {
        /* The loopbacks feed and read a headset that is about to
         * disappear. */
        bt_ptt_request(false);
        bt_audio_request(false);
        bt_ctl_request(false);
        msg_update_text_fmt("Bluetooth off");
        bt_say("Bluetooth off");
    }

    status_refresh();
    buttons_refresh(item);
}

static void audio_toggle_cb(button_data_t *item)
{
    bt_audio_state_t st = bt_audio_state();

    disarm_ptt();
    if (!require_bt_on()) {
        return;
    }

    if (st == BT_AUDIO_ON || st == BT_AUDIO_STARTING) {
        bt_audio_request(false);
        msg_update_text_fmt("Bluetooth audio off");
        bt_say("Listening audio off");
    } else {
        bt_audio_request(true);
        msg_update_text_fmt("Bluetooth audio on");
        bt_say("Listening audio turning on");
    }

    buttons_refresh(item);
}

static void connect_cb(button_data_t *item)
{
    bt_dev_t dev;
    char     name[48];
    bool     off;

    disarm_ptt();
    if (!require_bt_on() || !require_selection(&dev)) {
        return;
    }
    spoken_name(&dev, name, sizeof(name), &off);

    /* The outcome ("... connected", "Connect failed ...") comes from the
     * status timer once bt_dev's worker is done. */
    if (dev.connected) {
        bt_dev_disconnect(dev.addr);
        msg_update_text_fmt("Disconnecting %s", dev.name);
        bt_say("Disconnecting %s", name);
    } else {
        bt_dev_connect(dev.addr);
        msg_update_text_fmt("Connecting %s", dev.name);
        bt_say("Connecting %s", name);
    }

    buttons_refresh(item);
}

static void scan_cb(button_data_t *item)
{
    disarm_ptt();
    if (!require_bt_on()) {
        return;
    }
    if (bt_dev_scanning()) {
        bt_say("Already scanning");
        return;
    }

    bt_dev_scan();
    found_count = 0;
    msg_update_text_fmt("Scanning - pairing open for 3 min (a phone can pair now)");
    bt_say("Scanning, pairing open for 3 minutes");
    buttons_refresh(item);
}

/* Keys the transmitter with no announcement: speaking at that instant is
 * the hazard the Msg Voice Send arm/second-press pattern exists for, and
 * with listening off the speech would go out on the air. */
static void start_ptt(void)
{
    ptt_armed = false;
    bt_ptt_request(true);
    msg_update_text_fmt("PTT on - BT microphone");
}

static void ptt_cb(button_data_t *item)
{
    bt_ptt_state_t st = bt_ptt_state();

    if (st == BT_PTT_TX || st == BT_PTT_KEYING) {
        /* "P T T off, receiving" is said once it really is off. */
        ptt_armed = false;
        bt_ptt_request(false);
        msg_update_text_fmt("PTT off");
    } else {
        if (!require_bt_on()) {
            return;
        }
        if (ptt_armed || param_i_get(cfg.voice.mode()) == VOICE_OFF) {
            start_ptt();
        } else {
            ptt_armed = true;
            msg_update_text_fmt("PTT: press again to transmit");
            bt_say("Ready to transmit from the Bluetooth microphone, press again to start");
        }
    }

    buttons_refresh(item);
}

static void forget_cb(button_data_t *item)
{
    bt_dev_t dev;
    char     name[48];
    bool     off;

    (void)item;
    disarm_ptt();
    if (!require_selection(&dev)) {
        return;
    }
    spoken_name(&dev, name, sizeof(name), &off);
    msg_update_text_fmt("Hold to forget %s", dev.name);
    bt_say("Hold Forget to remove %s", name);
}

static void forget_hold_cb(button_data_t *item)
{
    bt_dev_t dev;
    char     name[48];
    bool     off;

    (void)item;
    disarm_ptt();
    if (!require_bt_on() || !require_selection(&dev)) {
        return;
    }
    spoken_name(&dev, name, sizeof(name), &off);
    /* The listening loopback may be playing into this very headset. */
    if (dev.audio) {
        bt_ptt_request(false);
        bt_audio_request(false);
    }
    bt_dev_forget(dev.addr);
    /* "<name> forgotten" once it is out of the list. */
    snprintf(forget_addr, sizeof(forget_addr), "%s", dev.addr);
    msg_update_text_fmt("Forgetting %s - pair again in pairing mode",
                        dev.name);
    bt_say("Forgetting %s, pair again in pairing mode", name);
}

static void udp_toggle_cb(button_data_t *item)
{
    disarm_ptt();
    ft8_udp_set_enabled(!udp_on);
    udp_on = ft8_udp_is_enabled();

    if (udp_on) {
        msg_update_text_fmt("UDP on -> %s:%u", ft8_udp_host(),
                            ft8_udp_port());
        bt_say("U D P on, sending to %s port %u", ft8_udp_host(), ft8_udp_port());
    } else {
        msg_update_text_fmt("UDP off");
        bt_say("U D P off");
    }

    status_refresh();
    buttons_refresh(item);
}

static void status_say_cb(button_data_t *item)
{
    char text[256];

    (void)item;
    disarm_ptt();
    status_summary(text, sizeof(text));
    bt_say("%s", text);
}

static void exit_cb(button_data_t *item)
{
    (void)item;
    dialog_destruct(&dialog);
}

/* ---- Dialog ------------------------------------------------------------ */

static void key_cb(lv_event_t *e)
{
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        case LV_KEY_ESC:
            dialog_destruct(&dialog);
            break;

        case LV_KEY_ENTER:
            connect_cb(&btn_connect);
            break;

        /* The knob. LVGL's own table handler has already moved the
         * highlight by now, but the first turn after it was cleared only
         * selects the top row and sends no VALUE_CHANGED: speak from here
         * too (announce_selection() skips a device already spoken). */
        case LV_KEY_LEFT:
        case LV_KEY_RIGHT:
        case LV_KEY_UP:
        case LV_KEY_DOWN:
            last_nav_ms = lv_tick_get();
            announce_selection(false);
            break;

        case KEY_VOL_LEFT_EDIT:
        case KEY_VOL_LEFT_SELECT:
            radio_change_vol(-1);
            break;

        case KEY_VOL_RIGHT_EDIT:
        case KEY_VOL_RIGHT_SELECT:
            radio_change_vol(1);
            break;
    }
}

static void construct_cb(lv_obj_t *parent)
{
    lv_obj_t *cont, *param_cont, *label;
    static lv_style_t style_val;

    dialog.obj = dialog_init(parent);

    udp_on = ft8_udp_is_enabled();
    /* Quiet: the window speaks its own opening summary below, which a
     * page-name announcement would race. */
    buttons_load_page_quiet(&btn_page_1);

    cont = lv_obj_create(dialog.obj);
    lv_obj_remove_style(cont, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(cont, DIALOG_WIDTH, DIALOG_HEIGHT);
    lv_obj_center(cont);
    lv_obj_set_flex_flow(cont, LV_FLEX_FLOW_ROW);

    param_cont = lv_obj_create(cont);
    lv_obj_remove_style(param_cont, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(param_cont, PARAMS_WIDTH, DIALOG_HEIGHT);
    lv_obj_set_flex_flow(param_cont, LV_FLEX_FLOW_COLUMN);

    lv_style_init(&style_val);
    lv_style_set_pad_bottom(&style_val, 14);
    lv_style_set_pad_left(&style_val, 10);

    label = lv_label_create(param_cont);
    lv_label_set_text(label, "Adapter:");
    label_adapter = lv_label_create(param_cont);
    lv_label_set_text(label_adapter, "Off");
    lv_obj_add_style(label_adapter, &style_val, 0);

    label = lv_label_create(param_cont);
    lv_label_set_text(label, "FT8 packets:");
    label_phone = lv_label_create(param_cont);
    lv_label_set_text(label_phone, "-");
    lv_obj_add_style(label_phone, &style_val, 0);

    label = lv_label_create(param_cont);
    lv_label_set_text(label, "Listening audio:");
    label_audio = lv_label_create(param_cont);
    lv_label_set_text(label_audio, "Off");
    lv_obj_add_style(label_audio, &style_val, 0);

    table = lv_table_create(cont);
    lv_table_set_col_cnt(table, 1);
    lv_table_set_row_cnt(table, 1);
    lv_table_set_col_width(table, 0, DIALOG_WIDTH - PARAMS_WIDTH - 2);
    lv_obj_set_height(table, DIALOG_HEIGHT);
    lv_obj_set_flex_grow(table, 1);

    lv_obj_remove_style(table, NULL, LV_PART_MAIN | LV_PART_ITEMS |
                        LV_STATE_ANY);
    lv_obj_set_style_bg_opa(table, LV_OPA_50, LV_PART_MAIN);
    lv_obj_set_style_bg_color(table, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(table, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(table, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_border_opa(table, 128, LV_PART_MAIN);

    lv_obj_set_style_border_width(table, 0, LV_PART_ITEMS);
    lv_obj_set_style_text_color(table, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_bg_color(table, lv_color_white(),
                              LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_bg_opa(table, LV_OPA_30,
                            LV_PART_ITEMS | LV_STATE_EDITED);
    lv_obj_set_style_pad_top(table, 8, LV_PART_ITEMS);
    lv_obj_set_style_pad_bottom(table, 8, LV_PART_ITEMS);
    lv_obj_set_style_pad_left(table, 5, LV_PART_ITEMS);

    lv_obj_add_event_cb(table, key_cb, LV_EVENT_KEY, NULL);
    lv_obj_add_event_cb(table, table_value_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_group_add_obj(keyboard_group, table);
    lv_group_set_editing(keyboard_group, true);

    lv_table_set_cell_value(table, 0, 0, " ");
    row_count = 0;

    tick = 0;
    last_activity[0] = '\0';
    bt_dev_init();
    bt_audio_init();

    if (bt_ctl_state() == BT_CTL_ON) {
        bt_dev_refresh();
        bt_audio_poll();
    }

    table_refresh();
    status_refresh();
    speech_reset();

    {
        char text[256];

        status_summary(text, sizeof(text));
        bt_say("Bluetooth window, %s", text);
    }

    timer_status = lv_timer_create(status_timer_cb, TICK_MS, NULL);
}

static void destruct_cb(void)
{
    if (timer_status) {
        lv_timer_del(timer_status);
        timer_status = NULL;
    }

    /* Never leave the transmitter keyed with nothing on screen to
     * unkey it. */
    bt_ptt_request(false);
    ptt_armed = false;

    /*
     * The adapter, the SPP server and the listening loopback keep running
     * after the window closes - that is the point of having On and Off.
     * Only the widgets go.
     */
    table = NULL;
    label_adapter = NULL;
    label_phone = NULL;
    label_audio = NULL;
}

/* ---- Mic gain ----------------------------------------------------------- */

static const char *mic_gain_label_getter(void)
{
    static char buf[24];

    snprintf(buf, sizeof(buf), "Mic gain\n%+d dB", bt_audio_mic_gain());
    return buf;
}

static void mic_gain_step(button_data_t *item, int step)
{
    int db = bt_audio_set_mic_gain(bt_audio_mic_gain() + step);

    disarm_ptt();
    msg_update_text_fmt("Mic gain %+d dB (press +1, hold -1)", db);
    bt_say("Headset mic gain %s%d decibels", db > 0 ? "plus " : db < 0 ? "minus " : "",
           db < 0 ? -db : db);
    buttons_refresh(item);
}

static void mic_gain_up_cb(button_data_t *item)
{
    mic_gain_step(item, 1);
}

static void mic_gain_down_cb(button_data_t *item)
{
    mic_gain_step(item, -1);
}
