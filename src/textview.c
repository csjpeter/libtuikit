/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* The text view: a scrolling read-only text. */

#include "tuikit/textview.h"
#include "internal.h"
#include "tuikit/text.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

void tui_textview_init(tui_textview *tv) {
    if (!tv) return;
    memset(tv, 0, sizeof(*tv));
    tv->dirty = 1;
}

void tui_textview_free(tui_textview *tv) {
    if (!tv) return;
    free(tv->text);
    free(tv->starts);
    free(tv->lens);
    tui_textview_init(tv);
}

static int max_top(const tui_textview *tv) {
    int m = tv->nlines - (tv->rows > 0 ? tv->rows : 1);
    return m < 0 ? 0 : m;
}

static void clamp_top(tui_textview *tv) {
    if (tv->top > max_top(tv)) tv->top = max_top(tv);
    if (tv->top < 0) tv->top = 0;
}

int tui_textview_set_text(tui_textview *tv, const char *text) {
    if (!tv) {
        errno = EINVAL;
        return -1;
    }
    size_t n = text ? strlen(text) : 0;
    char *copy = malloc(n + 1);
    if (!copy) return -1;
    if (n) memcpy(copy, text, n);
    copy[n] = '\0';
    free(tv->text);
    tv->text = copy;
    tv->len = n;
    tv->top = 0;
    tv->follow = 0;
    tv->dirty = 1;
    return 0;
}

int tui_textview_at_end(const tui_textview *tv) {
    if (!tv) return 1;
    return tv->top >= max_top(tv);
}

int tui_textview_append(tui_textview *tv, const char *text, size_t n) {
    if (!tv || (!text && n)) {
        errno = EINVAL;
        return -1;
    }
    if (n == 0) return 0;
    /* At the end (or nothing shown yet): follow what is appended. */
    int follow = tv->follow || tui_textview_at_end(tv);
    char *p = realloc(tv->text, tv->len + n + 1);
    if (!p) return -1;
    memcpy(p + tv->len, text, n);
    tv->text = p;
    tv->len += n;
    tv->text[tv->len] = '\0';
    tv->dirty = 1;
    tv->follow = follow;
    return 0;
}

static int add_line(tui_textview *tv, size_t start, size_t len) {
    if (tv->nlines == tv->cap) {
        int cap = tv->cap ? tv->cap * 2 : 64;
        size_t *s = realloc(tv->starts, (size_t)cap * sizeof(size_t));
        if (!s) return -1;
        tv->starts = s;
        size_t *l = realloc(tv->lens, (size_t)cap * sizeof(size_t));
        if (!l) return -1;
        tv->lens = l;
        tv->cap = cap;
    }
    tv->starts[tv->nlines] = start;
    tv->lens[tv->nlines] = len;
    tv->nlines++;
    return 0;
}

/* Width of one character at column @p col, as the text is drawn. */
static int char_width(uint32_t cp, int col) {
    if (cp == '\t') return 8 - col % 8;
    int w = tui_cp_width(cp);
    return w < 0 ? 1 : w;
}

/* Wraps the logical line [b, e) into tv's line index. */
static int wrap_line(tui_textview *tv, size_t b, size_t e, int cols) {
    const char *t = tv->text;
    if (b == e) return add_line(tv, b, 0);
    size_t pos = b;
    while (pos < e) {
        int col = 0;
        size_t j = pos;
        size_t space_at = 0;    /* last space in this line: break there */
        int have_space = 0;
        while (j < e) {
            uint32_t cp;
            int k = tui_utf8_decode(t + j, e - j, &cp);
            int w = char_width(cp, col);
            /* Whitespace is a break opportunity even when it is itself
             * the character that does not fit. */
            if ((cp == ' ' || cp == '\t') && j > pos) {
                space_at = j;
                have_space = 1;
            }
            if (col > 0 && col + w > cols) break;
            col += w;
            j += (size_t)k;
        }
        if (j >= e) return add_line(tv, pos, e - pos);
        if (have_space) {
            if (add_line(tv, pos, space_at - pos) != 0) return -1;
            pos = space_at;
            while (pos < e && (t[pos] == ' ' || t[pos] == '\t')) pos++;   /* the break eats them */
        } else {
            if (add_line(tv, pos, j - pos) != 0) return -1;
            pos = j;
        }
    }
    return 0;
}

int tui_textview_wrap(tui_textview *tv, int cols) {
    if (!tv) {
        errno = EINVAL;
        return -1;
    }
    if (cols < 1) cols = 1;
    if (!tv->dirty && tv->width == cols) return tv->nlines;
    /* The line at the top stays at the top across a re-wrap, as far as a
     * byte offset can say where it went. */
    size_t anchor = (tv->top > 0 && tv->top < tv->nlines) ? tv->starts[tv->top] : 0;
    tv->nlines = 0;
    size_t i = 0;
    while (i < tv->len) {
        const char *nl = memchr(tv->text + i, '\n', tv->len - i);
        size_t e = nl ? (size_t)(nl - tv->text) : tv->len;
        size_t le = (e > i && tv->text[e - 1] == '\r') ? e - 1 : e;
        if (wrap_line(tv, i, le, cols) != 0) {
            tv->nlines = 0;
            tv->dirty = 1;
            errno = ENOMEM;
            return -1;
        }
        i = e + 1;
    }
    tv->width = cols;
    tv->dirty = 0;
    tv->top = 0;
    if (tv->follow) {
        tv->follow = 0;
        tv->top = max_top(tv);
    } else if (anchor > 0) {
        while (tv->top + 1 < tv->nlines && tv->starts[tv->top + 1] <= anchor) tv->top++;
    }
    clamp_top(tv);
    return tv->nlines;
}

int tui_textview_lines(const tui_textview *tv) {
    return tv ? tv->nlines : 0;
}

int tui_textview_top(const tui_textview *tv) {
    return tv ? tv->top : 0;
}

void tui_textview_scroll(tui_textview *tv, int delta) {
    if (!tv) return;
    tv->follow = 0;
    tv->top += delta;
    clamp_top(tv);
}

void tui_textview_scroll_to(tui_textview *tv, int line) {
    if (!tv) return;
    tv->follow = 0;
    tv->top = line;
    clamp_top(tv);
}

void tui_textview_scroll_end(tui_textview *tv) {
    if (!tv) return;
    tv->top = max_top(tv);
}

int tui_textview_handle(tui_textview *tv, const tui_event *ev) {
    if (!tv || !ev) return 0;
    int page = tv->rows > 1 ? tv->rows - 1 : 1;
    switch (tui__nav_key(ev)) {
    case TUI_KEY_UP:   tui_textview_scroll(tv, -1); return 1;
    case TUI_KEY_DOWN: tui_textview_scroll(tv, 1); return 1;
    case TUI_KEY_PGUP: tui_textview_scroll(tv, -page); return 1;
    case TUI_KEY_PGDN: tui_textview_scroll(tv, page); return 1;
    case TUI_KEY_HOME: tui_textview_scroll_to(tv, 0); return 1;
    case TUI_KEY_END:  tui_textview_scroll_end(tv); return 1;
    default:           return 0;
    }
}

/* Draws one wrapped line, expanding tabs. */
static void draw_line(tui_canvas c, int row, const char *s, size_t n, tui_style st) {
    char seg[1024];
    size_t sl = 0;
    int col = 0, seg_col = 0;
    size_t i = 0;
    while (i <= n) {
        uint32_t cp = 0;
        int k = 1;
        if (i < n) k = tui_utf8_decode(s + i, n - i, &cp);
        int flush = (i == n) || cp == '\t' || sl + (size_t)k + 1 > sizeof(seg);
        if (flush && sl > 0) {
            seg[sl] = '\0';
            tui_canvas_put(c, row, seg_col, seg, st);
            sl = 0;
        }
        if (i == n) break;
        if (cp == '\t') {
            col += 8 - col % 8;
            seg_col = col;
        } else {
            if (sl == 0) seg_col = col;
            memcpy(seg + sl, s + i, (size_t)k);
            sl += (size_t)k;
            col += char_width(cp, col);
        }
        i += (size_t)k;
    }
}

void tui_textview_render(tui_textview *tv, tui_canvas c, tui_style st) {
    if (!tv) return;
    tui_canvas_clear(c, st);
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    if (rows <= 0 || cols <= 0) return;
    /* A view showing the end of a longer text keeps showing the end when
     * its size changes (a chat history after a resize). */
    if (tv->top > 0 && tui_textview_at_end(tv) && (rows != tv->rows || cols != tv->width))
        tv->follow = 1;
    tv->rows = rows;
    if (cols != tv->width) tv->dirty = 1;
    if (tui_textview_wrap(tv, cols) < 0) return;
    if (tv->follow) {                    /* the width did not change */
        tv->follow = 0;
        tv->top = max_top(tv);
    }
    clamp_top(tv);
    for (int r = 0; r < rows && tv->top + r < tv->nlines; r++) {
        int line = tv->top + r;
        draw_line(c, r, tv->text + tv->starts[line], tv->lens[line], st);
    }
}
