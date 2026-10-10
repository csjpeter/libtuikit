/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Péter Császár */

/*
 * Unit tests of the drawing layer above tui_screen: rectangles and layout,
 * clipped canvases, the list view and the text view.  What they draw is
 * rendered, fed to libptytest's emulator and compared cell by cell.
 */

#include "test_helpers.h"
#include "tuikit/canvas.h"
#include "tuikit/listview.h"
#include "tuikit/textview.h"

int g_tests_run = 0;
int g_tests_failed = 0;

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

#define ASSERT_RECT(r, R, C, H, W, what) do { \
    char _w[128]; \
    snprintf(_w, sizeof(_w), "%s: row", what); ASSERT_EQ_INT((r).row, R, _w); \
    snprintf(_w, sizeof(_w), "%s: col", what); ASSERT_EQ_INT((r).col, C, _w); \
    snprintf(_w, sizeof(_w), "%s: rows", what); ASSERT_EQ_INT((r).rows, H, _w); \
    snprintf(_w, sizeof(_w), "%s: cols", what); ASSERT_EQ_INT((r).cols, W, _w); \
} while (0)

/* ── Rectangles ─────────────────────────────────────────────────────── */

static void test_rect_layout(void) {
    tui_rect r = tui_rect_make(1, 2, 10, 40), a, b;
    tui_rect_split_h(r, 12, &a, &b);
    ASSERT_RECT(a, 1, 2, 10, 12, "split_h left");
    ASSERT_RECT(b, 1, 14, 10, 28, "split_h right");
    tui_rect_split_h(r, -10, &a, &b);
    ASSERT_RECT(a, 1, 2, 10, 30, "split_h from the right: left");
    ASSERT_RECT(b, 1, 32, 10, 10, "split_h from the right: right");
    tui_rect_split_h(r, 99, &a, &b);
    ASSERT_RECT(b, 1, 42, 10, 0, "split_h beyond the width: empty right");
    tui_rect_split_v(r, -1, &a, &b);
    ASSERT_RECT(a, 1, 2, 9, 40, "split_v: all but the status row");
    ASSERT_RECT(b, 10, 2, 1, 40, "split_v: status row");
    tui_rect_split_v(r, 3, NULL, &b);
    ASSERT_RECT(b, 4, 2, 7, 40, "split_v with a NULL output");
    ASSERT_RECT(tui_rect_inset(r, 1), 2, 3, 8, 38, "inset 1");
    ASSERT(tui_rect_is_empty(tui_rect_inset(r, 5)), "inset beyond the size is empty");
    ASSERT_RECT(tui_rect_center(r, 4, 10), 4, 17, 4, 10, "centered");
    ASSERT_RECT(tui_rect_center(r, 99, 99), 1, 2, 10, 40, "centered, clamped");
    ASSERT_RECT(tui_rect_intersect(tui_rect_make(0, 0, 5, 5), tui_rect_make(3, 3, 5, 5)),
                3, 3, 2, 2, "intersection");
    ASSERT(tui_rect_is_empty(tui_rect_intersect(tui_rect_make(0, 0, 2, 2),
                                                tui_rect_make(5, 5, 2, 2))), "disjoint");
}

/* tg-cli's three-pane layout, expressed as two splits. */
static void test_three_pane_layout(void) {
    Fx f;
    ASSERT(fx_open(&f, 6, 30) == 0, "fixtures");
    tui_canvas all = tui_canvas_of(f.scr);
    tui_rect top, status, left, right;
    tui_rect_split_v(tui_canvas_rect(all), -1, &top, &status);
    tui_rect_split_h(top, 10, &left, &right);
    tui_canvas cl = tui_canvas_sub(all, left), cr = tui_canvas_sub(all, right);
    tui_canvas cs = tui_canvas_sub(all, status);
    tui_canvas_box(cl, tui_canvas_rect(cl), "Chats", TUI_STYLE_DEFAULT);
    tui_canvas_box(cr, tui_canvas_rect(cr), "History of a very long name", TUI_STYLE_DEFAULT);
    tui_canvas_put(tui_canvas_sub(cl, tui_rect_inset(tui_canvas_rect(cl), 1)), 0, 0,
                   "Alice and Bob", TUI_STYLE_DEFAULT);
    tui_canvas_clear(cs, tui_style_attrs(TUI_ATTR_REVERSE));
    tui_canvas_put_aligned(cs, 0, "q quit", TUI_ALIGN_LEFT, tui_style_attrs(TUI_ATTR_REVERSE));
    tui_canvas_put_aligned(cs, 0, "online", TUI_ALIGN_RIGHT, tui_style_attrs(TUI_ATTR_REVERSE));
    fx_show(&f);
    const char *const expect[] = {
        "┌─ Chat…─┐┌─ History of a v…─┐",
        "│Alice an││                  │",
        "│        ││                  │",
        "│        ││                  │",
        "└────────┘└──────────────────┘",
        "q quit                  online",
        NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    static const PtyStyleKey legend[] = {
        { '.', PTY_ATTR_NONE, 0, 0 }, { 'R', PTY_ATTR_REVERSE, 0, 0 }, { 0, 0, 0, 0 } };
    const char *const map[] = { "..............................",
                                "RRRRRRRRRRRRRRRRRRRRRRRRRRRRRR", NULL };
    ASSERT_REGION_STYLE(f.s, PTY_REGION(4, 0, 2, 30), map, legend);
    fx_close(&f);
}

static void test_canvas_clipping(void) {
    Fx f;
    ASSERT(fx_open(&f, 5, 12) == 0, "fixtures");
    tui_canvas all = tui_canvas_of(f.scr);
    tui_canvas c = tui_canvas_sub(all, tui_rect_make(1, 2, 3, 6));
    ASSERT_EQ_INT(tui_canvas_rows(c), 3, "canvas rows");
    ASSERT_EQ_INT(tui_canvas_cols(c), 6, "canvas cols");
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_canvas_put(c, 0, 0, "abcdefghij", TUI_STYLE_DEFAULT), 6, "clipped right");
    ASSERT_EQ_INT(tui_canvas_put(c, 1, -3, "0123456789", TUI_STYLE_DEFAULT), 6, "clipped left");
    ASSERT_EQ_INT(tui_canvas_put(c, 2, -1, "\xE4\xB8\xAD" "xyz", TUI_STYLE_DEFAULT), 4,
                  "a wide character cut by the left edge");
    ASSERT_EQ_INT(tui_canvas_put(c, 3, 0, "below", TUI_STYLE_DEFAULT), 0, "row below");
    ASSERT_EQ_INT(tui_canvas_put(c, -1, 0, "above", TUI_STYLE_DEFAULT), 0, "row above");
    ASSERT_EQ_INT(tui_canvas_put(c, 0, -20, "short", TUI_STYLE_DEFAULT), 0, "all of it left");
    /* The mark of a character left of the canvas goes with it. */
    tui_canvas mark_row = tui_canvas_sub(all, tui_rect_make(4, 0, 1, 6));
    ASSERT_EQ_INT(tui_canvas_put(mark_row, 0, -1, "e\xCC\x81z", TUI_STYLE_DEFAULT), 1,
                  "only z is drawn");
    tui_canvas_fill(c, tui_rect_make(2, 4, 5, 5), tui_style_attrs(TUI_ATTR_BOLD));
    tui_canvas_hline(c, -1, 0, 6, TUI_STYLE_DEFAULT);
    tui_canvas_vline(c, 0, 7, 3, TUI_STYLE_DEFAULT);
    fx_show(&f);
    const char *const expect[] = { "", "  abcdef", "  345678", "   xyz", "z", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    /* A sub-canvas reaching past its parent is cut to the parent. */
    tui_canvas big = tui_canvas_sub(c, tui_rect_make(-5, -5, 50, 50));
    ASSERT_EQ_INT(tui_canvas_rows(big), 3, "clipped sub rows");
    ASSERT_EQ_INT(tui_canvas_cols(big), 6, "clipped sub cols");
    tui_canvas none = tui_canvas_sub(c, tui_rect_make(10, 10, 2, 2));
    ASSERT_EQ_INT(tui_canvas_rows(none), 0, "outside: empty");
    ASSERT_EQ_INT(tui_canvas_put(none, 0, 0, "x", TUI_STYLE_DEFAULT), 0, "drawing on empty");
    tui_canvas_box(none, tui_rect_make(0, 0, 3, 3), "t", TUI_STYLE_DEFAULT);
    fx_close(&f);
}

static void test_canvas_aligned_and_cursor(void) {
    Fx f;
    ASSERT(fx_open(&f, 3, 11) == 0, "fixtures");
    tui_canvas all = tui_canvas_of(f.scr);
    ASSERT_EQ_INT(tui_canvas_put_aligned(all, 0, "mid", TUI_ALIGN_CENTER, TUI_STYLE_DEFAULT), 4, "centre column");
    ASSERT_EQ_INT(tui_canvas_put_aligned(all, 1, "right", TUI_ALIGN_RIGHT, TUI_STYLE_DEFAULT), 6, "right column");
    ASSERT_EQ_INT(tui_canvas_put_aligned(all, 2, "much too long text", TUI_ALIGN_CENTER, TUI_STYLE_DEFAULT), 0,
                  "too long: from column 0");
    tui_canvas c = tui_canvas_sub(all, tui_rect_make(1, 1, 2, 3));
    tui_canvas_set_cursor(c, 1, 2, 1);
    fx_show(&f);
    const char *const expect[] = { "    mid", "      right", "much too l…", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    int row, col;
    pty_cursor(f.s, &row, &col);
    ASSERT_EQ_INT(row, 2, "cursor row in screen coordinates");
    ASSERT_EQ_INT(col, 3, "cursor column in screen coordinates");
    tui_canvas_set_cursor(c, 5, 0, 1);              /* outside: hidden */
    tui_buf_reset(&f.buf);
    tui_screen_render(f.scr, &f.buf);
    ASSERT(strstr(f.buf.data, "\033[?25l") != NULL, "cursor outside the canvas is hidden");
    char *longtext = malloc(2000);
    ASSERT(longtext != NULL, "allocation");
    memset(longtext, 'w', 1999);
    longtext[1999] = '\0';
    ASSERT_EQ_INT(tui_canvas_put_aligned(all, 0, longtext, TUI_ALIGN_LEFT, TUI_STYLE_DEFAULT), 0,
                  "text longer than the stack buffer");
    free(longtext);
    fx_show(&f);
    const char *const expect2[] = { "wwwwwwwwww…", NULL };
    ASSERT_REGION_EQ(f.s, PTY_REGION(0, 0, 1, 11), expect2);
    fx_close(&f);
}

/* ── List view ──────────────────────────────────────────────────────── */

static void test_listview_navigation(void) {
    tui_listview lv;
    tui_listview_init(&lv);
    ASSERT_EQ_INT(lv.selected, -1, "empty: no selection");
    tui_listview_move_down(&lv);
    ASSERT_EQ_INT(lv.selected, -1, "moving in an empty list does nothing");
    tui_listview_set_viewport(&lv, 4);
    tui_listview_set_count(&lv, 10);
    ASSERT_EQ_INT(lv.selected, 0, "first item selected");
    tui_listview_move_up(&lv);
    ASSERT_EQ_INT(lv.selected, 0, "Up at the top");
    for (int i = 0; i < 5; i++) tui_listview_move_down(&lv);
    ASSERT_EQ_INT(lv.selected, 5, "five down");
    ASSERT_EQ_INT(lv.scroll_top, 2, "scrolled just enough");
    ASSERT(tui_listview_is_visible(&lv, 2) && tui_listview_is_visible(&lv, 5), "2..5 visible");
    ASSERT(!tui_listview_is_visible(&lv, 6) && !tui_listview_is_visible(&lv, 1), "6 and 1 not");
    tui_listview_page_down(&lv);
    ASSERT_EQ_INT(lv.selected, 9, "PgDn");
    tui_listview_page_down(&lv);
    ASSERT_EQ_INT(lv.selected, 9, "PgDn at the end");
    ASSERT_EQ_INT(lv.scroll_top, 6, "last item at the bottom");
    tui_listview_page_up(&lv);
    ASSERT_EQ_INT(lv.selected, 5, "PgUp");
    tui_listview_home(&lv);
    ASSERT_EQ_INT(lv.selected, 0, "Home");
    ASSERT_EQ_INT(lv.scroll_top, 0, "Home scrolls to top");
    tui_listview_end(&lv);
    ASSERT_EQ_INT(lv.selected, 9, "End");
    tui_listview_set_count(&lv, 3);
    ASSERT_EQ_INT(lv.selected, 2, "selection clamped when the list shrinks");
    ASSERT_EQ_INT(lv.scroll_top, 0, "no empty rows with items hidden above");
    tui_listview_set_count(&lv, 0);
    ASSERT_EQ_INT(lv.selected, -1, "emptied");
    tui_listview_select(&lv, 3);
    ASSERT_EQ_INT(lv.selected, -1, "select in an empty list");
    tui_listview_set_count(&lv, 20);
    tui_listview_select(&lv, 99);
    ASSERT_EQ_INT(lv.selected, 19, "select clamped");
    tui_listview_select(&lv, -4);
    ASSERT_EQ_INT(lv.selected, 0, "select clamped below");
    tui_listview_set_viewport(&lv, 0);
    tui_listview_page_down(&lv);
    ASSERT_EQ_INT(lv.selected, 1, "a page is at least one item");
    tui_listview_init(NULL);
    tui_listview_set_count(NULL, 1);
    ASSERT_EQ_INT(tui_listview_is_visible(NULL, 0), 0, "NULL");
}

static void test_listview_keys(void) {
    tui_listview lv;
    tui_listview_init(&lv);
    tui_listview_set_viewport(&lv, 3);
    tui_listview_set_count(&lv, 10);
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    static const tui_key keys[] = { TUI_KEY_DOWN, TUI_KEY_PGDN, TUI_KEY_UP, TUI_KEY_END,
                                    TUI_KEY_PGUP, TUI_KEY_HOME };
    static const int expect[] = { 1, 4, 3, 9, 6, 0 };
    for (int i = 0; i < 6; i++) {
        ev.key = keys[i];
        ASSERT_EQ_INT(tui_listview_handle(&lv, &ev), 1, "navigation key handled");
        ASSERT_EQ_INT(lv.selected, expect[i], "selection after the key");
    }
    /* The workspace's shared letters (policies/tui-interface-model.md):
     * j k, ctrl-f ctrl-b, g G. */
    static const struct { uint32_t cp; unsigned mods; int sel; const char *what; } letters[] = {
        { 'j', 0, 1, "j: down" },          { 'k', 0, 0, "k: up" },
        { 'k', 0, 0, "k at the top: no wrap" },
        { 'f', TUI_MOD_CTRL, 3, "ctrl-f: a page down" },
        { 'b', TUI_MOD_CTRL, 0, "ctrl-b: a page up" },
        { 'G', 0, 9, "G: the last row" }, { 'j', 0, 9, "j at the end: no wrap" },
        { 'g', 0, 0, "g: the first row" },
    };
    for (size_t i = 0; i < sizeof(letters) / sizeof(letters[0]); i++) {
        ev.key = TUI_KEY_CHAR;
        ev.cp = letters[i].cp;
        ev.mods = letters[i].mods;
        ASSERT_EQ_INT(tui_listview_handle(&lv, &ev), 1, letters[i].what);
        ASSERT_EQ_INT(lv.selected, letters[i].sel, letters[i].what);
    }
    ev.mods = 0;
    ev.cp = 'x';
    ASSERT_EQ_INT(tui_listview_handle(&lv, &ev), 0, "other letters are the program's");
    ev.cp = 'J';
    ASSERT_EQ_INT(tui_listview_handle(&lv, &ev), 0, "J is not j");
    ev.cp = 'j';
    ev.mods = TUI_MOD_ALT;
    ASSERT_EQ_INT(tui_listview_handle(&lv, &ev), 0, "Alt+j is not j");
    ev.cp = 'g';
    ev.mods = TUI_MOD_CTRL;
    ASSERT_EQ_INT(tui_listview_handle(&lv, &ev), 0, "ctrl-g is not g");
    ev.key = TUI_KEY_DOWN;
    ev.mods = TUI_MOD_CTRL;
    ASSERT_EQ_INT(tui_listview_handle(&lv, &ev), 0, "Ctrl+Down is not plain Down");
    ev.type = TUI_EV_RESIZE;
    ASSERT_EQ_INT(tui_listview_handle(&lv, &ev), 0, "not a key");
}

static const char *const g_names[] = { "Alice", "Bob", "Carol", "Dave", "Eve", "Frank" };

static void draw_name(void *ctx, tui_canvas row, int index, int selected) {
    int *calls = ctx;
    (*calls)++;
    tui_style st = selected ? tui_style_attrs(TUI_ATTR_REVERSE) : TUI_STYLE_DEFAULT;
    if (selected) tui_canvas_clear(row, st);
    char line[32];
    snprintf(line, sizeof(line), "%d %s", index, g_names[index]);
    tui_canvas_put(row, 0, 0, line, st);
}

static void test_listview_render(void) {
    Fx f;
    ASSERT(fx_open(&f, 5, 10) == 0, "fixtures");
    tui_canvas all = tui_canvas_of(f.scr);
    tui_canvas list = tui_canvas_sub(all, tui_rect_make(1, 1, 3, 8));
    tui_listview lv;
    tui_listview_init(&lv);
    tui_listview_set_count(&lv, 6);
    tui_listview_select(&lv, 4);
    int calls = 0;
    tui_listview_render(&lv, list, TUI_STYLE_DEFAULT, draw_name, &calls);
    ASSERT_EQ_INT(lv.rows_visible, 3, "viewport from the canvas");
    ASSERT_EQ_INT(calls, 3, "one call per visible row");
    fx_show(&f);
    const char *const expect[] = { "", " 2 Carol", " 3 Dave", " 4 Eve", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    static const PtyStyleKey legend[] = {
        { '.', PTY_ATTR_NONE, 0, 0 }, { 'R', PTY_ATTR_REVERSE, 0, 0 }, { 0, 0, 0, 0 } };
    const char *const map[] = { ".RRRRRRRR.", NULL };
    ASSERT_REGION_STYLE(f.s, PTY_REGION(3, 0, 1, 10), map, legend);

    /* Fewer items than rows: the rest is cleared. */
    tui_listview_set_count(&lv, 1);
    calls = 0;
    tui_listview_render(&lv, list, TUI_STYLE_DEFAULT, draw_name, &calls);
    ASSERT_EQ_INT(calls, 1, "one item drawn");
    fx_show(&f);
    const char *const expect2[] = { "", " 0 Alice", "", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect2);
    tui_listview_render(&lv, list, TUI_STYLE_DEFAULT, NULL, NULL);
    tui_listview_render(NULL, list, TUI_STYLE_DEFAULT, draw_name, &calls);
    fx_close(&f);
}

/* ── Text view ──────────────────────────────────────────────────────── */

static void test_textview_wrap(void) {
    tui_textview tv;
    tui_textview_init(&tv);
    ASSERT_EQ_INT(tui_textview_set_text(&tv, "hello world and more\n\nshort\r\nabcdefghijkl"), 0, "set");
    ASSERT_EQ_INT(tui_textview_wrap(&tv, 8), 7, "line count at width 8");
    Fx f;
    ASSERT(fx_open(&f, 8, 8) == 0, "fixtures");
    tui_textview_render(&tv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    const char *const expect[] = {
        "hello", "world", "and more", "", "short", "abcdefgh", "ijkl", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    ASSERT_EQ_INT(tui_textview_lines(&tv), 7, "lines");
    ASSERT(tui_textview_at_end(&tv), "everything shown: at the end");

    ASSERT_EQ_INT(tui_textview_set_text(&tv, "\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97 x\ty\n"
                                             "e\xCC\x81" "e\xCC\x81" "e\xCC\x81" "e\xCC\x81\n"), 0, "set");
    ASSERT_EQ_INT(tui_textview_wrap(&tv, 5), 4, "wide characters and a tab");
    tui_textview_render(&tv, tui_canvas_sub(tui_canvas_of(f.scr), tui_rect_make(0, 0, 8, 5)),
                        TUI_STYLE_DEFAULT);
    tui_canvas_clear(tui_canvas_sub(tui_canvas_of(f.scr), tui_rect_make(0, 5, 8, 3)), TUI_STYLE_DEFAULT);
    fx_show(&f);
    const char *const expect2[] = {
        "\xE4\xB8\xAD\xE6\x96\x87", "\xE5\xAD\x97 x", "y",
        "e\xCC\x81" "e\xCC\x81" "e\xCC\x81" "e\xCC\x81", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect2);
    fx_close(&f);
    tui_textview_free(&tv);
}

static void test_textview_tabs_and_controls(void) {
    tui_textview tv;
    tui_textview_init(&tv);
    tui_textview_set_text(&tv, "a\tb\tc\n\x1b[31mred");
    Fx f;
    ASSERT(fx_open(&f, 2, 20) == 0, "fixtures");
    tui_textview_render(&tv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    const char *const expect[] = { "a       b       c", "\xC2\xB7[31mred", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    fx_close(&f);
    tui_textview_free(&tv);
}

static void test_textview_scrolling_and_follow(void) {
    tui_textview tv;
    tui_textview_init(&tv);
    char text[512] = "";
    for (int i = 0; i < 20; i++) {
        char line[16];
        snprintf(line, sizeof(line), "line %d\n", i);
        strcat(text, line);
    }
    tui_textview_set_text(&tv, text);
    Fx f;
    ASSERT(fx_open(&f, 4, 10) == 0, "fixtures");
    tui_canvas all = tui_canvas_of(f.scr);
    tui_textview_render(&tv, all, TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_textview_top(&tv), 0, "set_text starts at the top (a mail body)");
    ASSERT(!tui_textview_at_end(&tv), "not at the end");
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = TUI_KEY_PGDN;
    ASSERT_EQ_INT(tui_textview_handle(&tv, &ev), 1, "PgDn handled");
    ASSERT_EQ_INT(tui_textview_top(&tv), 3, "a page keeps one line of context");
    ev.key = TUI_KEY_END;
    tui_textview_handle(&tv, &ev);
    ASSERT_EQ_INT(tui_textview_top(&tv), 16, "End: last line at the bottom");
    ASSERT(tui_textview_at_end(&tv), "at the end");
    ev.key = TUI_KEY_DOWN;
    tui_textview_handle(&tv, &ev);
    ASSERT_EQ_INT(tui_textview_top(&tv), 16, "cannot scroll past the end");

    /* At the end, appended lines are followed. */
    tui_textview_append(&tv, "line 20\nline 21\n", 16);
    tui_textview_render(&tv, all, TUI_STYLE_DEFAULT);
    fx_show(&f);
    const char *const expect[] = { "line 18", "line 19", "line 20", "line 21", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);

    /* Scrolled up, the view stays where the user put it. */
    ev.key = TUI_KEY_UP;
    tui_textview_handle(&tv, &ev);
    tui_textview_handle(&tv, &ev);
    int top = tui_textview_top(&tv);
    tui_textview_append(&tv, "line 22\n", 8);
    tui_textview_render(&tv, all, TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_textview_top(&tv), top, "not moved under the reader");

    /* Resizing keeps the end in view for a view at the end. */
    ev.key = TUI_KEY_END;
    tui_textview_handle(&tv, &ev);
    tui_textview_render(&tv, all, TUI_STYLE_DEFAULT);
    tui_canvas smaller = tui_canvas_sub(all, tui_rect_make(0, 0, 2, 10));
    tui_textview_render(&tv, smaller, TUI_STYLE_DEFAULT);
    tui_canvas_clear(tui_canvas_sub(all, tui_rect_make(2, 0, 2, 10)), TUI_STYLE_DEFAULT);
    fx_show(&f);
    const char *const expect2[] = { "line 21", "line 22", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect2);

    ev.key = TUI_KEY_HOME;
    tui_textview_handle(&tv, &ev);
    ASSERT_EQ_INT(tui_textview_top(&tv), 0, "Home");
    ev.key = TUI_KEY_PGUP;
    tui_textview_handle(&tv, &ev);
    ASSERT_EQ_INT(tui_textview_top(&tv), 0, "PgUp at the top");
    tui_textview_scroll_to(&tv, 5);
    ASSERT_EQ_INT(tui_textview_top(&tv), 5, "scroll_to");
    tui_textview_scroll(&tv, -100);
    ASSERT_EQ_INT(tui_textview_top(&tv), 0, "scroll clamped");
    ev.key = TUI_KEY_CHAR;
    ASSERT_EQ_INT(tui_textview_handle(&tv, &ev), 0, "other keys not handled");

    /* The shared letters: j k a line, ctrl-f ctrl-b a page, g G the ends. */
    tui_textview_scroll_to(&tv, 0);
    tui_textview_render(&tv, all, TUI_STYLE_DEFAULT);
    static const struct { uint32_t cp; unsigned mods; int top; const char *what; } letters[] = {
        { 'j', 0, 1, "j: a line down" },  { 'k', 0, 0, "k: a line up" },
        { 'f', TUI_MOD_CTRL, 3, "ctrl-f: a page, one line of context" },
        { 'b', TUI_MOD_CTRL, 0, "ctrl-b: a page up" },
        { 'G', 0, 19, "G: the end" },     { 'g', 0, 0, "g: the top" },
    };
    for (size_t i = 0; i < sizeof(letters) / sizeof(letters[0]); i++) {
        ev.key = TUI_KEY_CHAR;
        ev.cp = letters[i].cp;
        ev.mods = letters[i].mods;
        ASSERT_EQ_INT(tui_textview_handle(&tv, &ev), 1, letters[i].what);
        ASSERT_EQ_INT(tui_textview_top(&tv), letters[i].top, letters[i].what);
    }
    ev.cp = 'e';
    ASSERT_EQ_INT(tui_textview_handle(&tv, &ev), 0, "ctrl-e is the program's");
    ev.mods = 0;
    ev.type = TUI_EV_RESIZE;
    ASSERT_EQ_INT(tui_textview_handle(&tv, &ev), 0, "not a key");
    ev.type = TUI_EV_KEY;

    /* Re-wrapping keeps the top line in place. */
    tui_textview_scroll_to(&tv, 10);
    tui_canvas narrow = tui_canvas_sub(all, tui_rect_make(0, 0, 2, 4));
    tui_textview_render(&tv, narrow, TUI_STYLE_DEFAULT);
    fx_show(&f);
    const char *const expect3[] = { "line", "10", NULL };
    ASSERT_REGION_EQ(f.s, PTY_REGION(0, 0, 2, 4), expect3);
    fx_close(&f);
    tui_textview_free(&tv);
}

static void test_textview_append_to_empty_and_errors(void) {
    tui_textview tv;
    tui_textview_init(&tv);
    ASSERT_EQ_INT(tui_textview_lines(&tv), 0, "empty");
    ASSERT_EQ_INT(tui_textview_append(&tv, "a\nb\nc\nd\n", 8), 0, "append");
    Fx f;
    ASSERT(fx_open(&f, 2, 5) == 0, "fixtures");
    tui_textview_render(&tv, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    const char *const expect[] = { "c", "d", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    ASSERT_EQ_INT(tui_textview_append(&tv, NULL, 3), -1, "NULL text");
    ASSERT_EQ_INT(tui_textview_append(&tv, "", 0), 0, "nothing");
    ASSERT_EQ_INT(tui_textview_set_text(NULL, "x"), -1, "NULL view");
    ASSERT_EQ_INT(tui_textview_set_text(&tv, NULL), 0, "NULL text is empty");
    ASSERT_EQ_INT(tui_textview_wrap(&tv, 0), 0, "empty text, width 0");
    ASSERT_EQ_INT(tui_textview_wrap(NULL, 3), -1, "wrap NULL");
    ASSERT_EQ_INT(tui_textview_lines(NULL), 0, "lines of NULL");
    ASSERT_EQ_INT(tui_textview_top(NULL), 0, "top of NULL");
    ASSERT(tui_textview_at_end(NULL), "NULL is at its end");
    tui_textview_scroll(NULL, 1);
    tui_textview_scroll_to(NULL, 1);
    tui_textview_scroll_end(NULL);
    tui_textview_render(NULL, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_textview_handle(NULL, NULL), 0, "handle NULL");
    /* A very long word at width 1, and a line longer than the segment
     * buffer used for drawing. */
    char *big = malloc(3000);
    ASSERT(big != NULL, "allocation");
    memset(big, 'k', 2999);
    big[2999] = '\0';
    tui_textview_set_text(&tv, big);
    ASSERT_EQ_INT(tui_textview_wrap(&tv, 1), 2999, "one character per line");
    ASSERT_EQ_INT(tui_textview_wrap(&tv, 3000), 1, "one line");
    /* Spaces at the end of a line that breaks there are eaten by the break. */
    tui_textview_set_text(&tv, "hello        ");
    ASSERT_EQ_INT(tui_textview_wrap(&tv, 5), 1, "trailing spaces make no extra line");
    free(big);
    fx_close(&f);
    tui_textview_free(&tv);
    tui_textview_free(NULL);
}

static const TestCase tests[] = {
    TEST_CASE(test_rect_layout),
    TEST_CASE(test_three_pane_layout),
    TEST_CASE(test_canvas_clipping),
    TEST_CASE(test_canvas_aligned_and_cursor),
    TEST_CASE(test_listview_navigation),
    TEST_CASE(test_listview_keys),
    TEST_CASE(test_listview_render),
    TEST_CASE(test_textview_wrap),
    TEST_CASE(test_textview_tabs_and_controls),
    TEST_CASE(test_textview_scrolling_and_follow),
    TEST_CASE(test_textview_append_to_empty_and_errors),
};

int main(int argc, char **argv) {
    return RUN_SUITE("widgets", tests, 160, 60);
}
