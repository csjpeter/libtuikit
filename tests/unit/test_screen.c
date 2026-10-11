/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Unit tests of tui_screen. */

/*
 * Unit tests of tui_screen.  Every render is checked twice, independently:
 *   - the exact bytes, against sequences written out from xterm's
 *     documentation (CUP, SGR, ED, DECTCEM);
 *   - the same bytes fed to libptytest's emulator with pty_feed(), and the
 *     resulting screen compared cell by cell.
 * A wrong rule shared by renderer and emulator would pass the second check;
 * it cannot pass the first.
 */

#include "test_helpers.h"
#include "tuikit/screen.h"

#include <errno.h>

int g_tests_run = 0;
int g_tests_failed = 0;

#define SYNC_ON  "\033[?2026h"
#define SYNC_OFF "\033[?2026l"
#define CLEAR    "\033[0m\033[H\033[2J"

/* Renders into a fresh buffer and feeds the bytes to the emulator. */
static long render_feed(tui_screen *scr, PtySession *s, tui_buf *b) {
    tui_buf_reset(b);
    long n = tui_screen_render(scr, b);
    if (n > 0) pty_feed(s, b->data, b->len);
    return n;
}

static void test_new_and_errors(void) {
    errno = 0;
    ASSERT(tui_screen_new(0, 10) == NULL, "zero rows refused");
    ASSERT_EQ_INT(errno, EINVAL, "errno");
    ASSERT(tui_screen_new(10, -1) == NULL, "negative cols refused");
    ASSERT(tui_screen_new(1 << 30, 1 << 30) == NULL, "absurd size refused");
    tui_screen *s = tui_screen_new(3, 7);
    ASSERT(s != NULL, "3x7");
    ASSERT_EQ_INT(tui_screen_rows(s), 3, "rows");
    ASSERT_EQ_INT(tui_screen_cols(s), 7, "cols");
    ASSERT_EQ_INT(tui_screen_rows(NULL), 0, "rows of NULL");
    ASSERT_EQ_INT(tui_screen_cols(NULL), 0, "cols of NULL");
    tui_buf b;
    tui_buf_init(&b);
    ASSERT_EQ_INT(tui_screen_render(NULL, &b), -1, "render NULL screen");
    ASSERT_EQ_INT(tui_screen_render(s, NULL), -1, "render into NULL");
    ASSERT_EQ_INT(tui_screen_resize(NULL, 2, 2), -1, "resize NULL");
    ASSERT_EQ_INT(tui_screen_resize(s, 0, 2), -1, "resize to zero");
    ASSERT_EQ_INT(tui_screen_rows(s), 3, "failed resize leaves the size");
    tui_screen_free(s);
    tui_screen_free(NULL);
    tui_buf_free(&b);
}

static void test_first_frame_bytes_and_screen(void) {
    tui_screen *scr = tui_screen_new(3, 10);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(10, 3);
    ASSERT(scr && s, "fixtures");
    pty_feed(s, "garbage!!!", 10);       /* what the terminal showed before */
    ASSERT_EQ_INT(tui_screen_put(scr, 1, 2, 0, "Hello", TUI_STYLE_DEFAULT), 5, "columns written");
    long n = render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len,
                 SYNC_ON CLEAR "\033[2;3HHello\033[?25l" SYNC_OFF,
                 "first frame: clear, then only the non-blank cells");
    ASSERT_EQ_INT(n, (long)b.len, "return value is the byte count");
    const char *const expect[] = { "", "  Hello", "", NULL };
    ASSERT_SCREEN_EQ(s, expect);

    ASSERT_EQ_INT(render_feed(scr, s, &b), 0, "no change, no bytes");
    ASSERT_EQ_INT(b.len, 0, "buffer untouched");

    tui_screen_put(scr, 1, 3, 0, "a", TUI_STYLE_DEFAULT);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, SYNC_ON "\033[2;4Ha" SYNC_OFF, "one cell changed: one cell written");
    const char *const expect2[] = { "", "  Hallo", "", NULL };
    ASSERT_SCREEN_EQ(s, expect2);

    tui_screen_put(scr, 0, 0, 0, "ab", TUI_STYLE_DEFAULT);
    tui_screen_put(scr, 0, 5, 0, "x", TUI_STYLE_DEFAULT);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, SYNC_ON "\033[1;1Hab\033[1;6Hx" SYNC_OFF,
                 "adjacent cells share one position; a gap gets a new one");
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

static void test_styles(void) {
    tui_screen *scr = tui_screen_new(2, 20);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(20, 2);
    ASSERT(scr && s, "fixtures");
    tui_screen_set_sync(scr, 0);
    tui_screen_put(scr, 0, 0, 0, "B", tui_style_make(TUI_RED, TUI_BLUE, TUI_ATTR_BOLD));
    tui_screen_put(scr, 0, 1, 0, "u", tui_style_attrs(TUI_ATTR_UNDERLINE | TUI_ATTR_ITALIC));
    tui_screen_put(scr, 0, 2, 0, "R", tui_style_attrs(TUI_ATTR_REVERSE | TUI_ATTR_DIM));
    tui_screen_put(scr, 0, 3, 0, "b", tui_style_make(TUI_BRIGHT_YELLOW, TUI_BRIGHT_BLACK, 0));
    tui_screen_put(scr, 0, 4, 0, "i", tui_style_make(TUI_COLOR_256(208), TUI_COLOR_256(17), 0));
    tui_screen_put(scr, 0, 5, 0, "t", tui_style_make(TUI_COLOR_RGB(1, 2, 3), TUI_COLOR_RGB(250, 128, 0), 0));
    tui_screen_put(scr, 0, 6, 0, "d", TUI_STYLE_DEFAULT);
    tui_screen_put(scr, 0, 7, 0, "ee", tui_style_attrs(TUI_ATTR_BOLD));
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len,
                 CLEAR ""
                 "\033[0;1;31;44mB"
                 "\033[0;3;4mu"
                 "\033[0;2;7mR"
                 "\033[0;93;100mb"
                 "\033[0;38;5;208;48;5;17mi"
                 "\033[0;38;2;1;2;3;48;2;250;128;0mt"
                 "\033[0md"
                 "\033[0;1mee"
                 "\033[0m\033[?25l",
                 "SGR: reset first, attributes, then fg and bg; reset at the end");
    static const PtyStyleKey legend[] = {
        { '.', PTY_ATTR_NONE, 0, 0 },
        { 'B', PTY_ATTR_BOLD, PTY_FG_RED, PTY_BG_BLUE },
        { 'u', PTY_ATTR_UNDERLINE | PTY_ATTR_ITALIC, 0, 0 },
        { 'R', PTY_ATTR_REVERSE | PTY_ATTR_DIM, 0, 0 },
        { 'b', PTY_ATTR_NONE, 93, 100 },
        { 'i', PTY_ATTR_NONE, PTY_COLOR_IDX(208), PTY_COLOR_IDX(17) },
        { 't', PTY_ATTR_NONE, PTY_COLOR_RGB(1, 2, 3), PTY_COLOR_RGB(250, 128, 0) },
        { 'e', PTY_ATTR_BOLD, 0, 0 },
        { 0, 0, 0, 0 } };
    const char *const map[] = { "BuRbit.ee..........", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 1, 19), map, legend);
    const char *const text[] = { "BuRbitdee", NULL };
    ASSERT_SCREEN_EQ(s, text);

    /* A changed style alone is a change. */
    tui_screen_put(scr, 0, 6, 0, "d", tui_style_attrs(TUI_ATTR_REVERSE));
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, "\033[1;7H\033[0;7md\033[0m", "style change redraws the cell");
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

/* The workspace palette (policies/tui-interface-model.md, "Palette"):
 * each role is the exact colours and attribute the policy names. */
static void test_palette(void) {
    tui_screen *scr = tui_screen_new(1, 10);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(10, 1);
    ASSERT(scr && s, "fixtures");
    tui_screen_set_sync(scr, 0);
    tui_screen_put(scr, 0, 0, 0, "H", TUI_STYLE_HEADER);
    tui_screen_put(scr, 0, 1, 0, "S", TUI_STYLE_SELECTION);
    tui_screen_put(scr, 0, 2, 0, "A", TUI_STYLE_ACCENT);
    tui_screen_put(scr, 0, 3, 0, "O", TUI_STYLE_OK);
    tui_screen_put(scr, 0, 4, 0, "E", TUI_STYLE_ERROR);
    tui_screen_put(scr, 0, 5, 0, "h", TUI_STYLE_HINT);
    tui_screen_put(scr, 0, 6, 0, "p", TUI_STYLE_PROGRESS);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len,
                 CLEAR ""
                 "\033[0;1;37;44mH"
                 "\033[0;1;7mS"
                 "\033[0;1;36mA"
                 "\033[0;1;32mO"
                 "\033[0;1;31mE"
                 "\033[0;33mh"
                 "\033[0;3;33mp"
                 "\033[0m\033[?25l",
                 "the seven roles");
    static const PtyStyleKey legend[] = {
        { '.', PTY_ATTR_NONE, 0, 0 },
        { 'H', PTY_ATTR_BOLD, PTY_FG_WHITE, PTY_BG_BLUE },
        { 'S', PTY_ATTR_BOLD | PTY_ATTR_REVERSE, 0, 0 },
        { 'A', PTY_ATTR_BOLD, PTY_FG_CYAN, PTY_BG_DEFAULT },
        { 'O', PTY_ATTR_BOLD, PTY_FG_GREEN, PTY_BG_DEFAULT },
        { 'E', PTY_ATTR_BOLD, PTY_FG_RED, PTY_BG_DEFAULT },
        { 'h', PTY_ATTR_NONE, PTY_FG_YELLOW, PTY_BG_DEFAULT },
        { 'p', PTY_ATTR_ITALIC, PTY_FG_YELLOW, PTY_BG_DEFAULT },
        { 0, 0, 0, 0 } };
    const char *const map[] = { "HSAOEhp...", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 1, 10), map, legend);
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

static void test_clear_with_background(void) {
    tui_screen *scr = tui_screen_new(2, 4);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(4, 2);
    ASSERT(scr && s, "fixtures");
    tui_screen_set_sync(scr, 0);
    tui_screen_clear(scr, tui_style_make(TUI_COLOR_DEFAULT, TUI_GREEN, 0));
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len,
                 CLEAR "\033[0;42m    \033[2;1H    \033[0m\033[?25l",
                 "every coloured blank is written");
    static const PtyStyleKey legend[] = {
        { 'g', PTY_ATTR_NONE, 0, PTY_BG_GREEN }, { 0, 0, 0, 0 } };
    const char *const map[] = { "gggg", "gggg", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 2, 4), map, legend);
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

static void test_wide_characters(void) {
    tui_screen *scr = tui_screen_new(2, 6);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(6, 2);
    ASSERT(scr && s, "fixtures");
    tui_screen_set_sync(scr, 0);
    /* 中文 is four columns; a third wide character does not fit in two. */
    ASSERT_EQ_INT(tui_screen_put(scr, 0, 0, 0, "\xE4\xB8\xAD\xE6\x96\x87x", TUI_STYLE_DEFAULT), 5,
                  "two wide and one narrow");
    ASSERT_EQ_INT(tui_screen_put(scr, 1, 0, 5, "ab\xE4\xB8\xAD\xE6\x96\x87", TUI_STYLE_DEFAULT), 4,
                  "a wide character is not drawn by halves");
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len,
                 CLEAR "\xE4\xB8\xAD\xE6\x96\x87x\033[2;1Hab\xE4\xB8\xAD\033[?25l",
                 "wide characters written once, cursor advanced by two");
    const char *const expect[] = { "\xE4\xB8\xAD\xE6\x96\x87x", "ab\xE4\xB8\xAD", NULL };
    ASSERT_SCREEN_EQ(s, expect);

    /* Overwriting the second half of 中 blanks its first half. */
    tui_screen_put(scr, 0, 1, 0, "Z", TUI_STYLE_DEFAULT);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, "\033[1;1H Z", "both halves redrawn");
    const char *const expect2[] = { " Z\xE6\x96\x87x", "ab\xE4\xB8\xAD", NULL };
    ASSERT_SCREEN_EQ(s, expect2);

    /* A narrow character over the first half of 文 blanks the second. */
    tui_screen_put(scr, 0, 2, 0, "Q", TUI_STYLE_DEFAULT);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, "\033[1;3HQ ", "first half replaced, second blanked");
    const char *const expect3[] = { " ZQ x", "ab\xE4\xB8\xAD", NULL };
    ASSERT_SCREEN_EQ(s, expect3);

    /* A wide character over two narrow ones, and over the trailer of
     * another wide character. */
    tui_screen_put(scr, 1, 1, 0, "\xE5\xAD\x97", TUI_STYLE_DEFAULT);   /* 字 at col 1 */
    render_feed(scr, s, &b);
    const char *const expect4[] = { " ZQ x", "a\xE5\xAD\x97 ", NULL };
    ASSERT_SCREEN_EQ(s, expect4);
    ASSERT_BYTES(b.data, b.len, "\033[2;2H\xE5\xAD\x97 ", "wide written, broken neighbour blanked");
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

static void test_combining_and_controls(void) {
    tui_screen *scr = tui_screen_new(1, 10);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(10, 1);
    ASSERT(scr && s, "fixtures");
    tui_screen_set_sync(scr, 0);
    /* e + combining acute, then a control and an escape sequence. */
    ASSERT_EQ_INT(tui_screen_put(scr, 0, 0, 0, "e\xCC\x81" "a\x1b[2Jb\xC2\x9B", TUI_STYLE_DEFAULT), 8,
                  "controls take one column each");
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len,
                 CLEAR "e\xCC\x81" "a\xC2\xB7[2Jb\xC2\xB7\033[?25l",
                 "mark drawn with its base; ESC and U+009B drawn as U+00B7");
    const char *const expect[] = { "e\xCC\x81" "a\xC2\xB7[2Jb\xC2\xB7", NULL };
    ASSERT_SCREEN_EQ(s, expect);
    /* A mark with no base in its text is not drawn, and touches nothing. */
    ASSERT_EQ_INT(tui_screen_put(scr, 0, 1, 0, "\xCC\x81", TUI_STYLE_DEFAULT), 0, "lone mark");
    ASSERT_EQ_INT(render_feed(scr, s, &b), 0, "nothing changed");
    /* More marks than a cell keeps. */
    tui_screen_put(scr, 0, 0, 0, "o\xCC\x81\xCC\x88\xCC\xA3", TUI_STYLE_DEFAULT);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, "\033[1;1Ho\xCC\x81\xCC\x88", "two marks kept");
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

static void test_clipping(void) {
    tui_screen *scr = tui_screen_new(2, 5);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(5, 2);
    ASSERT(scr && s, "fixtures");
    ASSERT_EQ_INT(tui_screen_put(scr, 0, 3, 0, "abcdef", TUI_STYLE_DEFAULT), 2, "clipped at the edge");
    ASSERT_EQ_INT(tui_screen_put(scr, 1, 0, 2, "abcdef", TUI_STYLE_DEFAULT), 2, "clipped by max_cols");
    ASSERT_EQ_INT(tui_screen_put(scr, 2, 0, 0, "x", TUI_STYLE_DEFAULT), 0, "row below the screen");
    ASSERT_EQ_INT(tui_screen_put(scr, -1, 0, 0, "x", TUI_STYLE_DEFAULT), 0, "row above");
    ASSERT_EQ_INT(tui_screen_put(scr, 0, -1, 0, "x", TUI_STYLE_DEFAULT), 0, "negative column");
    ASSERT_EQ_INT(tui_screen_put(scr, 0, 5, 0, "x", TUI_STYLE_DEFAULT), 0, "column past the edge");
    ASSERT_EQ_INT(tui_screen_put(scr, 0, 0, 0, NULL, TUI_STYLE_DEFAULT), 0, "NULL text");
    ASSERT_EQ_INT(tui_screen_put(NULL, 0, 0, 0, "x", TUI_STYLE_DEFAULT), 0, "NULL screen");
    tui_screen_fill(scr, 1, 3, 10, '-', TUI_STYLE_DEFAULT);
    tui_screen_fill(scr, 0, -2, 3, '#', TUI_STYLE_DEFAULT);
    tui_screen_fill(scr, 5, 0, 3, '#', TUI_STYLE_DEFAULT);
    tui_screen_fill(scr, 0, 0, 0, '#', TUI_STYLE_DEFAULT);
    tui_screen_fill(NULL, 0, 0, 3, '#', TUI_STYLE_DEFAULT);
    render_feed(scr, s, &b);
    const char *const expect[] = { "#  ab", "ab --", NULL };
    ASSERT_SCREEN_EQ(s, expect);
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

static void test_fill_characters(void) {
    tui_screen *scr = tui_screen_new(1, 6);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(6, 1);
    ASSERT(scr && s, "fixtures");
    tui_screen_put(scr, 0, 0, 0, "\xE4\xB8\xAD\xE4\xB8\xAD", TUI_STYLE_DEFAULT);
    tui_screen_fill(scr, 0, 1, 2, 0x2500, TUI_STYLE_DEFAULT);       /* ─ over two halves */
    tui_screen_fill(scr, 0, 4, 2, 0x4E2D, TUI_STYLE_DEFAULT);       /* wide: spaces */
    tui_screen_fill(scr, 0, 5, 1, 0x1B, TUI_STYLE_DEFAULT);         /* control: U+00B7 */
    render_feed(scr, s, &b);
    const char *const expect[] = { " \xE2\x94\x80\xE2\x94\x80  \xC2\xB7", NULL };
    ASSERT_SCREEN_EQ(s, expect);
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

static void test_cursor(void) {
    tui_screen *scr = tui_screen_new(3, 10);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(10, 3);
    ASSERT(scr && s, "fixtures");
    tui_screen_set_sync(scr, 0);
    tui_screen_put(scr, 0, 0, 0, "abc", TUI_STYLE_DEFAULT);
    tui_screen_set_cursor(scr, 2, 4, 1);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, CLEAR "abc\033[3;5H\033[?25h", "cursor placed and shown");
    int row = -1, col = -1;
    pty_cursor(s, &row, &col);
    ASSERT_EQ_INT(row, 2, "cursor row on the emulated screen");
    ASSERT_EQ_INT(col, 4, "cursor column");

    ASSERT_EQ_INT(render_feed(scr, s, &b), 0, "unchanged: nothing");
    tui_screen_set_cursor(scr, 1, 1, 1);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, "\033[2;2H", "moved cursor alone");
    tui_screen_put(scr, 0, 5, 0, "z", TUI_STYLE_DEFAULT);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, "\033[1;6Hz\033[2;2H", "drawing moves it back afterwards");
    tui_screen_set_cursor(scr, 1, 1, 0);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, "\033[?25l", "hidden");
    tui_screen_set_cursor(scr, 99, -5, 1);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, "\033[3;1H\033[?25h", "clamped to the screen");
    tui_screen_set_cursor(NULL, 0, 0, 1);
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

static void test_invalidate_and_resize(void) {
    tui_screen *scr = tui_screen_new(2, 4);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(4, 2);
    ASSERT(scr && s, "fixtures");
    tui_screen_set_sync(scr, 0);
    tui_screen_put(scr, 0, 0, 0, "ab", TUI_STYLE_DEFAULT);
    render_feed(scr, s, &b);
    pty_feed(s, "\033[2;1HLOG LINE", 11);          /* foreign output */
    tui_screen_invalidate(scr);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, CLEAR "ab\033[?25l", "everything again after invalidate");
    const char *const expect[] = { "ab", "", NULL };
    ASSERT_SCREEN_EQ(s, expect);

    ASSERT_EQ_INT(tui_screen_resize(scr, 3, 6), 0, "resize");
    ASSERT_EQ_INT(tui_screen_rows(scr), 3, "rows");
    ASSERT_EQ_INT(tui_screen_cols(scr), 6, "cols");
    tui_screen_put(scr, 2, 5, 0, "z", TUI_STYLE_DEFAULT);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, CLEAR "\033[3;6Hz\033[?25l", "cleared and drawn whole");
    tui_screen_invalidate(NULL);
    tui_screen_set_sync(NULL, 1);
    tui_screen_clear(NULL, TUI_STYLE_DEFAULT);
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

static void test_last_cell_and_position_after_edge(void) {
    tui_screen *scr = tui_screen_new(2, 3);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(3, 2);
    ASSERT(scr && s, "fixtures");
    tui_screen_set_sync(scr, 0);
    tui_screen_put(scr, 0, 0, 0, "abc", TUI_STYLE_DEFAULT);
    tui_screen_put(scr, 1, 0, 0, "def", TUI_STYLE_DEFAULT);
    render_feed(scr, s, &b);
    ASSERT_BYTES(b.data, b.len, CLEAR "abc\033[2;1Hdef\033[?25l",
                 "a new row is positioned explicitly, never by wrapping");
    const char *const expect[] = { "abc", "def", NULL };
    ASSERT_SCREEN_EQ(s, expect);
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

static void test_bidi_controls_are_dropped(void) {
    static const char *const cases[] = {
        "A\xe2\x80\xae" "B",       /* U+202E right-to-left override */
        "A\xe2\x80\xaa" "B",       /* U+202A */
        "A\xe2\x80\xad" "B",       /* U+202D */
        "A\xe2\x81\xa6" "B",       /* U+2066 isolate */
        "A\xe2\x81\xa9" "B",       /* U+2069 */
        "A\xe2\x80\x8e" "B",       /* U+200E */
        "A\xe2\x80\x8f" "B",       /* U+200F */
        "A\xd8\x9c" "B",            /* U+061C */
        "\xe2\x80\xae" "AB",       /* first in the text: nothing to join */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        tui_screen *scr = tui_screen_new(1, 6);
        tui_buf b;
        tui_buf_init(&b);
        PtySession *s = pty_open(6, 1);
        ASSERT(scr && s, "fixtures");
        ASSERT_EQ_INT(tui_screen_put(scr, 0, 0, 0, cases[i], TUI_STYLE_DEFAULT), 2,
                      "a bidi control takes no column");
        render_feed(scr, s, &b);
        ASSERT_BYTES(b.data, b.len, SYNC_ON CLEAR "AB\033[?25l" SYNC_OFF,
                     "no byte of the control reaches the terminal");
        const char *const expect[] = { "AB", NULL };
        ASSERT_SCREEN_EQ(s, expect);
        pty_close(s);
        tui_screen_free(scr);
        tui_buf_free(&b);
    }
}

static void test_failed_buffer(void) {
    tui_screen *scr = tui_screen_new(1, 4);
    tui_buf b;
    tui_buf_init(&b);
    tui_screen_put(scr, 0, 0, 0, "ab", TUI_STYLE_DEFAULT);
    b.failed = 1;
    errno = 0;
    ASSERT_EQ_INT(tui_screen_render(scr, &b), -1, "render into a failed buffer");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    tui_buf_reset(&b);
    tui_screen_render(scr, &b);
    ASSERT(strstr(b.data, "\033[2J") != NULL, "next render is a full one");
    tui_screen_free(scr);
    tui_buf_free(&b);
}

static const TestCase tests[] = {
    TEST_CASE(test_new_and_errors),
    TEST_CASE(test_first_frame_bytes_and_screen),
    TEST_CASE(test_styles),
    TEST_CASE(test_palette),
    TEST_CASE(test_clear_with_background),
    TEST_CASE(test_wide_characters),
    TEST_CASE(test_combining_and_controls),
    TEST_CASE(test_clipping),
    TEST_CASE(test_fill_characters),
    TEST_CASE(test_cursor),
    TEST_CASE(test_invalidate_and_resize),
    TEST_CASE(test_last_cell_and_position_after_edge),
    TEST_CASE(test_bidi_controls_are_dropped),
    TEST_CASE(test_failed_buffer),
};

int main(int argc, char **argv) {
    return RUN_SUITE("screen", tests, 80, 60);
}
