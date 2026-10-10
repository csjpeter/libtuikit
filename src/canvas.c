/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Péter Császár */

#include "tuikit/canvas.h"
#include "tuikit/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int clampi(int v, int lo, int hi) {
    return v < lo ? lo : v > hi ? hi : v;
}

static tui_rect empty_at(int row, int col) {
    return tui_rect_make(row, col, 0, 0);
}

void tui_rect_split_h(tui_rect r, int left_cols, tui_rect *left, tui_rect *right) {
    int w = r.cols > 0 ? r.cols : 0;
    int lw = left_cols >= 0 ? left_cols : w + left_cols;
    lw = clampi(lw, 0, w);
    if (left) *left = tui_rect_make(r.row, r.col, r.rows, lw);
    if (right) *right = tui_rect_make(r.row, r.col + lw, r.rows, w - lw);
}

void tui_rect_split_v(tui_rect r, int top_rows, tui_rect *top, tui_rect *bottom) {
    int h = r.rows > 0 ? r.rows : 0;
    int th = top_rows >= 0 ? top_rows : h + top_rows;
    th = clampi(th, 0, h);
    if (top) *top = tui_rect_make(r.row, r.col, th, r.cols);
    if (bottom) *bottom = tui_rect_make(r.row + th, r.col, h - th, r.cols);
}

tui_rect tui_rect_inset(tui_rect r, int n) {
    if (n < 0) n = 0;
    int rows = r.rows - 2 * n, cols = r.cols - 2 * n;
    if (rows <= 0 || cols <= 0) return empty_at(r.row + n, r.col + n);
    return tui_rect_make(r.row + n, r.col + n, rows, cols);
}

tui_rect tui_rect_center(tui_rect r, int rows, int cols) {
    int h = clampi(rows, 0, r.rows > 0 ? r.rows : 0);
    int w = clampi(cols, 0, r.cols > 0 ? r.cols : 0);
    return tui_rect_make(r.row + (r.rows - h) / 2, r.col + (r.cols - w) / 2, h, w);
}

tui_rect tui_rect_intersect(tui_rect a, tui_rect b) {
    int top = a.row > b.row ? a.row : b.row;
    int left = a.col > b.col ? a.col : b.col;
    int bottom = (a.row + a.rows < b.row + b.rows) ? a.row + a.rows : b.row + b.rows;
    int right = (a.col + a.cols < b.col + b.cols) ? a.col + a.cols : b.col + b.cols;
    if (bottom <= top || right <= left) return empty_at(top, left);
    return tui_rect_make(top, left, bottom - top, right - left);
}

tui_canvas tui_canvas_of(tui_screen *s) {
    tui_canvas c;
    c.screen = s;
    c.clip = tui_rect_make(0, 0, tui_screen_rows(s), tui_screen_cols(s));
    return c;
}

tui_canvas tui_canvas_sub(tui_canvas c, tui_rect r) {
    /* The part of r that is inside c.  Clipped at the top or left, the
     * sub-canvas starts at its first visible cell (see canvas.h). */
    tui_rect abs = tui_rect_make(c.clip.row + r.row, c.clip.col + r.col, r.rows, r.cols);
    tui_canvas sub;
    sub.screen = c.screen;
    sub.clip = tui_rect_intersect(c.clip, abs);
    return sub;
}

static int canvas_ok(tui_canvas c) {
    return c.screen && !tui_rect_is_empty(c.clip);
}

int tui_canvas_put(tui_canvas c, int row, int col, const char *utf8, tui_style st) {
    if (!canvas_ok(c) || !utf8 || row < 0 || row >= c.clip.rows || col >= c.clip.cols) return 0;
    const char *p = utf8;
    if (col < 0) {
        /* Skip the part left of the canvas; a wide character cut by the
         * edge leaves a blank in its visible half. */
        size_t n = strlen(p);
        size_t i = 0;
        while (i < n && col < 0) {
            uint32_t cp;
            int k = tui_utf8_decode(p + i, n - i, &cp);
            int w = tui_cp_width(tui_cp_sanitize(cp));
            i += (size_t)k;
            col += w;
        }
        /* Marks belonging to the last skipped character are skipped too. */
        while (i < n) {
            uint32_t cp;
            int k = tui_utf8_decode(p + i, n - i, &cp);
            if (tui_cp_width(tui_cp_sanitize(cp)) != 0) break;
            i += (size_t)k;
        }
        p += i;
        int written = 0;
        if (col > 0) {
            /* col is 1 here: the right half of a wide character. */
            tui_screen_fill(c.screen, c.clip.row + row, c.clip.col, col, ' ', st);
            written = col;
        }
        if (col >= c.clip.cols) return written;
        return written + tui_screen_put(c.screen, c.clip.row + row, c.clip.col + col,
                                        c.clip.cols - col, p, st);
    }
    return tui_screen_put(c.screen, c.clip.row + row, c.clip.col + col,
                          c.clip.cols - col, p, st);
}

int tui_canvas_put_aligned(tui_canvas c, int row, const char *utf8, tui_align align,
                           tui_style st) {
    if (!canvas_ok(c) || !utf8) return 0;
    char small[512];
    size_t need = strlen(utf8) + 4;
    char *buf = small;
    char *heap = NULL;
    if (need > sizeof(small)) {
        heap = malloc(need);
        if (!heap) return 0;
        buf = heap;
    }
    int w = tui_str_fit_ellipsis(utf8, c.clip.cols, buf, need);
    if (w < 0) w = tui_str_width(buf);
    int col = 0;
    if (align == TUI_ALIGN_CENTER) col = (c.clip.cols - w) / 2;
    else if (align == TUI_ALIGN_RIGHT) col = c.clip.cols - w;
    if (col < 0) col = 0;
    tui_canvas_put(c, row, col, buf, st);
    free(heap);
    return col;
}

void tui_canvas_fill(tui_canvas c, tui_rect r, tui_style st) {
    if (!canvas_ok(c)) return;
    tui_rect a = tui_rect_intersect(tui_canvas_rect(c), r);
    for (int i = 0; i < a.rows; i++)
        tui_screen_fill(c.screen, c.clip.row + a.row + i, c.clip.col + a.col, a.cols, ' ', st);
}

void tui_canvas_clear(tui_canvas c, tui_style st) {
    tui_canvas_fill(c, tui_canvas_rect(c), st);
}

void tui_canvas_hline(tui_canvas c, int row, int col, int n, tui_style st) {
    if (!canvas_ok(c) || row < 0 || row >= c.clip.rows) return;
    tui_rect a = tui_rect_intersect(tui_canvas_rect(c), tui_rect_make(row, col, 1, n));
    if (tui_rect_is_empty(a)) return;
    tui_screen_fill(c.screen, c.clip.row + row, c.clip.col + a.col, a.cols, 0x2500, st);
}

void tui_canvas_vline(tui_canvas c, int row, int col, int n, tui_style st) {
    if (!canvas_ok(c) || col < 0 || col >= c.clip.cols) return;
    tui_rect a = tui_rect_intersect(tui_canvas_rect(c), tui_rect_make(row, col, n, 1));
    for (int i = 0; i < a.rows; i++)
        tui_screen_fill(c.screen, c.clip.row + a.row + i, c.clip.col + col, 1, 0x2502, st);
}

/* One box-drawing character at a canvas cell, if the cell is inside. */
static void corner(tui_canvas c, int row, int col, uint32_t cp, tui_style st) {
    if (row < 0 || col < 0 || row >= c.clip.rows || col >= c.clip.cols) return;
    tui_screen_fill(c.screen, c.clip.row + row, c.clip.col + col, 1, cp, st);
}

void tui_canvas_box(tui_canvas c, tui_rect r, const char *title, tui_style st) {
    if (!canvas_ok(c) || r.rows < 2 || r.cols < 2) return;
    int top = r.row, bottom = r.row + r.rows - 1;
    int left = r.col, right = r.col + r.cols - 1;
    tui_canvas_hline(c, top, left + 1, r.cols - 2, st);
    tui_canvas_hline(c, bottom, left + 1, r.cols - 2, st);
    tui_canvas_vline(c, top + 1, left, r.rows - 2, st);
    tui_canvas_vline(c, top + 1, right, r.rows - 2, st);
    corner(c, top, left, 0x250C, st);
    corner(c, top, right, 0x2510, st);
    corner(c, bottom, left, 0x2514, st);
    corner(c, bottom, right, 0x2518, st);
    if (title && *title && r.cols >= 5) {
        /* "┌ title ─┐": one space either side, inside the corners. */
        tui_canvas edge = tui_canvas_sub(c, tui_rect_make(top, left + 2, 1, r.cols - 4));
        char spaced[512];
        snprintf(spaced, sizeof(spaced), " %s ", title);
        tui_canvas_put_aligned(edge, 0, spaced, TUI_ALIGN_LEFT, st);
    }
}

void tui_canvas_set_cursor(tui_canvas c, int row, int col, int visible) {
    if (!c.screen) return;
    if (!canvas_ok(c) || row < 0 || col < 0 || row >= c.clip.rows || col >= c.clip.cols) {
        tui_screen_set_cursor(c.screen, 0, 0, 0);
        return;
    }
    tui_screen_set_cursor(c.screen, c.clip.row + row, c.clip.col + col, visible);
}
