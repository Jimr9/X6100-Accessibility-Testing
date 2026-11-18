/*
 *  SPDX-License-Identifier: LGPL-2.1-or-later
 *
 *  Xiegu X6100 LVGL GUI - speaks what the knob lands on (accessibility)
 *
 *  The older windows announce each of their controls themselves
 *  (dialog_item_voice() in dialog.c). The newer app windows (JS8, WeFax,
 *  SSTV) build many popups and lists of their own and announce nothing, so
 *  instead of wiring every one of them, this listens to the keyboard
 *  group's focus changes and describes whatever got the focus - but only
 *  while the app speech helper is on (voice_auto_active()), which is only
 *  while one of those windows is open. Everywhere else the radio's own
 *  per-control announcements stay the only speech, so nothing is said
 *  twice.
 */

#include "voice_focus.h"

#include <stdio.h>
#include <string.h>

#include "ui_list.h"
#include "voice.h"

static void append(char *buf, size_t size, const char *txt) {
    if (!txt || !txt[0] || size < 4) {
        return;
    }
    size_t len = strlen(buf);

    if (len > 0 && len + 2 < size) {
        buf[len++] = ',';
        buf[len++] = ' ';
        buf[len] = '\0';
    }
    if (len + 1 < size) {
        strncat(buf, txt, size - len - 1);
    }
}

/* The text of the labels inside `obj` (a list button's icon and text, a
 * button with a caption), in order. */
static void collect_labels(lv_obj_t *obj, char *buf, size_t size, int depth) {
    uint32_t n = lv_obj_get_child_cnt(obj);

    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(obj, i);

        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) {
            continue;
        }
        if (lv_obj_check_type(c, &lv_label_class)) {
            append(buf, size, lv_label_get_text(c));
        } else if (depth < 2) {
            collect_labels(c, buf, size, depth + 1);
        }
    }
}

/* "3 of 7" for an entry of an LVGL list. */
static void list_position(lv_obj_t *obj, char *buf, size_t size) {
    lv_obj_t *parent = lv_obj_get_parent(obj);

    if (!parent || !lv_obj_check_type(parent, &lv_list_class)) {
        return;
    }

    int      index = 0;
    int      total = 0;
    uint32_t n = lv_obj_get_child_cnt(parent);

    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(parent, i);

        if (lv_obj_check_type(c, &lv_list_btn_class)) {
            total++;
            if (c == obj) {
                index = total;
            }
        }
    }
    if (index > 0 && total > 1) {
        char pos[24];

        snprintf(pos, sizeof(pos), "%d of %d", index, total);
        append(buf, size, pos);
    }
}

bool voice_focus_describe(lv_obj_t *obj, char *buf, size_t size) {
    if (!obj || !buf || size == 0) {
        return false;
    }
    buf[0] = '\0';

    if (ui_list_describe(obj, buf, size)) {
        return buf[0] != '\0';
    }

    if (lv_obj_check_type(obj, &lv_textarea_class)) {
        const char *text = lv_textarea_get_text(obj);

        append(buf, size, "Edit text");
        if (text && text[0]) {
            append(buf, size, text);
        } else {
            const char *hint = lv_textarea_get_placeholder_text(obj);

            append(buf, size, hint && hint[0] ? hint : "empty");
        }
    } else if (lv_obj_check_type(obj, &lv_dropdown_class)) {
        char sel[64];

        lv_dropdown_get_selected_str(obj, sel, sizeof(sel));
        append(buf, size, sel);
    } else if (lv_obj_check_type(obj, &lv_switch_class)) {
        append(buf, size, lv_obj_has_state(obj, LV_STATE_CHECKED) ? "on" : "off");
    } else if (lv_obj_check_type(obj, &lv_checkbox_class)) {
        append(buf, size, lv_checkbox_get_text(obj));
        append(buf, size, lv_obj_has_state(obj, LV_STATE_CHECKED) ? "checked" : "not checked");
    } else if (lv_obj_check_type(obj, &lv_slider_class)) {
        char val[16];

        snprintf(val, sizeof(val), "%d", (int)lv_slider_get_value(obj));
        append(buf, size, val);
    } else if (lv_obj_check_type(obj, &lv_table_class)) {
        uint16_t row;
        uint16_t col;

        lv_table_get_selected_cell(obj, &row, &col);
        if (row != LV_TABLE_CELL_NONE && row < lv_table_get_row_cnt(obj) && col < lv_table_get_col_cnt(obj)) {
            append(buf, size, lv_table_get_cell_value(obj, row, col));
        }
    } else if (lv_obj_check_type(obj, &lv_label_class)) {
        append(buf, size, lv_label_get_text(obj));
    } else {
        collect_labels(obj, buf, size, 0);
        list_position(obj, buf, size);
    }
    return buf[0] != '\0';
}

static char        hint[200];
static lv_timer_t *hint_timer;

static void hint_clear(void) {
    hint[0] = '\0';
    if (hint_timer) {
        lv_timer_del(hint_timer);
        hint_timer = NULL;
    }
}

/* No focus change came with the hint: say it alone. */
static void hint_timer_cb(lv_timer_t *t) {
    (void)t;
    hint_timer = NULL;
    if (hint[0] && voice_auto_active()) {
        voice_say_clean(hint, false);
    }
    hint[0] = '\0';
}

void voice_focus_hint(const char *text) {
    if (!text || !text[0] || !voice_auto_active()) {
        return;
    }
    snprintf(hint, sizeof(hint), "%s", text);
    if (!hint_timer) {
        hint_timer = lv_timer_create(hint_timer_cb, 0, NULL);
        lv_timer_set_repeat_count(hint_timer, 1);
    }
}

static void focus_cb(lv_group_t *group) {
    if (!voice_auto_active()) {
        hint_clear();
        return;
    }

    lv_obj_t *obj = lv_group_get_focused(group);
    char      buf[256];
    char      text[460];

    if (!voice_focus_describe(obj, buf, sizeof(buf))) {
        buf[0] = '\0';
    }
    if (hint[0]) {
        snprintf(text, sizeof(text), "%s%s%s", hint, buf[0] ? ". " : "", buf);
        hint_clear();
        voice_say_clean(text, false);
    } else if (buf[0]) {
        voice_say_clean(buf, true);
    }
}

void voice_focus_init(lv_group_t *group) {
    lv_group_set_focus_cb(group, focus_cb);
}
