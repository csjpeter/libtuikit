/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* The inline one-row yes/no question: "Text (y/N)", answered by one key. */

#include "tuikit/yesno.h"
#include "tuikit/scheme.h"
#include "tuikit/text.h"

#include <string.h>

#define SUFFIX      "(y/N)"
#define SUFFIX_COLS 5
#define LINE_MAX_B  (256 + 8 + 8)

typedef struct {
    char text[LINE_MAX_B];   /* what is drawn, in column order */
    int  suffix_col;         /* where "(y/N)" starts, in columns */
    size_t suffix_off;       /* and in bytes of text */
    int  suffix_len;         /* its columns on the row (cut when the row is narrower) */
} layout_t;

/* The question and its suffix on @p cols columns: the question is cut with
 * an ellipsis so that the suffix stays; a row narrower than the suffix shows
 * the front of it. */
static void layout(const tui_yesno *yn, int cols, layout_t *lo) {
    lo->text[0] = '\0';
    lo->suffix_col = 0;
    lo->suffix_off = 0;
    lo->suffix_len = 0;
    if (cols <= 0) return;
    if (cols < SUFFIX_COLS) {
        size_t n = tui_str_fit(SUFFIX, cols, NULL);
        memcpy(lo->text, SUFFIX, n);
        lo->text[n] = '\0';
        lo->suffix_len = (int)n;
        return;
    }
    lo->suffix_len = SUFFIX_COLS;
    int budget = cols - SUFFIX_COLS - 1;      /* the blank before the suffix */
    if (yn->text[0] == '\0' || budget <= 0) {
        strcpy(lo->text, SUFFIX);
        return;
    }
    char q[LINE_MAX_B];
    if (tui_str_width(yn->text) <= budget) {
        strcpy(q, yn->text);
    } else {
        tui_str_fit_ellipsis(yn->text, budget, q, sizeof q);
    }
    lo->suffix_col = tui_str_width(q) + 1;
    lo->suffix_off = strlen(q) + 1;
    strcpy(lo->text, q);
    strcat(lo->text, " " SUFFIX);
}

void tui_yesno_init(tui_yesno *yn, const char *text) {
    if (!yn) return;
    memset(yn, 0, sizeof *yn);
    if (!text) return;
    size_t len = strlen(text);
    if (len > sizeof yn->text - 1) {
        len = sizeof yn->text - 1;
        while (len > 0 && ((unsigned char)text[len] & 0xC0) == 0x80) len--;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)text[i];
        yn->text[i] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    yn->text[len] = '\0';
}

tui_yesno_result tui_yesno_handle(tui_yesno *yn, const tui_event *ev) {
    if (!yn) return TUI_YESNO_OPEN;
    if (yn->result != TUI_YESNO_OPEN || !ev) return (tui_yesno_result)yn->result;
    if (ev->type == TUI_EV_HANGUP) {
        yn->result = TUI_YESNO_INTERRUPT;
    } else if (ev->type == TUI_EV_KEY) {
        if (TUI_EV_IS_CTRL(ev, 'c'))
            yn->result = TUI_YESNO_INTERRUPT;
        else if (ev->key == TUI_KEY_CHAR && (ev->cp == 'y' || ev->cp == 'Y') &&
                 !(ev->mods & (TUI_MOD_CTRL | TUI_MOD_ALT)))
            yn->result = TUI_YESNO_YES;
        else
            yn->result = TUI_YESNO_NO;
    }
    return (tui_yesno_result)yn->result;
}

tui_yesno_result tui_yesno_get(const tui_yesno *yn) {
    return yn ? (tui_yesno_result)yn->result : TUI_YESNO_OPEN;
}

void tui_yesno_render(const tui_yesno *yn, tui_canvas c, int row) {
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    if (!yn || row < 0 || row >= rows || cols <= 0) return;
    tui_style base = tui_scheme_style(TUI_ROLE_TEXT);
    tui_style bold = base;
    bold.attrs = (uint16_t)(bold.attrs | TUI_ATTR_BOLD);
    layout_t lo;
    layout(yn, cols, &lo);
    tui_canvas_fill(c, tui_rect_make(row, 0, 1, cols), base);
    size_t qlen = lo.suffix_off;
    char q[LINE_MAX_B];
    memcpy(q, lo.text, qlen);
    q[qlen] = '\0';
    if (qlen > 0) tui_canvas_put(c, row, 0, q, base);
    tui_canvas_put(c, row, lo.suffix_col, lo.text + qlen, bold);
    if (yn->result == TUI_YESNO_OPEN) {
        int cur = lo.suffix_col + lo.suffix_len + 1;
        tui_canvas_set_cursor(c, row, cur < cols ? cur : cols - 1, 1);
    }
}

int tui_yesno_format(const tui_yesno *yn, int cols, char *out, size_t outsz) {
    if (!yn || !out || outsz == 0) return -1;
    layout_t lo;
    layout(yn, cols, &lo);
    size_t n = strlen(lo.text);
    if (n + 1 > outsz) {
        n = outsz - 1;
        while (n > 0 && ((unsigned char)lo.text[n] & 0xC0) == 0x80) n--;
        memcpy(out, lo.text, n);
        out[n] = '\0';
        return -1;
    }
    memcpy(out, lo.text, n + 1);
    return (int)n;
}
