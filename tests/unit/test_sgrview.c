/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* Unit tests of tui_sgrview (TUI-122): SGR styles kept across wrap and scroll. */

/*
 * The view is drawn into a screen whose bytes go to libptytest's emulator;
 * the cells (text, colour, attributes) are compared there.
 */

#include "test_helpers.h"
#include "tuikit/tuikit.h"

#include <errno.h>

int g_tests_run = 0;
int g_tests_failed = 0;

typedef struct fx {
    tui_screen *scr;
    tui_buf     buf;
    PtySession *s;
} fx;

static int fx_open(fx *f, int rows, int cols) {
    f->scr = tui_screen_new(rows, cols);
    tui_buf_init(&f->buf);
    f->s = pty_open(cols, rows);
    return (f->scr && f->s) ? 0 : -1;
}

static void fx_show(fx *f) {
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    pty_close(f->s);
}

static tui_event key(const char *bytes) {
    tui_decoder d;
    tui_decoder_init(&d);
    tui_event ev;
    memset(&ev, 0, sizeof ev);
    tui_decoder_feed(&d, bytes, strlen(bytes));
    if (!tui_decoder_next(&d, &ev)) tui_decoder_timeout(&d, &ev);
    tui_decoder_free(&d);
    return ev;
}

/* Draws `text` at rows x cols with the base style and shows it in the emulator. */
static void show(fx *f, tui_sgrview *sv, const char *text, tui_style base) {
    tui_sgrview_set_text(sv, text);
    tui_sgrview_render(sv, tui_canvas_of(f->scr), base);
    fx_show(f);
}

static void test_attributes(void) {
    fx f;
    ASSERT(fx_open(&f, 3, 40) == 0, "fixtures");
    tui_sgrview sv;
    tui_sgrview_init(&sv);
    show(&f, &sv, "a\x1b[1mB\x1b[22m \x1b[3mI\x1b[23m \x1b[4mU\x1b[24m \x1b[9mS\x1b[29m \x1b[2mD\x1b[22m z", TUI_STYLE_DEFAULT);
    const char *const expect[] = { "aB I U S D z", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 0), 0, "plain a");
    ASSERT_CELL_ATTR_EQ(f.s, 0, 1, PTY_ATTR_BOLD);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 2), 0, "bold ends (22)");
    ASSERT_CELL_ATTR_EQ(f.s, 0, 3, PTY_ATTR_ITALIC);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 4), 0, "italic ends (23)");
    ASSERT_CELL_ATTR_EQ(f.s, 0, 5, PTY_ATTR_UNDERLINE);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 6), 0, "underline ends (24)");
    ASSERT_CELL_ATTR_EQ(f.s, 0, 7, PTY_ATTR_STRIKE);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 8), 0, "strike ends (29)");
    ASSERT_CELL_ATTR_EQ(f.s, 0, 9, PTY_ATTR_DIM);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 11), 0, "dim ends (22)");
    /* Combined, then reset by 0 and by a bare ESC[m. */
    show(&f, &sv, "\x1b[1;3;4;9mall\x1b[0m none \x1b[1mb\x1b[mn", TUI_STYLE_DEFAULT);
    ASSERT_CELL_ATTR_EQ(f.s, 0, 0, PTY_ATTR_BOLD | PTY_ATTR_ITALIC | PTY_ATTR_UNDERLINE | PTY_ATTR_STRIKE);
    ASSERT_CELL_ATTR_EQ(f.s, 0, 2, PTY_ATTR_BOLD | PTY_ATTR_ITALIC | PTY_ATTR_UNDERLINE | PTY_ATTR_STRIKE);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 4), 0, "reset by 0");
    ASSERT_CELL_ATTR_EQ(f.s, 0, 9, PTY_ATTR_BOLD);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 10), 0, "reset by ESC[m");
    /* Reverse and blink, and their offs. */
    show(&f, &sv, "\x1b[7mr\x1b[27m\x1b[5mk\x1b[25mn", TUI_STYLE_DEFAULT);
    ASSERT_CELL_ATTR_EQ(f.s, 0, 0, PTY_ATTR_REVERSE);
    ASSERT_CELL_ATTR_EQ(f.s, 0, 1, PTY_ATTR_BLINK);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 2), 0, "both off");
    tui_sgrview_free(&sv);
    fx_close(&f);
}

static void test_colours(void) {
    fx f;
    ASSERT(fx_open(&f, 3, 50) == 0, "fixtures");
    tui_sgrview sv;
    tui_sgrview_init(&sv);
    show(&f, &sv,
         "\x1b[31ma\x1b[92mb\x1b[39mc \x1b[44md\x1b[103me\x1b[49mf "
         "\x1b[38;5;200mg\x1b[0m \x1b[48;5;21mh\x1b[0m "
         "\x1b[38;2;10;20;30mi\x1b[48;2;1;2;3mj\x1b[0m "
         "\x1b[38:2:40:50:60mk\x1b[0m",
         TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 0), 31, "SGR 31");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 1), 92, "SGR 92");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 2), PTY_FG_DEFAULT, "39 restores the default");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 0, 4), 44, "SGR 44");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 0, 5), 103, "SGR 103");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 0, 6), 0, "49 restores the default");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 8), PTY_COLOR_IDX(200), "38;5;200");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 0, 10), PTY_COLOR_IDX(21), "48;5;21");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 12), PTY_COLOR_RGB(10, 20, 30), "24-bit foreground");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 13), PTY_COLOR_RGB(10, 20, 30), "the foreground stays");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 0, 13), PTY_COLOR_RGB(1, 2, 3), "24-bit background");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 15), PTY_COLOR_RGB(40, 50, 60), "24-bit with colons");
    ASSERT_CELL_TEXT(f.s, 0, 15, "k");
    /* The base style: a colour the text leaves is the base's, one it sets wins. */
    tui_style base = tui_style_make(TUI_GREEN, TUI_BLUE, TUI_ATTR_BOLD);
    show(&f, &sv, "a\x1b[31mb\x1b[39mc\x1b[0md", base);
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 0), PTY_FG_GREEN, "base colour");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 1), 31, "the text's colour wins");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 2), PTY_FG_GREEN, "default falls back to the base");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 0, 1), pty_cell_bg(f.s, 2, 49), "the background is the base's");
    ASSERT_CELL_ATTR(f.s, 0, 3, PTY_ATTR_BOLD);
    /* Malformed and unknown sequences are harmless. */
    show(&f, &sv, "x\x1b[38;2;1my\x1b[38;5mz\x1b[99m\x1b[2Jw\x1b[1", TUI_STYLE_DEFAULT);
    const char *const expect[] = { "xyzw", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    tui_sgrview_free(&sv);
    fx_close(&f);
}

static void test_wrap_inside_run(void) {
    fx f;
    ASSERT(fx_open(&f, 5, 12) == 0, "fixtures");
    tui_sgrview sv;
    tui_sgrview_init(&sv);
    /* A bold red run that wraps over three lines, then plain text. */
    show(&f, &sv, "\x1b[1;31mone two three four five\x1b[0m six", TUI_STYLE_DEFAULT);
    const char *const expect[] = { "one two", "three four", "five six", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    ASSERT_EQ_INT(tui_sgrview_lines(&sv), 3, "three lines");
    for (int r = 0; r < 3; r++) {
        ASSERT_CELL_ATTR(f.s, r, 0, PTY_ATTR_BOLD);
        ASSERT_EQ_INT(pty_cell_fg(f.s, r, 0), 31, "each line starts in the run's colour");
    }
    ASSERT_CELL_ATTR(f.s, 2, 1, PTY_ATTR_BOLD);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 2, 5), 0, "the run ends where it ends");
    ASSERT_CELL_TEXT(f.s, 2, 5, "s");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 2, 5), PTY_FG_DEFAULT, "plain after the run");
    /* A word longer than the line breaks inside the styled run. */
    show(&f, &sv, "\x1b[4mabcdefghijklmnopqrstuvwxyz\x1b[0m", TUI_STYLE_DEFAULT);
    const char *const expect2[] = { "abcdefghijkl", "mnopqrstuvwx", "yz", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect2);
    ASSERT_CELL_ATTR(f.s, 1, 0, PTY_ATTR_UNDERLINE);
    ASSERT_CELL_ATTR(f.s, 2, 1, PTY_ATTR_UNDERLINE);
    /* Escape sequences take no column: exactly twelve letters fit one line. */
    show(&f, &sv, "\x1b[1mabcdef\x1b[0m\x1b[31mghijkl\x1b[0m", TUI_STYLE_DEFAULT);
    const char *const expect3[] = { "abcdefghijkl", "", "", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect3);
    ASSERT_EQ_INT(tui_sgrview_lines(&sv), 1, "one line");
    /* A style set in the break whitespace still reaches the next line. */
    show(&f, &sv, "aaaaaaaaaa \x1b[3mbbb ccc\x1b[0m", TUI_STYLE_DEFAULT);
    ASSERT_CELL_TEXT(f.s, 1, 0, "b");
    ASSERT_CELL_ATTR(f.s, 1, 0, PTY_ATTR_ITALIC);
    /* A style carries over a newline; CRLF is one break. */
    show(&f, &sv, "\x1b[1mfirst\r\nsecond\x1b[0m\nthird", TUI_STYLE_DEFAULT);
    const char *const expect4[] = { "first", "second", "third", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect4);
    ASSERT_CELL_ATTR(f.s, 1, 0, PTY_ATTR_BOLD);
    ASSERT_EQ_INT(pty_cell_attr(f.s, 2, 0), 0, "ended before the third line");
    tui_sgrview_free(&sv);
    fx_close(&f);
}

static void test_wide_tab_control(void) {
    fx f;
    ASSERT(fx_open(&f, 4, 10) == 0, "fixtures");
    tui_sgrview sv;
    tui_sgrview_init(&sv);
    /* A wide character is not split; a tab goes to the next multiple of 8. */
    show(&f, &sv, "\x1b[1m\xE5\xAD\x97\xE5\xAD\x97\xE5\xAD\x97\xE5\xAD\x97\xE5\xAD\x97\xE5\xAD\x97", TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_sgrview_lines(&sv), 2, "six wide characters over ten columns: two lines");
    ASSERT_CELL_TEXT(f.s, 1, 0, "\xE5\xAD\x97");
    ASSERT_CELL_ATTR(f.s, 1, 0, PTY_ATTR_BOLD);
    show(&f, &sv, "a\tb", TUI_STYLE_DEFAULT);
    ASSERT_CELL_TEXT(f.s, 0, 8, "b");
    show(&f, &sv, "x\x01y\x1b" "z", TUI_STYLE_DEFAULT);
    ASSERT_CELL_TEXT(f.s, 0, 1, "\xC2\xB7");
    ASSERT_CELL_TEXT(f.s, 0, 2, "y");
    ASSERT_CELL_TEXT(f.s, 0, 3, "\xC2\xB7");
    tui_sgrview_free(&sv);
    fx_close(&f);
}

static void test_scroll_keeps_style(void) {
    fx f;
    ASSERT(fx_open(&f, 3, 20) == 0, "fixtures");
    tui_sgrview sv;
    tui_sgrview_init(&sv);
    /* Ten lines, a red italic run opened on line 2 and closed on line 8. */
    tui_sgrview_set_text(&sv, "l1\nl2 \x1b[3;31mopen\nl3\nl4\nl5\nl6\nl7\nl8\x1b[0m\nl9\nl10");
    tui_canvas all = tui_canvas_of(f.scr);
    tui_sgrview_render(&sv, all, TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_sgrview_lines(&sv), 10, "ten lines");
    fx_show(&f);
    ASSERT_EQ_STR(pty_cell_text(f.s, 0, 19), "\xE2\x96\x88", "a scroll bar at the right edge");
    tui_sgrview_scroll_to(&sv, 4);
    tui_sgrview_render(&sv, all, TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_CELL_TEXT(f.s, 0, 0, "l");
    ASSERT_CELL_TEXT(f.s, 0, 1, "5");
    ASSERT_CELL_ATTR(f.s, 0, 0, PTY_ATTR_ITALIC);
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 0), 31, "the run, opened above the top line, is still drawn");
    tui_sgrview_scroll_to(&sv, 7);
    tui_sgrview_render(&sv, all, TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_CELL_TEXT(f.s, 0, 1, "8");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 0), 31, "line 8 is in the run");
    ASSERT_CELL_TEXT(f.s, 1, 1, "9");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 1, 0), PTY_FG_DEFAULT, "line 9 is after it");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 1, 0), 0, "and plain");

    /* The keys. */
    tui_sgrview_scroll_to(&sv, 0);
    tui_event ev = key("j");
    ASSERT_EQ_INT(tui_sgrview_handle(&sv, &ev), 1, "j is a scroll key");
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 1, "j moves down");
    ev = key("\x1b[B");
    tui_sgrview_handle(&sv, &ev);
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 2, "Down");
    ev = key("\x1b[A");
    tui_sgrview_handle(&sv, &ev);
    ev = key("k");
    tui_sgrview_handle(&sv, &ev);
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 0, "Up and k");
    ev = key("\x1b[6~");
    ASSERT_EQ_INT(tui_sgrview_handle(&sv, &ev), 1, "PgDn");
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 1, "a page is the rows less two, at least one");
    ev = key("\x1b[5~");
    tui_sgrview_handle(&sv, &ev);
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 0, "PgUp");
    ev = key("\x1b[F");
    ASSERT_EQ_INT(tui_sgrview_handle(&sv, &ev), 1, "End");
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 7, "End shows the last line");
    ev = key("\x1b[H");
    ASSERT_EQ_INT(tui_sgrview_handle(&sv, &ev), 1, "Home");
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 0, "Home");
    tui_sgrview_scroll(&sv, 3);
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 3, "scroll");
    tui_sgrview_scroll(&sv, 100);
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 7, "clamped at the end");
    tui_sgrview_scroll_end(&sv);
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 7, "scroll_end");
    ev = key("q");
    ASSERT_EQ_INT(tui_sgrview_handle(&sv, &ev), 0, "q is the program's");
    ASSERT_EQ_INT(tui_sgrview_handle(&sv, NULL), 0, "no event");
    ASSERT_EQ_INT(tui_sgrview_handle(NULL, &ev), 0, "no view");
    tui_sgrview_set_text(&sv, "new");
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 0, "set_text scrolls to the top");
    tui_sgrview_free(&sv);
    fx_close(&f);
}

static void test_resize_and_misc(void) {
    fx f;
    ASSERT(fx_open(&f, 6, 40) == 0, "fixtures");
    tui_sgrview sv;
    tui_sgrview_init(&sv);
    tui_canvas all = tui_canvas_of(f.scr);
    tui_sgrview_render(&sv, all, TUI_STYLE_DEFAULT);        /* never given a text */
    ASSERT_EQ_INT(tui_sgrview_lines(&sv), 0, "no text, no lines");
    tui_sgrview_set_text(&sv, NULL);
    tui_sgrview_render(&sv, all, TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_sgrview_lines(&sv), 0, "the empty text");
    tui_sgrview_scroll(&sv, 5);
    ASSERT_EQ_INT(tui_sgrview_top(&sv), 0, "nothing to scroll");
    errno = 0;
    ASSERT_EQ_INT(tui_sgrview_set_text(NULL, "x"), -1, "set_text: no view");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_sgrview_lines(NULL), 0, "lines: no view");
    ASSERT_EQ_INT(tui_sgrview_top(NULL), 0, "top: no view");
    tui_sgrview_scroll(NULL, 1);
    tui_sgrview_scroll_to(NULL, 1);
    tui_sgrview_scroll_end(NULL);
    tui_sgrview_init(NULL);
    tui_sgrview_free(NULL);
    tui_sgrview_render(NULL, all, TUI_STYLE_DEFAULT);

    tui_sgrview_set_text(&sv, "\x1b[1malpha beta gamma\x1b[0m delta epsilon zeta eta theta iota");
    tui_sgrview_render(&sv, all, TUI_STYLE_DEFAULT);
    int wide = tui_sgrview_lines(&sv);
    tui_canvas narrow = tui_canvas_sub(all, tui_rect_make(0, 0, 6, 16));
    tui_sgrview_render(&sv, narrow, TUI_STYLE_DEFAULT);
    ASSERT(tui_sgrview_lines(&sv) > wide, "narrower: more lines");
    tui_sgrview_render(&sv, all, TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_sgrview_lines(&sv), wide, "wide again");
    tui_sgrview_render(&sv, tui_canvas_sub(all, tui_rect_make(0, 0, 6, 3)), TUI_STYLE_DEFAULT);
    ASSERT(tui_sgrview_lines(&sv) > 6, "three columns: many lines, no crash");
    tui_sgrview_render(&sv, tui_canvas_sub(all, tui_rect_make(0, 0, 6, 1)), TUI_STYLE_DEFAULT);
    tui_sgrview_render(&sv, tui_canvas_sub(all, tui_rect_make(0, 0, 0, 10)), TUI_STYLE_DEFAULT);
    tui_sgrview_free(&sv);
    fx_close(&f);
}

static const TestCase tests[] = {
    TEST_CASE(test_attributes),
    TEST_CASE(test_colours),
    TEST_CASE(test_wrap_inside_run),
    TEST_CASE(test_wide_tab_control),
    TEST_CASE(test_scroll_keeps_style),
    TEST_CASE(test_resize_and_misc),
};

int main(int argc, char **argv) {
    return RUN_SUITE("sgrview", tests, 80, 60);
}
