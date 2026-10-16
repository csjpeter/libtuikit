/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-16 */
/* Capability tests: tui_screen, rectangles and tui_canvas drawing. */

#include "cap_common.h"

static void cap_screen(void) {
    tui_screen *s = tui_screen_new(4, 20);
    CHECK(s != NULL, "new");
    CHECK_INT(tui_screen_rows(s), 4, "rows");
    CHECK_INT(tui_screen_cols(s), 20, "cols");
    CHECK_INT(tui_screen_rows(NULL), 0, "rows of NULL");
    CHECK_INT(tui_screen_cols(NULL), 0, "cols of NULL");

    tui_screen_clear(s, TUI_STYLE_DEFAULT);
    CHECK_INT(tui_screen_put(s, 0, 0, 20, "hello", TUI_STYLE_DEFAULT), 5, "put returns columns");
    CHECK_INT(tui_screen_put(s, 1, 0, 3, "abcdef", TUI_STYLE_DEFAULT), 3, "put honours max_cols");
    tui_screen_fill(s, 2, 2, 4, '#', TUI_STYLE_ERROR);
    tui_screen_set_cursor(s, 3, 7, 1);

    tui_buf b;
    tui_buf_init(&b);
    long n = tui_screen_render(s, &b);
    CHECK(n > 0 && (size_t)n == b.len, "render returns the bytes appended");
    CHECK_HAS(b.data, "hello", "text is rendered");
    CHECK_HAS(b.data, "abc", "clipped text");
    CHECK(strstr(b.data, "abcd") == NULL, "clipped text stops at max_cols");
    CHECK_HAS(b.data, "####", "fill");
    CHECK_HAS(b.data, "\x1b[0;1;31m", "fill is bold red");
    CHECK_HAS(b.data, "\x1b[?25h", "cursor shown");
    tui_buf_reset(&b);
    CHECK_INT(tui_screen_render(s, &b), 0, "an unchanged screen renders nothing");

    tui_screen_set_cursor(s, 3, 7, 0);
    tui_buf_reset(&b);
    (void)tui_screen_render(s, &b);
    CHECK_HAS(b.data, "\x1b[?25l", "cursor hidden");

    tui_screen_invalidate(s);
    tui_buf_reset(&b);
    CHECK(tui_screen_render(s, &b) > 0, "invalidate redraws everything");
    CHECK_HAS(b.data, "hello", "again");

    tui_screen_set_sync(s, 1);
    tui_screen_put(s, 0, 0, 20, "HELLO", TUI_STYLE_DEFAULT);
    tui_buf_reset(&b);
    (void)tui_screen_render(s, &b);
    CHECK_HAS(b.data, "\x1b[?2026h", "synchronized update begins");
    CHECK_HAS(b.data, "\x1b[?2026l", "synchronized update ends");
    tui_screen_set_sync(s, 0);
    tui_screen_put(s, 0, 0, 20, "hello", TUI_STYLE_DEFAULT);
    tui_buf_reset(&b);
    (void)tui_screen_render(s, &b);
    CHECK(strstr(b.data, "\x1b[?2026h") == NULL, "no synchronized update when off");

    CHECK_INT(tui_screen_resize(s, 6, 30), 0, "resize");
    CHECK_INT(tui_screen_rows(s), 6, "new rows");
    CHECK_INT(tui_screen_cols(s), 30, "new cols");
    tui_buf_free(&b);
    tui_screen_free(s);
    tui_screen_free(NULL);
}

static void cap_rect(void) {
    tui_rect r = tui_rect_make(2, 3, 10, 40), a, b;
    CHECK(!tui_rect_is_empty(r), "not empty");
    CHECK(tui_rect_is_empty(tui_rect_make(0, 0, 0, 5)), "empty");

    tui_rect_split_h(r, 10, &a, &b);
    CHECK_INT(a.cols, 10, "left cols");
    CHECK_INT(b.cols, 30, "right cols");
    CHECK_INT(b.col, 13, "right col");
    tui_rect_split_h(r, -15, &a, &b);
    CHECK_INT(b.cols, 15, "negative counts from the right");
    CHECK_INT(a.cols, 25, "left of that");
    tui_rect_split_h(r, 5, NULL, &b);
    CHECK_INT(b.cols, 35, "NULL output allowed");

    tui_rect_split_v(r, 3, &a, &b);
    CHECK_INT(a.rows, 3, "top rows");
    CHECK_INT(b.rows, 7, "bottom rows");
    CHECK_INT(b.row, 5, "bottom row");
    tui_rect_split_v(r, -2, &a, &b);
    CHECK_INT(b.rows, 2, "negative counts from the bottom");

    tui_rect in = tui_rect_inset(r, 1);
    CHECK_INT(in.row, 3, "inset row");
    CHECK_INT(in.col, 4, "inset col");
    CHECK_INT(in.rows, 8, "inset rows");
    CHECK_INT(in.cols, 38, "inset cols");
    CHECK(tui_rect_is_empty(tui_rect_inset(r, 50)), "inset never below empty");

    tui_rect c = tui_rect_center(tui_rect_make(0, 0, 10, 20), 4, 10);
    CHECK_INT(c.row, 3, "centre row");
    CHECK_INT(c.col, 5, "centre col");
    c = tui_rect_center(tui_rect_make(0, 0, 4, 6), 10, 20);
    CHECK_INT(c.rows, 4, "at most the rectangle: rows");
    CHECK_INT(c.cols, 6, "at most the rectangle: cols");

    tui_rect x = tui_rect_intersect(tui_rect_make(0, 0, 5, 5), tui_rect_make(3, 3, 5, 5));
    CHECK_INT(x.row, 3, "intersection row");
    CHECK_INT(x.rows, 2, "intersection rows");
    CHECK_INT(x.cols, 2, "intersection cols");
    CHECK(tui_rect_is_empty(tui_rect_intersect(tui_rect_make(0, 0, 2, 2),
                                               tui_rect_make(5, 5, 2, 2))),
          "disjoint is empty");
}

static void cap_style(void) {
    tui_style a = tui_style_make(TUI_RED, TUI_BLUE, TUI_ATTR_BOLD);
    CHECK(a.fg == TUI_RED && a.bg == TUI_BLUE && a.attrs == TUI_ATTR_BOLD, "style_make");
    tui_style b = tui_style_attrs(TUI_ATTR_UNDERLINE);
    CHECK(b.fg == TUI_COLOR_DEFAULT && b.bg == TUI_COLOR_DEFAULT && b.attrs == TUI_ATTR_UNDERLINE,
          "style_attrs");
    CHECK(tui_style_eq(a, a), "style_eq equal");
    CHECK(!tui_style_eq(a, b), "style_eq different");
    CHECK(tui_style_eq(TUI_STYLE_DEFAULT, tui_style_make(TUI_COLOR_DEFAULT, TUI_COLOR_DEFAULT, 0)),
          "the default style");
    CHECK_INT(TUI_COLOR_256(200) & 0xFF, 200, "256-colour value");
    CHECK_INT(TUI_COLOR_RGB(1, 2, 3) & 0xFFFFFF, 0x010203, "rgb value");

    tui_screen *s = tui_screen_new(2, 20);
    tui_canvas c = tui_canvas_of(s);
    tui_canvas_put(c, 0, 0, "rgb", tui_style_make(TUI_COLOR_RGB(10, 20, 30), TUI_COLOR_256(99),
                                                  TUI_ATTR_ITALIC | TUI_ATTR_UNDERLINE | TUI_ATTR_DIM));
    char *raw = NULL;
    char *txt = cap_screen_text(s, &raw);
    CHECK_HAS(raw, "38;2;10;20;30", "RGB foreground");
    CHECK_HAS(raw, "48;5;99", "256-colour background");
    CHECK_HAS(raw, "2;3;4", "dim, italic and underline");
    free(txt); free(raw);
    tui_screen_free(s);
}

static void cap_canvas(void) {
    tui_screen *s = tui_screen_new(8, 30);
    tui_canvas whole = tui_canvas_of(s);
    CHECK_INT(tui_canvas_rows(whole), 8, "canvas rows");
    CHECK_INT(tui_canvas_cols(whole), 30, "canvas cols");
    tui_rect cr = tui_canvas_rect(whole);
    CHECK(cr.row == 0 && cr.col == 0 && cr.rows == 8 && cr.cols == 30, "canvas_rect");
    tui_canvas_clear(whole, TUI_STYLE_DEFAULT);

    tui_canvas sub = tui_canvas_sub(whole, tui_rect_make(2, 5, 3, 10));
    CHECK_INT(tui_canvas_rows(sub), 3, "sub rows");
    CHECK_INT(tui_canvas_cols(sub), 10, "sub cols");
    CHECK_INT(tui_canvas_put(sub, 0, 0, "abcdefghijklmnop", TUI_STYLE_DEFAULT), 10,
              "put is clipped to the canvas");
    CHECK_INT(tui_canvas_put(sub, 1, -2, "abcdef", TUI_STYLE_DEFAULT), 4,
              "negative column scrolls");
    tui_canvas_fill(sub, tui_rect_make(2, 0, 1, 4), TUI_STYLE_SELECTION);

    CHECK_INT(tui_canvas_put_aligned(whole, 5, "mid", TUI_ALIGN_CENTER, TUI_STYLE_DEFAULT), 13,
              "centre starts at");
    CHECK_INT(tui_canvas_put_aligned(whole, 6, "end", TUI_ALIGN_RIGHT, TUI_STYLE_DEFAULT), 27,
              "right starts at");
    CHECK_INT(tui_canvas_put_aligned(whole, 7, "beg", TUI_ALIGN_LEFT, TUI_STYLE_DEFAULT), 0,
              "left starts at");

    tui_canvas_hline(whole, 0, 0, 5, TUI_STYLE_DEFAULT);
    tui_canvas_vline(whole, 1, 29, 3, TUI_STYLE_DEFAULT);
    tui_canvas_box(whole, tui_rect_make(3, 20, 4, 8), "T", TUI_STYLE_ACCENT);
    tui_canvas_set_cursor(sub, 1, 2, 1);

    char *raw = NULL;
    char *text = cap_screen_text(s, &raw);
    CHECK_HAS(text, "abcdefghij", "put text");
    CHECK(strstr(text, "abcdefghijk") == NULL, "nothing outside the canvas");
    CHECK_HAS(text, "cdef", "scrolled text starts at its third character");
    CHECK_HAS(text, "mid", "centred");
    CHECK_HAS(text, "\xe2\x94\x80\xe2\x94\x80\xe2\x94\x80", "hline is U+2500");
    CHECK_HAS(text, "\xe2\x94\x82", "vline is U+2502");
    CHECK_HAS(text, "\xe2\x94\x8c", "box corner");
    CHECK_HAS(text, "T", "box title");
    CHECK_HAS(raw, "\x1b[0;1;7m", "reverse bold selection fill");
    CHECK_HAS(raw, "\x1b[?25h", "canvas cursor shown");
    free(text);
    free(raw);
    tui_screen_free(s);
}

int main(void) {
    cap_screen();
    cap_rect();
    cap_style();
    cap_canvas();
    return cap_finish("cap-draw");
}
