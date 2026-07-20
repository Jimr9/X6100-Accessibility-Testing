/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "events.h"
#include "radio.h"
#include "keyboard.h"
#include "textarea_window.h"
#include "styles.h"
#include "voice.h"

#include <string.h>

static lv_obj_t             *window = NULL;
static lv_obj_t             *label = NULL;
static lv_obj_t             *text = NULL;
static lv_obj_t             *keyboard = NULL;

static textarea_window_cb_t ok_cb = NULL;
static textarea_window_cb_t cancel_cb = NULL;

static void ok() {
    if (ok_cb) {
        if(ok_cb()) {
            ok_cb = NULL;
            textarea_window_close();
        }
    }
 }

static void cancel() {
    if (cancel_cb) {
        cancel_cb();
        cancel_cb = NULL;
    }

    voice_say_text_fmt("Cancelled");
    textarea_window_close();
}

/**
 * Speak a key's text, using a readable word for the icon-glyph special
 * keys (backspace/enter/space/shift/mode-switch aren't plain readable
 * text as button labels).
 */
static void speak_keyboard_btn_text(const char *txt) {
    if (txt == NULL || txt[0] == '\0') {
        return;
    }

    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) {
        voice_delay_say_text_fmt("backspace");
    } else if (strcmp(txt, LV_SYMBOL_NEW_LINE) == 0 || strcmp(txt, LV_SYMBOL_OK) == 0) {
        voice_delay_say_text_fmt("enter");
    } else if (strcmp(txt, LV_SYMBOL_KEYBOARD) == 0) {
        voice_delay_say_text_fmt("close keyboard");
    } else if (strcmp(txt, LV_SYMBOL_LEFT) == 0) {
        voice_delay_say_text_fmt("left");
    } else if (strcmp(txt, LV_SYMBOL_RIGHT) == 0) {
        voice_delay_say_text_fmt("right");
    } else if (strcmp(txt, " ") == 0) {
        voice_delay_say_text_fmt("space");
    } else if (strcmp(txt, "1#") == 0) {
        voice_delay_say_text_fmt("numbers");
    } else if (strcmp(txt, "ABC") == 0) {
        voice_delay_say_text_fmt("uppercase");
    } else if (strcmp(txt, "abc") == 0) {
        voice_delay_say_text_fmt("lowercase");
    } else {
        voice_delay_say_text_fmt("%s", txt);
    }
}

/**
 * Speak the character just typed/removed on the on-screen keyboard, the
 * same way dialog_freq.c speaks each typed digit - so encoder-driven
 * (non-touch) text entry gives feedback instead of being silent.
 */
static void keyboard_char_voice_cb(lv_event_t * e) {
    lv_obj_t *obj    = lv_event_get_target(e);
    uint16_t  btn_id = lv_btnmatrix_get_selected_btn(obj);
    if (btn_id == LV_BTNMATRIX_BTN_NONE) {
        return;
    }
    speak_keyboard_btn_text(lv_btnmatrix_get_btn_text(obj, btn_id));
}

/**
 * Speak the key the encoder just moved onto, BEFORE it's pressed/typed.
 * Rotating the encoder only sends LV_EVENT_KEY to move the highlighted
 * button (btn_id_sel) inside the matrix - it never fires VALUE_CHANGED,
 * which only fires on an actual press. Without this, there is no way to
 * know which letter is about to be typed until after committing it.
 */
static uint16_t last_nav_btn_id = LV_BTNMATRIX_BTN_NONE;

static void keyboard_nav_voice_cb(lv_event_t * e) {
    lv_obj_t *obj    = lv_event_get_target(e);
    uint16_t  btn_id = lv_btnmatrix_get_selected_btn(obj);
    if (btn_id == LV_BTNMATRIX_BTN_NONE || btn_id == last_nav_btn_id) {
        return;
    }
    last_nav_btn_id = btn_id;
    speak_keyboard_btn_text(lv_btnmatrix_get_btn_text(obj, btn_id));
}

static void text_cb(lv_event_t * e) {
    uint32_t key = *((uint32_t *)lv_event_get_param(e));

    switch (key) {
        case HKEY_FINP:
        case LV_KEY_ENTER:
            ok();
            break;

        case LV_KEY_ESC:
            cancel();
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


static void keyboard_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    uint32_t        *key = ((uint32_t *)lv_event_get_param(e));

    switch (code) {
        case LV_EVENT_KEY:
            switch (*key) {
                case KEY_VOL_LEFT_EDIT:
                case KEY_VOL_LEFT_SELECT:
                    radio_change_vol(-1);
                    break;

                case KEY_VOL_RIGHT_EDIT:
                case KEY_VOL_RIGHT_SELECT:
                    radio_change_vol(1);
                    break;
            }
            break;

        case LV_EVENT_READY:
            ok();
            break;

        case LV_EVENT_CANCEL:
            cancel();
            break;
    }
}

lv_obj_t * textarea_window_open(textarea_window_cb_t ok, textarea_window_cb_t cancel) {
    ok_cb = ok;
    cancel_cb = cancel;

    window = lv_obj_create(lv_scr_act());

    lv_obj_remove_style_all(window);

    lv_obj_add_style(window, &msg_style, 0);
    lv_obj_clear_flag(window, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_y(window, 80);

    lv_obj_t * obj = lv_obj_create(window);
    lv_obj_set_layout(obj, LV_LAYOUT_FLEX);
    lv_obj_remove_style(obj, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_height(obj, 35);
    lv_obj_set_width(obj, 560);
    lv_obj_center(obj);
    lv_obj_set_flex_flow(obj, LV_FLEX_FLOW_ROW);

    lv_obj_t * item_wrapper;
    item_wrapper = lv_obj_create(obj);
    lv_obj_remove_style(item_wrapper, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(item_wrapper, LV_SIZE_CONTENT, LV_PCT(100));

    label = lv_label_create(item_wrapper);
    lv_obj_set_style_text_font(label, &sony_36, 0);
    lv_label_set_text(label, "");
    lv_obj_align_to(label, item_wrapper, LV_ALIGN_LEFT_MID, 0, 0);

    text = lv_textarea_create(obj);

    lv_obj_remove_style(text, NULL, LV_STATE_ANY | LV_PART_MAIN);
    lv_obj_set_size(text, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    lv_obj_set_style_text_color(text, lv_color_white(), 0);
    lv_obj_set_style_bg_color(text, lv_color_white(), LV_PART_CURSOR);
    lv_obj_set_style_bg_opa(text, LV_OPA_80, LV_PART_CURSOR);

    lv_textarea_set_one_line(text, true);
    lv_textarea_set_max_length(text, 64);

    lv_obj_clear_flag(text, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_text_font(text, &sony_44, 0);
    lv_obj_set_flex_grow(text, 1);

    if (ok || cancel) {
        lv_obj_add_event_cb(text, text_cb, LV_EVENT_KEY, NULL);
    }

    if (!keyboard_ready()) {
        keyboard = lv_keyboard_create(lv_scr_act());
        last_nav_btn_id = LV_BTNMATRIX_BTN_NONE;

        lv_keyboard_set_textarea(keyboard, text);
        lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_TEXT_UPPER);
        lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_READY, NULL);
        lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_CANCEL, NULL);
        lv_obj_add_event_cb(keyboard, keyboard_cb, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(keyboard, keyboard_nav_voice_cb, LV_EVENT_KEY, NULL);
        lv_obj_add_event_cb(keyboard, keyboard_char_voice_cb, LV_EVENT_VALUE_CHANGED, NULL);

        lv_obj_set_style_bg_color(keyboard, bg_color, LV_PART_MAIN);
        lv_obj_add_style(keyboard, &dialog_item_focus_style, LV_STATE_FOCUSED | LV_PART_ITEMS);

        lv_group_add_obj(keyboard_group, keyboard);
    } else {
        keyboard = NULL;
    }

    lv_group_add_obj(keyboard_group, text);

    return window;
}

lv_obj_t *textarea_window_open_w_label(textarea_window_cb_t ok_cb, textarea_window_cb_t cancel_cb, const char *text) {
    lv_obj_t * obj = textarea_window_open(ok_cb, cancel_cb);
    lv_label_set_text(label, text);
    return obj;
}

void textarea_window_close() {
    if (keyboard) {
        lv_obj_del(keyboard);
        keyboard = NULL;
    }

    if (window) {
        lv_obj_del(window);
        window = NULL;
    }
}

const char* textarea_window_get() {
    return lv_textarea_get_text(text);
}

void textarea_window_set(const char *val) {
    lv_textarea_set_text(text, val);
}

lv_obj_t * textarea_window_text() {
    return text;
}
