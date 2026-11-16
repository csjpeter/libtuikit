/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* The setting dialog: a description, one input, a hint, OK and Cancel. */

#include "tuikit/settingdialog.h"
#include "tuikit/text.h"
#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define SET_MIN_W  40      /* the width follows the text between these */
#define SET_MAX_W  70
#define SET_TINY_W 12      /* below this the box is not worth opening */

static char *dup_or_empty(const char *s) {
    size_t n = s ? strlen(s) : 0;
    char *p = malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

int tui_settingdialog_init(tui_settingdialog *d, const char *title, const char *text,
                           const char *hint, char *buf, size_t size, const char *initial) {
    if (!d || !buf || size == 0) {
        errno = EINVAL;
        return -1;
    }
    memset(d, 0, sizeof(*d));
    d->state = TUI_DIALOG_OPEN;
    d->focus = TUI_SETTING_FIELD;
    d->frame = tui_scheme_style(TUI_ROLE_FRAME);
    d->fill = tui_scheme_style(TUI_ROLE_TEXT);
    d->focus_style = tui_selection_style();
    tui_textview_init(&d->body);
    tui_lineedit_init(&d->edit, buf, size, initial ? initial : "");
    d->title = dup_or_empty(title);
    d->text = dup_or_empty(text);
    d->hint = dup_or_empty(hint);
    d->orig = dup_or_empty(buf);   /* what the editor kept of @p initial */
    if (!d->title || !d->text || !d->hint || !d->orig ||
        tui_textview_set_text(&d->body, d->text) != 0) {
        tui_settingdialog_free(d);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

void tui_settingdialog_free(tui_settingdialog *d) {
    if (!d) return;
    free(d->title);
    free(d->text);
    free(d->hint);
    free(d->orig);
    d->title = d->text = d->hint = d->orig = NULL;
    tui_textview_free(&d->body);
    tui_lineedit_free(&d->edit);
}

static tui_dialog_state cancel(tui_settingdialog *d) {
    tui_lineedit_set_text(&d->edit, d->orig ? d->orig : "");
    d->state = TUI_DIALOG_CANCELLED;
    return d->state;
}

/* OK: the value goes through the validator; a refusal keeps the dialog
 * open with the reason shown and the focus on the input. */
static tui_dialog_state press_ok(tui_settingdialog *d) {
    if (d->validate) {
        char err[sizeof d->error];
        err[0] = '\0';
        if (d->validate(d->user, d->edit.buf, err, sizeof err) != 0) {
            if (!err[0]) strcpy(err, "Invalid value");
            memcpy(d->error, err, sizeof d->error);
            d->error[sizeof d->error - 1] = '\0';
            d->focus = TUI_SETTING_FIELD;
            return d->state;
        }
    }
    d->error[0] = '\0';
    d->state = TUI_DIALOG_OK;
    return d->state;
}

tui_dialog_state tui_settingdialog_handle(tui_settingdialog *d, const tui_event *ev) {
    if (!d || !ev) return d ? d->state : TUI_DIALOG_CANCELLED;
    if (d->state != TUI_DIALOG_OPEN) return d->state;
    int is_key = ev->type == TUI_EV_KEY;
    if (is_key && (ev->key == TUI_KEY_ESC || TUI_EV_IS_CTRL(ev, 'c'))) return cancel(d);
    int tab = is_key && ev->key == TUI_KEY_TAB;
    int back = tab && (ev->mods & TUI_MOD_SHIFT);

    if (tab) {
        /* The ring: input, OK, Cancel. */
        int n = (int)d->focus + (back ? 2 : 1);
        d->focus = (tui_setting_focus)(n % 3);
        return d->state;
    }
    if (d->focus == TUI_SETTING_FIELD) {
        size_t before = d->edit.len;
        tui_edit_result r = tui_lineedit_handle(&d->edit, ev);
        if (r == TUI_EDIT_ACCEPT) return press_ok(d);
        if (r == TUI_EDIT_CANCEL) return cancel(d);
        if (r == TUI_EDIT_CHANGED && d->error[0] && d->edit.len != before) d->error[0] = '\0';
        return d->state;
    }
    if (is_key && ev->key == TUI_KEY_ENTER)
        return d->focus == TUI_SETTING_OK ? press_ok(d) : cancel(d);
    if (is_key && (ev->key == TUI_KEY_LEFT || ev->key == TUI_KEY_RIGHT))
        d->focus = d->focus == TUI_SETTING_OK ? TUI_SETTING_CANCEL : TUI_SETTING_OK;
    else {
        int k = tui__nav_key(ev);
        if (k == TUI_KEY_UP || k == TUI_KEY_DOWN || k == TUI_KEY_PGUP || k == TUI_KEY_PGDN)
            tui_textview_handle(&d->body, ev);
    }
    return d->state;
}

void tui_settingdialog_render(tui_settingdialog *d, tui_canvas c) {
    if (!d || !c.screen) return;
    int avail_h = tui_canvas_rows(c) - 2;
    int want = 30, hw = d->hint[0] ? tui_str_width(d->hint) : 0;
    const char *s = d->text;
    while (*s) {                                  /* the widest line of the description */
        const char *nl = strchr(s, '\n');
        size_t n = nl ? (size_t)(nl - s) : strlen(s);
        int w = tui_strn_width(s, n);
        if (w > want) want = w;
        s = nl ? nl + 1 : s + n;
    }
    int tw = tui_str_width(d->title) + 4;
    if (tw > want) want = tw;
    if (hw > want) want = hw;
    int inner_w = want < SET_MIN_W ? SET_MIN_W : want > SET_MAX_W ? SET_MAX_W : want;
    if (inner_w > tui_canvas_cols(c) - 8) inner_w = tui_canvas_cols(c) - 8;
    if (inner_w < 1) inner_w = 1;

    int lines = d->text[0] ? tui_textview_wrap(&d->body, inner_w) : 0;
    if (lines < 0) lines = 0;
    /* Blank, description, blank, input, hint, error, blank, buttons. */
    int hint_row = d->hint[0] ? 1 : 0;
    int below = (lines > 0 ? 1 : 0) + 1 + hint_row + 1 + 1 + 1;
    int h = 2 + 1 + lines + below;
    int min_h = 2 + 1 + (lines > 0 ? 1 : 0) + below;
    if (inner_w < SET_TINY_W || avail_h < min_h) {
        tui_canvas_put_aligned(tui_canvas_sub(c, tui_rect_make(tui_canvas_rows(c) - 1, 0, 1,
                                                               tui_canvas_cols(c))),
                               0, "Terminal too small for this dialog", TUI_ALIGN_LEFT,
                               tui_scheme_style(TUI_ROLE_WARNING));
        tui_screen_set_cursor(c.screen, 0, 0, 0);
        return;
    }
    if (h > avail_h) h = avail_h;
    int text_rows = h - 2 - 1 - below;
    tui_rect box = tui_rect_center(tui_canvas_rect(c), h, inner_w + 4);
    box.row = tui_canvas_rect(c).row + (tui_canvas_rows(c) - h) * 2 / 5;
    tui_canvas_fill(c, box, d->fill);
    tui_canvas_box(c, box, d->title, d->frame);
    tui_rect content = tui_rect_make(box.row + 1, box.col + 2, box.rows - 2, box.cols - 4);
    tui_canvas cc = tui_canvas_sub(c, content);
    tui_canvas wide = tui_canvas_sub(c, tui_rect_make(content.row, content.col, content.rows,
                                                      content.cols + 1));
    if (text_rows > 0 && lines > 0)
        tui_textview_render(&d->body, tui_canvas_sub(wide, tui_rect_make(1, 0, text_rows, inner_w + 1)),
                            d->fill);

    int r = 1 + (text_rows > 0 ? text_rows : 0) + (lines > 0 ? 1 : 0);
    tui_screen_set_cursor(c.screen, 0, 0, 0);
    tui_canvas field = tui_canvas_sub(cc, tui_rect_make(r, 0, 1, inner_w));
    tui_lineedit_render(&d->edit, field, NULL,
                        tui__input_style(tui_scheme_style(TUI_ROLE_INPUT), d->focus == TUI_SETTING_FIELD));
    if (d->focus != TUI_SETTING_FIELD) tui_screen_set_cursor(c.screen, 0, 0, 0);
    r++;
    if (hint_row) {
        tui_style hs = d->fill;
        hs.attrs = (uint16_t)(hs.attrs | TUI_ATTR_DIM);
        tui_canvas_put_aligned(tui_canvas_sub(cc, tui_rect_make(r, 0, 1, inner_w)), 0, d->hint,
                               TUI_ALIGN_LEFT, hs);
        r++;
    }
    if (d->error[0])
        tui_canvas_put_aligned(tui_canvas_sub(cc, tui_rect_make(r, 0, 1, inner_w)), 0, d->error,
                               TUI_ALIGN_LEFT, tui_scheme_style(TUI_ROLE_WARNING));
    tui_canvas last = tui_canvas_sub(cc, tui_rect_make(tui_canvas_rows(cc) - 1, 0, 1, inner_w));
    /* Two buttons, two cells apart, together in the middle. */
    int start = (tui_canvas_cols(last) - 18) / 2;
    if (start < 0) start = 0;
    tui_canvas_put(last, 0, start, "[ OK ]",
                   d->focus == TUI_SETTING_OK ? tui__button_focus(d->focus_style) : d->fill);
    tui_canvas_put(last, 0, start + 8, "[ Cancel ]",
                   d->focus == TUI_SETTING_CANCEL ? tui__button_focus(d->focus_style) : d->fill);
}
