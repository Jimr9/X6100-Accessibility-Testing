/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI
 *
 *  Copyright (c) 2022-2023 Belousov Oleg aka R1CBU
 */

#include "dialog.h"

#include "styles.h"
#include "main_screen.h"
#include "keyboard.h"
#include "events.h"
#include "waterfall.h"
#include "knobs.h"
#include "voice.h"

static lv_obj_t     *obj;
static dialog_t     *current_dialog = NULL;

void dialog_construct(dialog_t *dialog, lv_obj_t *parent) {
    // Switching to a different app (e.g. pressing another page's F-key)
    // while a dialog is still open used to leave the old one running
    // silently underneath the new one - both then fight over the shared
    // keyboard_group/focus state, which is what was causing the Wi-Fi
    // list to go silent and eventually freeze all speech after leaving
    // Callsign open and jumping straight to Wi-Fi. Close whatever's
    // currently open first, the same way Escape already does.
    if (current_dialog && current_dialog->run && current_dialog != dialog) {
        voice_say_text_fmt("Closing previous window");
        dialog_destruct();
    }

    if (dialog && !dialog->run) {
        knobs_display(false);
        waterfall_refresh_period_set(2);
        main_screen_keys_enable(false);
        dialog->prev_page = buttons_get_cur_page();
        buttons_unload_page();
        if (dialog->btn_page) {
            buttons_load_page(dialog->btn_page);
        }
        dialog->construct_cb(parent);

        dialog->run = true;
    }

    current_dialog = dialog;
}

static void dialog_destruct_impl(bool restore_page) {
    if (current_dialog && current_dialog->run) {
        knobs_display(true);
        waterfall_refresh_reset();
        current_dialog->run = false;

        if (current_dialog->destruct_cb) {
            current_dialog->destruct_cb();
        }

        if (current_dialog->obj) {
            lv_obj_del(current_dialog->obj);
        }
        buttons_unload_page();
        if (restore_page && current_dialog->prev_page) {
            buttons_load_page(current_dialog->prev_page);
        }
        main_screen_keys_enable(true);
        current_dialog = NULL;
    }
}

void dialog_destruct() {
    dialog_destruct_impl(true);
}

void dialog_destruct_for_group_switch() {
    // Called instead of dialog_destruct() when a dialog is closing because
    // a group-select button (GEN/APP/KEY/DFN/DFL) was pressed. That press
    // immediately calls buttons_load_page_group() right after this, which
    // decides the real target page itself (favoring the group's
    // last-visited page). Restoring and announcing the dialog's own saved
    // prev_page here first isn't just redundant - it races the very next
    // page load/announcement that buttons_load_page_group() triggers, so
    // which of the two you actually hear is down to thread-scheduling
    // timing, and it doesn't always match the page that ends up loaded.
    dialog_destruct_impl(false);
}

void dialog_send(lv_event_code_t event_code, void *param) {
    if (dialog_is_run()) {
        event_send(current_dialog->obj, event_code, param);
    }
}

bool dialog_key(dialog_t *dialog, lv_event_t * e) {
    if (dialog && dialog->key_cb && dialog->run) {
        dialog->key_cb(e);
        return true;
    }

    return false;
}

bool dialog_is_run() {
    return (current_dialog != NULL) && current_dialog->run;
}

bool dialog_type_is_run(dialog_t *dialog) {
    return (current_dialog == dialog) && current_dialog->run;
}

lv_obj_t * dialog_init(lv_obj_t *parent) {
    obj = lv_obj_create(parent);

    lv_obj_remove_style_all(obj);
    lv_obj_add_style(obj, &dialog_style, 0);

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    return obj;
}

void dialog_item(dialog_t *dialog, lv_obj_t *obj) {
    lv_obj_add_style(obj, &dialog_item_style, LV_STATE_DEFAULT);
    lv_obj_add_style(obj, &dialog_item_focus_style, LV_STATE_FOCUSED);
    lv_obj_add_style(obj, &dialog_item_edited_style, LV_STATE_EDITED);

    lv_obj_set_style_bg_opa(obj, LV_OPA_TRANSP, LV_PART_CURSOR);
    lv_obj_set_style_text_color(obj, lv_color_white(), LV_PART_CURSOR);
    lv_obj_set_style_text_color(obj, lv_color_black(), LV_PART_CURSOR | LV_STATE_FOCUSED);
    lv_obj_set_style_bg_opa(obj, 128, LV_PART_CURSOR | LV_STATE_EDITED);

    lv_obj_set_style_border_width(obj, 2, LV_STATE_FOCUS_KEY | LV_PART_INDICATOR);
    lv_obj_set_style_border_color(obj, lv_color_white(), LV_STATE_FOCUS_KEY | LV_PART_INDICATOR);

    lv_obj_add_flag(obj, LV_OBJ_FLAG_SCROLL_ON_FOCUS);

    lv_group_add_obj(keyboard_group, obj);

    if (dialog->key_cb) {
        lv_obj_add_event_cb(obj, dialog->key_cb, LV_EVENT_KEY, NULL);
    }
}

/**
 * Announce a dialog control's name and current value when it receives
 * focus, so encoder/keypad navigation is as informative as touch. Reuses
 * the same voice_say_* functions/convention used everywhere else in the
 * firmware - not a separate accessibility system.
 */
static void focus_voice_cb(lv_event_t *e) {
    const char *name = (const char *)lv_event_get_user_data(e);
    if (!name) {
        return;
    }
    lv_obj_t *obj = lv_event_get_target(e);

    if (lv_obj_check_type(obj, &lv_switch_class)) {
        voice_say_bool(name, lv_obj_has_state(obj, LV_STATE_CHECKED));
    } else if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        char buf[64];
        lv_dropdown_get_selected_str(obj, buf, sizeof(buf));
        voice_say_text(name, buf);
    } else if (lv_obj_check_type(obj, &lv_spinbox_class)) {
        voice_say_int(name, lv_spinbox_get_value(obj));
    } else if (lv_obj_check_type(obj, &lv_slider_class)) {
        voice_say_int(name, lv_slider_get_value(obj));
    } else {
        voice_say_text_fmt("%s", name);
    }
}

void dialog_item_voice(dialog_t *dialog, lv_obj_t *obj, const char *name) {
    dialog_item(dialog, obj);
    lv_obj_add_event_cb(obj, focus_voice_cb, LV_EVENT_FOCUSED, (void *)name);
}

bool dialog_need_audio() {
    return dialog_is_run() && current_dialog->audio_cb;
}

void dialog_audio_samples(unsigned int n, float *samples) {
    if (dialog_need_audio()) {
        current_dialog->audio_cb(n, samples);
    }
}

void dialog_rotary(int32_t diff) {
    if (dialog_is_run() && current_dialog->rotary_cb) {
        current_dialog->rotary_cb(diff);
    }
}
