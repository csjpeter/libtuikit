/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-09 */
/* Unit tests of the scroll bar at the right edge of the scrolling widgets (TUI-110). */

/*
 * The scroll bar is shown only when the content is longer than the area,
 * takes the last column (the text shrinks by one only then), and its thumb
 * shows the size and place of the visible part.  Every test renders into a
 * screen and compares the emulated cells.
 */

#include "test_helpers.h"
#include "../../src/internal.h"
#include "tuikit/canvas.h"
#include "tuikit/dialog.h"
#include "tuikit/listview.h"
#include "tuikit/table.h"
#include "tuikit/textview.h"
#include "tuikit/tree.h"

int g_tests_run = 0;
int g_tests_failed = 0;

#define THUMB "\xE2\x96\x88"
#define TRACK "\xE2\x94\x82"

typedef struct {
    tui_screen *scr;
    tui_buf     buf;
    PtySession *s;
} Fx;

static int fx_open(Fx *f, int rows, int cols) {
    f->scr = tui_screen_new(rows, cols);
    tui_buf_init(&f->buf);
    f->s = pty_open(cols, rows);
    return (f->scr && f->s) ? 0 : -1;
}

static void fx_show(Fx *f) {
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    if (f->s) pty_close(f->s);
}

/* The bar of column @p col, rows [r0, r0 + rows): a string of 'T' (thumb),
 * '.' (track), ' ' (nothing). */
static void bar_of(Fx *f, int col, int r0, int rows, char *out) {
    for (int r = 0; r < rows; r++) {
        const char *t = pty_cell_text(f->s, r0 + r, col);
        out[r] = strcmp(t, THUMB) == 0 ? 'T' : strcmp(t, TRACK) == 0 ? '.' : ' ';
    }
    out[rows] = '\0';
}

/* ── The thumb's size and place ─────────────────────────────────────── */

static void test_geometry(void) {
    int top, len;
    tui__scrollbar_geometry(10, 20, 10, 0, &top, &len);
    ASSERT_EQ_INT(len, 5, "half of the content is shown: half the track");
    ASSERT_EQ_INT(top, 0, "at the top");
    tui__scrollbar_geometry(10, 20, 10, 10, &top, &len);
    ASSERT_EQ_INT(top, 5, "at the end the thumb touches the bottom");
    tui__scrollbar_geometry(10, 20, 10, 5, &top, &len);
    ASSERT_EQ_INT(top, 3, "in the middle (rounded)");
    tui__scrollbar_geometry(10, 1000, 10, 495, &top, &len);
    ASSERT_EQ_INT(len, 1, "never smaller than one row");
    tui__scrollbar_geometry(10, 11, 10, 0, &top, &len);
    ASSERT_EQ_INT(len, 9, "never the whole track while there is more");
    tui__scrollbar_geometry(10, 11, 10, 99, &top, &len);
    ASSERT_EQ_INT(top, 1, "a top past the end is clamped");
    tui__scrollbar_geometry(1, 5, 1, 0, &top, &len);
    ASSERT_EQ_INT(len, 1, "one row: the thumb fills it");
    ASSERT_EQ_INT(top, 0, "and sits at the top");
    ASSERT(!tui__scrollbar_needed(10, 10), "content as long as the area: no bar");
    ASSERT(!tui__scrollbar_needed(3, 10), "shorter: no bar");
    ASSERT(!tui__scrollbar_needed(10, 0), "no area: no bar");
    ASSERT(tui__scrollbar_needed(11, 10), "one more line: a bar");
}

static void test_scheme_roles(void) {
    tui_style tr = tui_scheme_builtin(TUI_ROLE_SCROLLBAR_TRACK);
    tui_style th = tui_scheme_builtin(TUI_ROLE_SCROLLBAR_THUMB);
    ASSERT_EQ_INT(tr.attrs, TUI_ATTR_DIM, "the track is dim");
    ASSERT_EQ_INT(tr.fg, TUI_COLOR_DEFAULT, "with no colour of its own");
    ASSERT_EQ_INT(th.attrs, 0, "the thumb is plain");
    ASSERT_EQ_STR(tui_role_name(TUI_ROLE_SCROLLBAR_TRACK), "scrollbar.track", "track key");
    ASSERT_EQ_STR(tui_role_name(TUI_ROLE_SCROLLBAR_THUMB), "scrollbar.thumb", "thumb key");
}

/* ── Text view ──────────────────────────────────────────────────────── */

static void test_textview_bar(void) {
    tui_textview tv;
    tui_textview_init(&tv);
    Fx f;
    ASSERT(fx_open(&f, 4, 10) == 0, "fixtures");
    char bar[16];

    /* Fits: no bar, the text keeps the whole width. */
    tui_textview_set_text(&tv, "aaaaaaaaaa\nb\nc");
    tui_textview_render(&tv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_CELL_TEXT(f.s, 0, 9, "a");
    bar_of(&f, 9, 0, 4, bar);
    ASSERT_EQ_STR(bar, "    ", "short text: no bar");

    /* Longer than the area: the bar at the right edge, the text one column narrower. */
    tui_textview_set_text(&tv, "1\n2\n3\n4\n5\n6\n7\n8\n");
    tui_textview_render(&tv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    bar_of(&f, 9, 0, 4, bar);
    ASSERT_EQ_STR(bar, "TT..", "top: thumb at the top");
    ASSERT_CELL_ATTR(f.s, 3, 9, PTY_ATTR_DIM);
    ASSERT_CELL_TEXT(f.s, 0, 0, "1");

    tui_textview_scroll(&tv, 2);
    tui_textview_render(&tv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    bar_of(&f, 9, 0, 4, bar);
    ASSERT_EQ_STR(bar, ".TT.", "middle: thumb moved down");

    tui_textview_scroll_end(&tv);
    tui_textview_render(&tv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    bar_of(&f, 9, 0, 4, bar);
    ASSERT_EQ_STR(bar, "..TT", "end: thumb at the bottom");
    ASSERT_CELL_TEXT(f.s, 3, 0, "8");

    /* The wrap makes it longer than the area only at the narrower width. */
    tui_textview_set_text(&tv, "aaaaaaaaaaa\nbbbbbbbbb\nccccccccc\nddddddddd");
    tui_textview_render(&tv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    bar_of(&f, 9, 0, 4, bar);
    ASSERT(strcmp(bar, "    ") != 0, "four lines wrap to five: a bar");
    ASSERT_CELL_TEXT(f.s, 0, 8, "a");

    /* Resize: wide enough again, the bar goes and the text gets the column back. */
    fx_close(&f);
    ASSERT(fx_open(&f, 8, 12) == 0, "fixtures");
    tui_textview_render(&tv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    bar_of(&f, 11, 0, 8, bar);
    ASSERT_EQ_STR(bar, "        ", "tall enough: no bar after the resize");
    ASSERT_CELL_TEXT(f.s, 0, 9, "a");
    fx_close(&f);

    /* Wide characters: never split at the narrower width. */
    ASSERT(fx_open(&f, 2, 6) == 0, "fixtures");
    tui_textview_set_text(&tv, "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97\n\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97\n"
                               "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97");
    tui_textview_render(&tv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    char b2[4];
    bar_of(&f, 5, 0, 2, b2);
    ASSERT(strcmp(b2, "  ") != 0, "wide text: bar shown");
    ASSERT_CELL_TEXT(f.s, 0, 0, "\xE4\xB8\xAD");
    ASSERT_CELL_TEXT(f.s, 0, 2, "\xE6\x96\x87");
    ASSERT_CELL_TEXT(f.s, 0, 4, " ");   /* the third would cross into the bar column */
    fx_close(&f);
    tui_textview_free(&tv);
}

/* ── List view and tree ─────────────────────────────────────────────── */

static void draw_num(void *ctx, tui_canvas row, int index, int selected) {
    (void)ctx;
    char t[16];
    snprintf(t, sizeof t, "%sitem %d", selected ? ">" : " ", index);
    tui_canvas_put(row, 0, 0, t, TUI_STYLE_DEFAULT);
}

static void test_listview_bar(void) {
    tui_listview lv;
    tui_listview_init(&lv);
    Fx f;
    ASSERT(fx_open(&f, 4, 12) == 0, "fixtures");
    char bar[16];

    tui_listview_set_count(&lv, 4);
    tui_listview_render(&lv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT, draw_num, NULL);
    fx_show(&f);
    bar_of(&f, 11, 0, 4, bar);
    ASSERT_EQ_STR(bar, "    ", "as many items as rows: no bar");

    tui_listview_set_count(&lv, 8);
    tui_listview_render(&lv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT, draw_num, NULL);
    fx_show(&f);
    bar_of(&f, 11, 0, 4, bar);
    ASSERT_EQ_STR(bar, "TT..", "more items: bar, thumb at the top");
    ASSERT_CELL_TEXT(f.s, 0, 1, "i");

    tui_listview_select(&lv, 5);
    tui_listview_render(&lv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT, draw_num, NULL);
    fx_show(&f);
    bar_of(&f, 11, 0, 4, bar);
    ASSERT_EQ_STR(bar, ".TT.", "scrolled: thumb in the middle");

    tui_listview_end(&lv);
    tui_listview_render(&lv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT, draw_num, NULL);
    fx_show(&f);
    bar_of(&f, 11, 0, 4, bar);
    ASSERT_EQ_STR(bar, "..TT", "end");

    tui_listview_set_count(&lv, 2);                 /* the list shrinks: the bar goes */
    tui_listview_render(&lv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT, draw_num, NULL);
    fx_show(&f);
    bar_of(&f, 11, 0, 4, bar);
    ASSERT_EQ_STR(bar, "    ", "gone again");
    fx_close(&f);
}

static void test_tree_bar(void) {
    tui_tree t;
    tui_tree_init(&t);
    Fx f;
    ASSERT(fx_open(&f, 3, 14) == 0, "fixtures");
    static const tui_treerow rows[9] = {
        { "1", 0, 0 }, { "2", 0, 0 }, { "3", 0, 0 }, { "4", 0, 0 }, { "5", 0, 0 },
        { "6", 0, 0 }, { "7", 0, 0 }, { "8", 0, 0 }, { "9", 0, 0 } };
    ASSERT_EQ_INT(tui_tree_set_rows(&t, rows, 9), 0, "rows");
    tui_tree_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT, TUI_STYLE_DEFAULT, NULL, NULL);
    fx_show(&f);
    char bar[8];
    bar_of(&f, 13, 0, 3, bar);
    ASSERT_EQ_STR(bar, "T..", "a tree longer than its window has the bar");
    tui_tree_free(&t);
    fx_close(&f);
}

/* ── Table (and so the history view) ────────────────────────────────── */

static void cell_fn(void *ctx, int index, int col, tui_table_cell *c) {
    (void)ctx;
    (void)col;
    char t[16];
    snprintf(t, sizeof t, "row%d", index);
    tui_table_cell_put(c, t, TUI_STYLE_DEFAULT);
}

static const tui_table_col g_cols[] = { { "NAME", 6, 1, TUI_ALIGN_LEFT, 0, 0, 0 } };

static void test_table_bar(void) {
    tui_table t;
    tui_table_model m = { NULL, cell_fn, NULL };
    tui_table_init(&t, g_cols, 1, &m);
    Fx f;
    ASSERT(fx_open(&f, 5, 14) == 0, "fixtures");
    char bar[8];

    tui_table_set_count(&t, 4);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    bar_of(&f, 13, 1, 4, bar);
    ASSERT_EQ_STR(bar, "    ", "four rows under the header fit: no bar");

    tui_table_set_count(&t, 12);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    bar_of(&f, 13, 1, 4, bar);
    ASSERT_EQ_STR(bar, "T...", "the bar is beside the rows");
    ASSERT_CELL_TEXT(f.s, 0, 13, " ");   /* not beside the header */
    ASSERT_ROW_CONTAINS(f.s, 0, "NAME");

    tui_table_select(&t, 5);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    bar_of(&f, 13, 1, 4, bar);
    ASSERT_EQ_STR(bar, ".T..", "middle");

    tui_table_select(&t, 11);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    bar_of(&f, 13, 1, 4, bar);
    ASSERT_EQ_STR(bar, "...T", "end");

    /* A single row drawn by the program leaves the bar column alone. */
    ASSERT_EQ_INT(tui_table_render_row(&t, tui_canvas_of(f.scr), 11), 1, "row drawn");
    tui_table_render_header(&t, tui_canvas_of(f.scr));
    fx_show(&f);
    bar_of(&f, 13, 1, 4, bar);
    ASSERT_EQ_STR(bar, "...T", "still there");
    tui_table_free(&t);
    fx_close(&f);
}

/* ── Dialogs and the help overlay ───────────────────────────────────── */

static void test_dialog_bars(void) {
    static const char *const keys[] = {
        "a", "one", "b", "two", "c", "three", "d", "four", "e", "five", "f", "six" };
    tui_dialog d;
    Fx f;
    ASSERT(fx_open(&f, 7, 24) == 0, "fixtures");
    ASSERT_EQ_INT(tui_dialog_help(&d, "Keys", keys, 6), 0, "help");
    tui_canvas_clear(tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    tui_dialog_render(&d, tui_canvas_of(f.scr));
    fx_show(&f);
    /* Three entry rows (rows 2..4) of six: the bar in the last content column. */
    int col = -1;
    for (int c = 0; c < 24 && col < 0; c++)
        if (strcmp(pty_cell_text(f.s, 2, c), THUMB) == 0) col = c;
    ASSERT(col > 0, "the help overlay has a thumb at the top");
    char bar[8];
    bar_of(&f, col, 2, 3, bar);
    ASSERT_EQ_STR(bar, "T..", "help: top");
    ASSERT_ROW_CONTAINS(f.s, 2, "a");
    d.list.scroll_top = 3;
    tui_canvas_clear(tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    tui_dialog_render(&d, tui_canvas_of(f.scr));
    fx_show(&f);
    bar_of(&f, col, 2, 3, bar);
    ASSERT_EQ_STR(bar, "..T", "help: end");
    tui_dialog_free(&d);

    /* A help that fits has none. */
    ASSERT_EQ_INT(tui_dialog_help(&d, "Keys", keys, 3), 0, "help");
    tui_canvas_clear(tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    tui_dialog_render(&d, tui_canvas_of(f.scr));
    fx_show(&f);
    int found = 0;
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 24; c++)
            if (strcmp(pty_cell_text(f.s, r, c), THUMB) == 0) found = 1;
    ASSERT(!found, "a help that fits: no bar");
    tui_dialog_free(&d);

    /* A menu longer than the screen. */
    static const char *const items[] = { "one", "two", "three", "four", "five", "six", "seven" };
    ASSERT_EQ_INT(tui_dialog_menu(&d, "Menu", items, 7, 0), 0, "menu");
    tui_canvas_clear(tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    tui_dialog_render(&d, tui_canvas_of(f.scr));
    fx_show(&f);
    found = 0;
    for (int r = 0; r < 7; r++)
        for (int c = 0; c < 24; c++)
            if (strcmp(pty_cell_text(f.s, r, c), THUMB) == 0) found = 1;
    ASSERT(found, "a menu with more entries than rows: a bar");
    tui_dialog_free(&d);
    fx_close(&f);
}

static const TestCase tests[] = {
    TEST_CASE(test_geometry),
    TEST_CASE(test_scheme_roles),
    TEST_CASE(test_textview_bar),
    TEST_CASE(test_listview_bar),
    TEST_CASE(test_tree_bar),
    TEST_CASE(test_table_bar),
    TEST_CASE(test_dialog_bars),
};

int main(int argc, char **argv) {
    return RUN_SUITE("scrollbar", tests, 45, 60);
}
