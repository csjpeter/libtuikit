/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* Unit tests of tui_yesno: every key, narrow rows, the picture and the bytes. */

#define _GNU_SOURCE
#include "test_helpers.h"
#include "tuikit/yesno.h"

#include <stdlib.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static tui_event key(uint32_t cp, unsigned mods) {
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = TUI_KEY_CHAR;
    ev.cp = cp;
    ev.mods = mods;
    return ev;
}

static tui_event special(tui_key k) {
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = k;
    return ev;
}

static const char *fmt(const tui_yesno *yn, int cols) {
    static char buf[512];
    if (tui_yesno_format(yn, cols, buf, sizeof buf) < 0) { g_tests_failed++; printf("FAIL: format\n"); }
    return buf;
}

static tui_yesno_result answer(tui_event ev) {
    tui_yesno yn;
    tui_yesno_init(&yn, "Delete R01?");
    return tui_yesno_handle(&yn, &ev);
}

static void test_keys(void) {
    ASSERT_EQ_INT(answer(key('y', 0)), TUI_YESNO_YES, "y");
    ASSERT_EQ_INT(answer(key('Y', 0)), TUI_YESNO_YES, "Y");
    ASSERT_EQ_INT(answer(key('Y', TUI_MOD_SHIFT)), TUI_YESNO_YES, "Y with shift");
    ASSERT_EQ_INT(answer(key('n', 0)), TUI_YESNO_NO, "n");
    ASSERT_EQ_INT(answer(key('N', 0)), TUI_YESNO_NO, "N");
    ASSERT_EQ_INT(answer(key('x', 0)), TUI_YESNO_NO, "another letter");
    ASSERT_EQ_INT(answer(key(' ', 0)), TUI_YESNO_NO, "space");
    ASSERT_EQ_INT(answer(key(0x00E9, 0)), TUI_YESNO_NO, "a non-ASCII letter");
    ASSERT_EQ_INT(answer(special(TUI_KEY_ESC)), TUI_YESNO_NO, "Esc");
    ASSERT_EQ_INT(answer(special(TUI_KEY_ENTER)), TUI_YESNO_NO, "Enter is no: the default");
    ASSERT_EQ_INT(answer(special(TUI_KEY_UP)), TUI_YESNO_NO, "an arrow");
    ASSERT_EQ_INT(answer(key('c', TUI_MOD_CTRL)), TUI_YESNO_INTERRUPT, "Ctrl-C");
    ASSERT_EQ_INT(answer(key('y', TUI_MOD_CTRL)), TUI_YESNO_NO, "Ctrl-Y is not yes");
    ASSERT_EQ_INT(answer(key('y', TUI_MOD_ALT)), TUI_YESNO_NO, "Alt-y is not yes");
}

static void test_other_events(void) {
    tui_event ev;
    memset(&ev, 0, sizeof ev);
    tui_yesno yn;
    tui_yesno_init(&yn, "Save?");
    ev.type = TUI_EV_RESIZE; ev.rows = 24; ev.cols = 80;
    ASSERT_EQ_INT(tui_yesno_handle(&yn, &ev), TUI_YESNO_OPEN, "a resize leaves it open");
    ev.type = TUI_EV_PASTE; ev.paste = "y"; ev.paste_len = 1;
    ASSERT_EQ_INT(tui_yesno_handle(&yn, &ev), TUI_YESNO_OPEN, "a paste leaves it open");
    ev.type = TUI_EV_FD; ev.fd_index = 0;
    ASSERT_EQ_INT(tui_yesno_handle(&yn, &ev), TUI_YESNO_OPEN, "an fd event leaves it open");
    ev.type = TUI_EV_NONE;
    ASSERT_EQ_INT(tui_yesno_handle(&yn, &ev), TUI_YESNO_OPEN, "no event");
    ASSERT_EQ_INT(tui_yesno_get(&yn), TUI_YESNO_OPEN, "get: open");
    ev.type = TUI_EV_HANGUP;
    ASSERT_EQ_INT(tui_yesno_handle(&yn, &ev), TUI_YESNO_INTERRUPT, "a hang-up");
    tui_event y = key('y', 0);
    ASSERT_EQ_INT(tui_yesno_handle(&yn, &y), TUI_YESNO_INTERRUPT, "once answered the outcome stays");
    ASSERT_EQ_INT(tui_yesno_get(&yn), TUI_YESNO_INTERRUPT, "get: the outcome");
    tui_yesno_init(&yn, "Save?");
    ASSERT_EQ_INT(tui_yesno_handle(&yn, &y), TUI_YESNO_YES, "reopened by init");
    ASSERT_EQ_INT(tui_yesno_handle(NULL, &y), TUI_YESNO_OPEN, "NULL question");
    ASSERT_EQ_INT(tui_yesno_handle(&yn, NULL), TUI_YESNO_YES, "NULL event keeps the outcome");
    tui_yesno_init(&yn, "Save?");
    ASSERT_EQ_INT(tui_yesno_handle(&yn, NULL), TUI_YESNO_OPEN, "NULL event, open");
    ASSERT_EQ_INT(tui_yesno_get(NULL), TUI_YESNO_OPEN, "NULL get");
    tui_yesno_init(NULL, "x");
}

static void test_format(void) {
    tui_yesno yn;
    tui_yesno_init(&yn, "Delete R01?");
    ASSERT_EQ_STR(fmt(&yn, 80), "Delete R01? (y/N)", "the question and the suffix");
    ASSERT_EQ_STR(fmt(&yn, 17), "Delete R01? (y/N)", "exactly fitting");
    ASSERT_EQ_STR(fmt(&yn, 16), "Delete R0\xE2\x80\xA6 (y/N)", "one short: an ellipsis, the suffix stays");
    ASSERT_EQ_STR(fmt(&yn, 10), "Del\xE2\x80\xA6 (y/N)", "narrow");
    ASSERT_EQ_STR(fmt(&yn, 8), "D\xE2\x80\xA6 (y/N)", "very narrow");
    ASSERT_EQ_STR(fmt(&yn, 7), "\xE2\x80\xA6 (y/N)", "room for the ellipsis alone");
    ASSERT_EQ_STR(fmt(&yn, 6), "(y/N)", "no room for a question");
    ASSERT_EQ_STR(fmt(&yn, 5), "(y/N)", "just the suffix");
    ASSERT_EQ_STR(fmt(&yn, 3), "(y/", "narrower than the suffix: its front");
    ASSERT_EQ_STR(fmt(&yn, 1), "(", "one column");
    ASSERT_EQ_STR(fmt(&yn, 0), "", "no column");
    tui_yesno_init(&yn, "Mentsd a f\xC3\xA1jlt?");
    ASSERT_EQ_STR(fmt(&yn, 80), "Mentsd a f\xC3\xA1jlt? (y/N)", "a wide-char text");
    tui_yesno_init(&yn, "\xE5\x89\x8A\xE9\x99\xA4\xE3\x81\x99\xE3\x82\x8B?");   /* double-width characters */
    ASSERT_EQ_STR(fmt(&yn, 15), "\xE5\x89\x8A\xE9\x99\xA4\xE3\x81\x99\xE3\x82\x8B? (y/N)", "double-width, fitting");
    ASSERT_EQ_STR(fmt(&yn, 14), "\xE5\x89\x8A\xE9\x99\xA4\xE3\x81\x99\xE2\x80\xA6 (y/N)", "double-width, cut at a character");
    tui_yesno_init(&yn, "");
    ASSERT_EQ_STR(fmt(&yn, 40), "(y/N)", "an empty question");
    tui_yesno_init(&yn, NULL);
    ASSERT_EQ_STR(fmt(&yn, 40), "(y/N)", "NULL text");
    tui_yesno_init(&yn, "a\tb\nc\x1b[31m");
    ASSERT_EQ_STR(fmt(&yn, 40), "a b c [31m (y/N)", "controls are blanks");
    char tiny[8];
    tui_yesno_init(&yn, "Delete R01?");
    ASSERT_EQ_INT(tui_yesno_format(&yn, 80, tiny, sizeof tiny), -1, "a small buffer");
    ASSERT_EQ_STR(tiny, "Delete ", "is cut and terminated");
    ASSERT_EQ_INT(tui_yesno_format(NULL, 80, tiny, sizeof tiny), -1, "NULL question");
    ASSERT_EQ_INT(tui_yesno_format(&yn, 80, NULL, 8), -1, "NULL buffer");
    ASSERT_EQ_INT(tui_yesno_format(&yn, 80, tiny, 0), -1, "no room at all");
    char big[300];
    memset(big, 'x', sizeof big - 1);
    big[sizeof big - 1] = '\0';
    tui_yesno_init(&yn, big);
    char out[600];
    ASSERT_EQ_INT(tui_yesno_format(&yn, 300, out, sizeof out), 255 + 6, "a long text is cut at 255 bytes");
    big[252] = '\0';
    strcat(big, "\xC3\xA9\xC3\xA9");
    tui_yesno_init(&yn, big);
    ASSERT_EQ_INT(tui_yesno_format(&yn, 300, out, sizeof out), 254 + 6, "a character is not cut");
}

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

static void fx_show(Fx *f, const tui_yesno *yn, int row) {
    tui_screen_clear(f->scr, TUI_STYLE_DEFAULT);
    tui_yesno_render(yn, tui_canvas_of(f->scr), row);
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    if (f->s) pty_close(f->s);
}

static void test_render_picture(void) {
    Fx f;
    ASSERT_EQ_INT(fx_open(&f, 5, 30), 0, "open");
    tui_yesno yn;
    tui_yesno_init(&yn, "Delete R01?");
    fx_show(&f, &yn, 4);
    char row[128];
    ASSERT_EQ_STR(pty_row_text(f.s, 4, row, sizeof row), "Delete R01? (y/N)", "on the given row");
    ASSERT_EQ_STR(pty_row_text(f.s, 0, row, sizeof row), "", "nothing on the other rows");
    ASSERT_EQ_STR(pty_row_text(f.s, 3, row, sizeof row), "", "nor the one above");
    ASSERT_CELL_TEXT(f.s, 4, 0, "D");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 4, 0) & PTY_ATTR_BOLD, 0, "the question is not bold");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 4, 12) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "the suffix is bold");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 4, 16) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "to its last character");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 4, 12), pty_cell_fg(f.s, 4, 0), "same foreground as the question");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 4, 29), pty_cell_bg(f.s, 4, 0), "the whole row in one background");
    ASSERT_CURSOR(f.s, 4, 18);
    ASSERT_EQ_INT(f.buf.len > 0, 1, "bytes were produced");
    ASSERT_EQ_INT(memchr(f.buf.data, '?', f.buf.len) != NULL, 1, "the bytes carry the question");
    tui_event y = key('y', 0);
    tui_yesno_handle(&yn, &y);
    fx_show(&f, &yn, 4);
    ASSERT_EQ_STR(pty_row_text(f.s, 4, row, sizeof row), "Delete R01? (y/N)", "answered: still drawn");
    fx_close(&f);
}

static void test_render_narrow(void) {
    Fx f;
    ASSERT_EQ_INT(fx_open(&f, 2, 10), 0, "open");
    tui_yesno yn;
    tui_yesno_init(&yn, "Delete R01?");
    fx_show(&f, &yn, 1);
    char row[128];
    ASSERT_EQ_STR(pty_row_text(f.s, 1, row, sizeof row), "Del\xE2\x80\xA6 (y/N)", "cut with an ellipsis, suffix kept");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 1, 5) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "the suffix is bold");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 1, 3) & PTY_ATTR_BOLD, 0, "the cut question is not");
    ASSERT_CURSOR(f.s, 1, 9);
    fx_close(&f);
    ASSERT_EQ_INT(fx_open(&f, 2, 3), 0, "open");
    fx_show(&f, &yn, 0);
    ASSERT_EQ_STR(pty_row_text(f.s, 0, row, sizeof row), "(y/", "narrower than the suffix");
    fx_close(&f);
    ASSERT_EQ_INT(fx_open(&f, 3, 6), 0, "open");
    fx_show(&f, &yn, 2);
    ASSERT_EQ_STR(pty_row_text(f.s, 2, row, sizeof row), "(y/N)", "no room for the question");
    fx_close(&f);
}

static void test_render_limits(void) {
    Fx f;
    ASSERT_EQ_INT(fx_open(&f, 3, 20), 0, "open");
    tui_yesno yn;
    tui_yesno_init(&yn, "Save?");
    char row[128];
    fx_show(&f, &yn, 3);
    ASSERT_EQ_STR(pty_row_text(f.s, 0, row, sizeof row), "", "a row below the canvas draws nothing");
    fx_show(&f, &yn, -1);
    ASSERT_EQ_STR(pty_row_text(f.s, 0, row, sizeof row), "", "a negative row draws nothing");
    tui_yesno_render(NULL, tui_canvas_of(f.scr), 0);
    tui_yesno_render(&yn, tui_canvas_of(NULL), 0);
    fx_show(&f, &yn, 0);
    ASSERT_EQ_STR(pty_row_text(f.s, 0, row, sizeof row), "Save? (y/N)", "row 0");
    fx_close(&f);
}

static const TestCase tests[] = {
    TEST_CASE(test_keys),
    TEST_CASE(test_other_events),
    TEST_CASE(test_format),
    TEST_CASE(test_render_picture),
    TEST_CASE(test_render_narrow),
    TEST_CASE(test_render_limits),
};

int main(int argc, char **argv) {
    return RUN_SUITE("yesno", tests, 60, 60);
}
