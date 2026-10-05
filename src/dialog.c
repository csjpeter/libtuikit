/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

#include "tuikit/dialog.h"
#include "tuikit/text.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

static char *dup_or_empty(const char *s) {
    size_t n = s ? strlen(s) : 0;
    char *p = malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static int setup(tui_dialog *d, tui_dialog_kind kind, const char *title, const char *text) {
    if (!d) {
        errno = EINVAL;
        return -1;
    }
    memset(d, 0, sizeof(*d));
    d->kind = kind;
    d->state = TUI_DIALOG_OPEN;
    d->frame = TUI_STYLE_DEFAULT;
    d->fill = TUI_STYLE_DEFAULT;
    d->focus = tui_style_attrs(TUI_ATTR_REVERSE);
    tui_textview_init(&d->body);
    tui_listview_init(&d->list);
    d->title = dup_or_empty(title);
    d->text = dup_or_empty(text);
    if (!d->title || !d->text || tui_textview_set_text(&d->body, d->text) != 0) {
        tui_dialog_free(d);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int tui_dialog_message(tui_dialog *d, const char *title, const char *text) {
    return setup(d, TUI_DIALOG_MESSAGE, title, text);
}

int tui_dialog_confirm(tui_dialog *d, const char *title, const char *text, int default_yes) {
    if (setup(d, TUI_DIALOG_CONFIRM, title, text) != 0) return -1;
    d->answer = default_yes ? 1 : 0;
    return 0;
}

int tui_dialog_prompt(tui_dialog *d, const char *title, const char *text,
                      char *buf, size_t size, const char *initial) {
    if (!buf || size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (setup(d, TUI_DIALOG_PROMPT, title, text) != 0) return -1;
    tui_lineedit_init(&d->edit, buf, size, initial);
    return 0;
}

int tui_dialog_menu(tui_dialog *d, const char *title, const char *const *items, int n,
                    int selected) {
    if ((!items && n > 0) || n < 0) {
        errno = EINVAL;
        return -1;
    }
    if (setup(d, TUI_DIALOG_MENU, title, NULL) != 0) return -1;
    d->items = items;
    d->nitems = n;
    tui_listview_set_count(&d->list, n);
    tui_listview_select(&d->list, selected);
    d->answer = d->list.selected;
    return 0;
}

int tui_dialog_checklist(tui_dialog *d, const char *title, const char *const *items,
                         int n, const unsigned char *initial, int selected) {
    if (tui_dialog_menu(d, title, items, n, selected) != 0) return -1;
    d->kind = TUI_DIALOG_CHECKLIST;
    d->checked = calloc(n > 0 ? (size_t)n : 1, 1);
    if (!d->checked) {
        tui_dialog_free(d);
        errno = ENOMEM;
        return -1;
    }
    for (int i = 0; initial && i < n; i++) d->checked[i] = initial[i] ? 1 : 0;
    return 0;
}

int tui_dialog_is_checked(const tui_dialog *d, int i) {
    if (!d || !d->checked || i < 0 || i >= d->nitems) return 0;
    return d->checked[i];
}

void tui_dialog_free(tui_dialog *d) {
    if (!d) return;
    free(d->checked);
    d->checked = NULL;
    free(d->title);
    free(d->text);
    d->title = d->text = NULL;
    tui_textview_free(&d->body);
    tui_lineedit_free(&d->edit);
}

static tui_dialog_state close_with(tui_dialog *d, tui_dialog_state s) {
    d->state = s;
    return s;
}

tui_dialog_state tui_dialog_handle(tui_dialog *d, const tui_event *ev) {
    if (!d || !ev) return d ? d->state : TUI_DIALOG_CANCELLED;
    if (d->state != TUI_DIALOG_OPEN) return d->state;
    int is_key = ev->type == TUI_EV_KEY;
    int cancel = is_key && (ev->key == TUI_KEY_ESC || TUI_EV_IS_CTRL(ev, 'c'));

    switch (d->kind) {
    case TUI_DIALOG_MESSAGE:
        if (cancel || (is_key && ev->key == TUI_KEY_ENTER) || TUI_EV_IS_CHAR(ev, ' '))
            return close_with(d, TUI_DIALOG_OK);
        break;
    case TUI_DIALOG_CONFIRM:
        if (cancel) {
            d->answer = 0;
            return close_with(d, TUI_DIALOG_CANCELLED);
        }
        if (TUI_EV_IS_CHAR(ev, 'y') || TUI_EV_IS_CHAR(ev, 'Y')) {
            d->answer = 1;
            return close_with(d, TUI_DIALOG_OK);
        }
        if (TUI_EV_IS_CHAR(ev, 'n') || TUI_EV_IS_CHAR(ev, 'N')) {
            d->answer = 0;
            return close_with(d, TUI_DIALOG_OK);
        }
        if (is_key && ev->key == TUI_KEY_ENTER) return close_with(d, TUI_DIALOG_OK);
        if (is_key && (ev->key == TUI_KEY_LEFT || ev->key == TUI_KEY_RIGHT ||
                       ev->key == TUI_KEY_TAB))
            d->answer = !d->answer;
        break;
    case TUI_DIALOG_PROMPT: {
        tui_edit_result r = tui_lineedit_handle(&d->edit, ev);
        if (r == TUI_EDIT_ACCEPT) return close_with(d, TUI_DIALOG_OK);
        if (r == TUI_EDIT_CANCEL) return close_with(d, TUI_DIALOG_CANCELLED);
        break;
    }
    case TUI_DIALOG_CHECKLIST:
        if (TUI_EV_IS_CHAR(ev, ' ') && d->nitems > 0) {
            d->checked[d->list.selected] ^= 1;
            return d->state;
        }
        /* Everything else is the menu's. */
        /* FALLTHROUGH */
    case TUI_DIALOG_MENU:
        if (cancel) return close_with(d, TUI_DIALOG_CANCELLED);
        if (is_key && ev->key == TUI_KEY_ENTER && d->nitems > 0) {
            d->answer = d->list.selected;
            return close_with(d, TUI_DIALOG_OK);
        }
        tui_listview_handle(&d->list, ev);
        d->answer = d->list.selected;
        break;
    }
    return d->state;
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

static int text_width(const char *s) {
    int widest = 0;
    while (s && *s) {
        const char *nl = strchr(s, '\n');
        size_t n = nl ? (size_t)(nl - s) : strlen(s);
        int w = tui_strn_width(s, n);
        if (w > widest) widest = w;
        s = nl ? nl + 1 : s + n;
    }
    return widest;
}

typedef struct {
    tui_dialog *d;
} menu_ctx;

static void draw_item(void *ctx, tui_canvas row, int index, int selected) {
    tui_dialog *d = ((menu_ctx *)ctx)->d;
    tui_style st = selected ? d->focus : d->fill;
    tui_canvas_clear(row, st);
    if (d->kind == TUI_DIALOG_CHECKLIST) {
        tui_canvas_put(row, 0, 0, d->checked[index] ? "[x] " : "[ ] ", st);
        row = tui_canvas_sub(row, tui_rect_make(0, 4, 1, tui_canvas_cols(row) - 4));
    }
    tui_canvas_put_aligned(row, 0, d->items[index] ? d->items[index] : "", TUI_ALIGN_LEFT, st);
}

void tui_dialog_render(tui_dialog *d, tui_canvas c) {
    if (!d || !c.screen) return;
    /* A margin of one cell around the dialog; on a tiny canvas the box is
     * clamped to the canvas by tui_rect_center(). */
    int avail_w = tui_canvas_cols(c) - 2, avail_h = tui_canvas_rows(c) - 2;

    /* Width: the widest of the text, the title, the buttons, the items. */
    int want = text_width(d->text);
    int tw = tui_str_width(d->title) + 4;
    if (tw > want) want = tw;
    if (d->kind == TUI_DIALOG_MESSAGE && want < 6) want = 6;
    if (d->kind == TUI_DIALOG_CONFIRM && want < 15) want = 15;
    if (d->kind == TUI_DIALOG_PROMPT && want < 30) want = 30;
    if (d->kind == TUI_DIALOG_MENU || d->kind == TUI_DIALOG_CHECKLIST) {
        int mark = d->kind == TUI_DIALOG_CHECKLIST ? 4 : 0;      /* "[x] " */
        for (int i = 0; i < d->nitems; i++) {
            int w = tui_str_width(d->items[i]) + 2 + mark;
            if (w > want) want = w;
        }
    }
    int inner_w = want;
    if (inner_w > avail_w - 4) inner_w = avail_w - 4;
    if (inner_w < 1) inner_w = 1;

    /* Height: the frame, the text, then what the kind adds below it. */
    int lines = d->text[0] ? tui_textview_wrap(&d->body, inner_w) : 0;
    if (lines < 0) lines = 0;
    int extra = 0;
    switch (d->kind) {
    case TUI_DIALOG_MESSAGE:
    case TUI_DIALOG_CONFIRM: extra = lines > 0 ? 2 : 1; break;   /* blank + buttons */
    case TUI_DIALOG_PROMPT:  extra = 1; break;                     /* the input line */
    case TUI_DIALOG_MENU:
    case TUI_DIALOG_CHECKLIST: extra = d->nitems > 0 ? d->nitems : 1; break;
    }
    int h = 2 + lines + extra;
    if (h > avail_h) h = avail_h;
    int w = inner_w + 4;

    tui_rect box = tui_rect_center(tui_canvas_rect(c), h, w);
    tui_canvas_fill(c, box, d->fill);
    tui_canvas_box(c, box, d->title, d->frame);
    tui_rect content = tui_rect_make(box.row + 1, box.col + 2, box.rows - 2, box.cols - 4);
    tui_canvas cc = tui_canvas_sub(c, content);
    int rows = tui_canvas_rows(cc);
    /* A menu has no text: its list takes every row. */
    int is_list = d->kind == TUI_DIALOG_MENU || d->kind == TUI_DIALOG_CHECKLIST;
    int below = is_list ? rows : extra;
    if (below > rows) below = rows;
    /* Short of rows, the blank line above the buttons goes first: the
     * text keeps at least one row. */
    if (!is_list && lines > 0 && rows - below < 1 && below > 1) below--;
    int text_rows = rows - below;
    if (text_rows > 0 && lines > 0)
        tui_textview_render(&d->body, tui_canvas_sub(cc, tui_rect_make(0, 0, text_rows, inner_w)),
                            d->fill);

    tui_canvas last = tui_canvas_sub(cc, tui_rect_make(rows - 1, 0, 1, inner_w));
    tui_screen_set_cursor(c.screen, 0, 0, 0);
    switch (d->kind) {
    case TUI_DIALOG_MESSAGE:
        tui_canvas_put_aligned(last, 0, "[ OK ]", TUI_ALIGN_CENTER, d->focus);
        break;
    case TUI_DIALOG_CONFIRM: {
        int start = (tui_canvas_cols(last) - 15) / 2;
        if (start < 0) start = 0;
        tui_canvas_put(last, 0, start, "[ Yes ]", d->answer ? d->focus : d->fill);
        tui_canvas_put(last, 0, start + 9, "[ No ]", d->answer ? d->fill : d->focus);
        break;
    }
    case TUI_DIALOG_PROMPT:
        tui_lineedit_render(&d->edit, last, NULL, d->fill);
        break;
    case TUI_DIALOG_MENU:
    case TUI_DIALOG_CHECKLIST: {
        menu_ctx ctx = { d };
        tui_canvas list = tui_canvas_sub(cc, tui_rect_make(text_rows, 0, below, inner_w));
        tui_listview_render(&d->list, list, d->fill, draw_item, &ctx);
        break;
    }
    }
}
