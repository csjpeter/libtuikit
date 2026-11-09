/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-09 */
/* Unit tests of tui_header: notice placement and clipping, standing warning, colours. */

/*
 * The bar is rendered, fed to libptytest's emulator and compared cell by
 * cell, next to the exact text of tui_header_format()
 * (policies/tui-interface-model.md, "The header carries no statistics").
 */

#include "test_helpers.h"
#include "tuikit/header.h"
#include "tuikit/statusline.h"
#include "tuikit/text.h"

#include <stdlib.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static const char *fmt(const tui_header *h, int cols) {
    static char buf[512];
    int n = tui_header_format(h, cols, buf, sizeof(buf));
    if (n < 0) { g_tests_failed++; printf("FAIL: format did not fit\n"); }
    return buf;
}

static tui_event key(uint32_t cp) {
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = TUI_KEY_CHAR;
    ev.cp = cp;
    int n = tui_utf8_encode(cp, ev.utf8);
    ev.utf8[n] = '\0';
    return ev;
}

static void setup(tui_header *h) {
    tui_header_init(h);
    tui_header_set_left(h, "Inbox (42)");
    tui_header_set_right(h, "email-tui");
}

/* ── Placement ──────────────────────────────────────────────────────── */

static void test_left_and_name(void) {
    tui_header h;
    setup(&h);
    ASSERT_EQ_STR(fmt(&h, 30), " Inbox (42)         email-tui", "left, name right-aligned, a blank at the edge");
    tui_header_set_left(&h, NULL);
    ASSERT_EQ_STR(fmt(&h, 20), "          email-tui", "no left text");
    tui_header_set_right(&h, "");
    ASSERT_EQ_STR(fmt(&h, 20), "", "empty bar");
    tui_header_set_left(&h, "a\tb\nc");
    ASSERT_EQ_STR(fmt(&h, 20), " a b c", "controls become spaces");
    ASSERT_EQ_STR(fmt(&h, 2), "", "no room");
}

static void test_notice_placement(void) {
    tui_header h;
    setup(&h);
    tui_header_set_notice(&h, "Saved 3 messages", TUI_STYLE_OK);
    ASSERT_EQ_STR(fmt(&h, 60), " Inbox (42)  Saved 3 messages                     email-tui",
                  "the notice follows the view, the name stays right");
    ASSERT_EQ_STR(tui_header_notice(&h), "Saved 3 messages", "readable");
    ASSERT_EQ_INT(tui_header_warning_active(&h), 0, "a plain notice is not a warning");
    tui_header_set_left(&h, "");
    ASSERT_EQ_STR(fmt(&h, 40), " Saved 3 messages"
                  "             email-tui", "no left text: the notice starts the row");
}

static void test_clipping_order(void) {
    tui_header h;
    setup(&h);
    tui_header_set_notice(&h, "Saved 3 messages", TUI_STYLE_OK);
    /* 1+10+2+16 = 29, +1+9+1 = 40: exact fit. */
    ASSERT_EQ_STR(fmt(&h, 40), " Inbox (42)  Saved 3 messages email-tui", "exact fit");
    /* One column less: the left side gives way first. */
    ASSERT_EQ_STR(fmt(&h, 39), " Inbox (4\xE2\x80\xA6  Saved 3 messages email-tui", "the left text is cut first");
    ASSERT_EQ_STR(fmt(&h, 31), " \xE2\x80\xA6  Saved 3 messages email-tui", "down to an ellipsis");
    /* No room for even that: the left text is dropped, then the notice is cut. */
    ASSERT_EQ_STR(fmt(&h, 30), " Saved 3 messages   email-tui", "the left text is dropped");
    ASSERT_EQ_STR(fmt(&h, 24), " Saved 3 mes\xE2\x80\xA6 email-tui", "then the notice");
    ASSERT_EQ_STR(fmt(&h, 13), " \xE2\x80\xA6 email-tui", "to an ellipsis");
    ASSERT_EQ_STR(fmt(&h, 12), "  email-tui", "then nothing");
    ASSERT_EQ_STR(fmt(&h, 11), " email-tui", "never the name");
    ASSERT_EQ_STR(fmt(&h, 8), " email\xE2\x80\xA6", "the name only when alone wider than the row");
    for (int cols = 3; cols < 70; cols++)
        ASSERT(tui_str_width(fmt(&h, cols)) <= cols, "never wider than the row");
}

/* ── Notice life ────────────────────────────────────────────────────── */

static void test_plain_notice_clears_on_key(void) {
    tui_header h;
    setup(&h);
    tui_header_set_notice(&h, "Sent", TUI_STYLE_OK);
    tui_event rz;
    memset(&rz, 0, sizeof(rz));
    rz.type = TUI_EV_RESIZE;
    rz.rows = 24;
    rz.cols = 80;
    ASSERT_EQ_INT(tui_header_handle(&h, &rz), TUI_HEADER_IGNORED, "a resize is not a key");
    ASSERT_EQ_STR(tui_header_notice(&h), "Sent", "the notice stays");
    tui_event k = key('j');
    ASSERT_EQ_INT(tui_header_handle(&h, &k), TUI_HEADER_CHANGED, "a key clears it");
    ASSERT_EQ_STR(tui_header_notice(&h), "", "gone");
    ASSERT_EQ_INT(tui_header_handle(&h, &k), TUI_HEADER_IGNORED, "nothing left: the key is the program's");
    tui_header_set_notice(&h, "x", TUI_STYLE_OK);
    tui_event paste;
    memset(&paste, 0, sizeof(paste));
    paste.type = TUI_EV_PASTE;
    paste.paste = "abc";
    paste.paste_len = 3;
    ASSERT_EQ_INT(tui_header_handle(&h, &paste), TUI_HEADER_CHANGED, "a paste clears it too");
    tui_header_set_notice(&h, "y", TUI_STYLE_OK);
    tui_header_set_notice(&h, NULL, TUI_STYLE_OK);
    ASSERT_EQ_STR(tui_header_notice(&h), "", "NULL clears a plain notice");
}

static void test_warning_stands(void) {
    tui_header h;
    setup(&h);
    tui_header_set_warning(&h, "Sync failed: no route");
    ASSERT_EQ_INT(tui_header_warning_active(&h), 1, "active");
    ASSERT_EQ_STR(fmt(&h, 60), " Inbox (42)  Sync failed: no route                email-tui", "placed like a notice");
    tui_event k = key('j');
    for (int i = 0; i < 3; i++)
        ASSERT_EQ_INT(tui_header_handle(&h, &k), TUI_HEADER_IGNORED, "a key does not clear a warning");
    ASSERT_EQ_INT(tui_header_warning_active(&h), 1, "still there after the keys");
    tui_header_set_notice(&h, "Saved", TUI_STYLE_OK);
    ASSERT_EQ_STR(tui_header_notice(&h), "Sync failed: no route", "the warning wins over a plain notice");
    tui_header_set_warning(&h, "Disk full");
    ASSERT_EQ_STR(tui_header_notice(&h), "Disk full", "another warning replaces it");
    tui_header_clear_warning(&h);
    ASSERT_EQ_INT(tui_header_warning_active(&h), 0, "cleared on request");
    ASSERT_EQ_STR(tui_header_notice(&h), "", "nothing shows");
    tui_header_set_notice(&h, "Saved", TUI_STYLE_OK);
    ASSERT_EQ_STR(tui_header_notice(&h), "Saved", "a notice works again");
    tui_header_set_warning(&h, "bad");
    tui_header_set_warning(&h, "");
    ASSERT_EQ_INT(tui_header_warning_active(&h), 0, "an empty warning clears it");
    tui_header_set_warning(&h, "bad");
    tui_header_clear_warning(&h);
    tui_header_clear_warning(&h);
    ASSERT_EQ_STR(tui_header_notice(&h), "", "clearing twice is harmless");
}

static void test_warning_replaces_plain_notice(void) {
    tui_header h;
    setup(&h);
    tui_header_set_notice(&h, "Saved", TUI_STYLE_OK);
    tui_header_set_warning(&h, "Sync failed");
    ASSERT_EQ_STR(tui_header_notice(&h), "Sync failed", "the warning replaces a plain notice");
    tui_event k = key('x');
    tui_header_handle(&h, &k);
    ASSERT_EQ_STR(tui_header_notice(&h), "Sync failed", "and stays");
}

static void test_statusline_unchanged_by_messages(void) {
    tui_header h;
    setup(&h);
    tui_statusline sl;
    tui_statusline_init(&sl);
    static const tui_statusline_hint hints[] = { { "Enter", "open", 5 }, { "q", "quit", 9 } };
    tui_statusline_set_hints(&sl, hints, 2);
    char before[256], after[256];
    tui_statusline_format(&sl, 60, before, sizeof(before));
    tui_header_set_warning(&h, "Sync failed");
    tui_header_set_notice(&h, "Saved", TUI_STYLE_OK);
    tui_statusline_format(&sl, 60, after, sizeof(after));
    ASSERT_EQ_STR(after, before, "the bottom line keeps the keys");
    ASSERT_EQ_STR(before, " Enter open  q quit  ? help", "hints and help");
    tui_statusline_free(&sl);
}

/* ── Drawing: the emulated screen ───────────────────────────────────── */

static void show(tui_screen *scr, PtySession *s, tui_header *h) {
    tui_buf buf;
    tui_buf_init(&buf);
    tui_screen_clear(scr, TUI_STYLE_DEFAULT);
    tui_header_render(h, tui_canvas_of(scr));
    tui_screen_render(scr, &buf);
    pty_feed(s, buf.data, buf.len);
    tui_buf_free(&buf);
}

static void test_render_bar_and_colours(void) {
    tui_screen *scr = tui_screen_new(4, 40);
    PtySession *s = pty_open(40, 4);
    ASSERT(scr && s, "fixtures");
    tui_header h;
    setup(&h);
    show(scr, s, &h);
    char b[128];
    ASSERT_EQ_STR(pty_row_text(s, 0, b, sizeof(b)), " Inbox (42)                   email-tui", "text");
    int bar_fg = pty_cell_fg(s, 0, 1), bar_bg = pty_cell_bg(s, 0, 1);
    ASSERT_EQ_INT(bar_fg, 37, "white on");
    ASSERT_EQ_INT(bar_bg, 44, "blue: the header role");
    ASSERT_EQ_INT(pty_cell_bg(s, 0, 39), 44, "the bar runs to the edge");
    ASSERT_EQ_INT(pty_cell_bg(s, 1, 1), PTY_COLOR_DEFAULT, "the row below is the terminal's own");

    tui_header_set_notice(&h, "Saved", TUI_STYLE_OK);
    show(scr, s, &h);
    ASSERT_EQ_STR(pty_row_text(s, 0, b, sizeof(b)), " Inbox (42)  Saved"
                  "            email-tui", "ok notice");
    ASSERT_EQ_INT(pty_cell_bg(s, 0, 14), 42, "ok: green");
    ASSERT_EQ_INT(pty_cell_bg(s, 0, 1), bar_bg, "the view keeps the bar");
    ASSERT_EQ_INT(pty_cell_bg(s, 0, 31), bar_bg, "and so does the name");

    tui_header_set_notice(&h, "Fetching", TUI_STYLE_PROGRESS);
    show(scr, s, &h);
    ASSERT_EQ_INT(pty_cell_bg(s, 0, 14), 43, "progress: yellow");
    ASSERT_EQ_INT(pty_cell_attr(s, 0, 14) & PTY_ATTR_ITALIC, PTY_ATTR_ITALIC, "and italic");

    tui_header_set_warning(&h, "Sync failed");
    show(scr, s, &h);
    ASSERT_EQ_INT(pty_cell_bg(s, 0, 14), 41, "warning: red");
    ASSERT_EQ_INT(pty_cell_attr(s, 0, 14) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "and bold");
    tui_event k = key('j');
    tui_header_handle(&h, &k);
    show(scr, s, &h);
    ASSERT_EQ_INT(pty_cell_bg(s, 0, 14), 41, "still red after a key");
    ASSERT_EQ_INT(pty_cell_bg(s, 0, 12), bar_bg, "the gap before it is the bar");
    tui_header_clear_warning(&h);
    show(scr, s, &h);
    ASSERT_EQ_STR(pty_row_text(s, 0, b, sizeof(b)), " Inbox (42)                   email-tui", "the notice is gone");
    ASSERT_EQ_INT(pty_cell_bg(s, 0, 14), bar_bg, "and its colour");

    pty_close(s);
    tui_screen_free(scr);
}

static void test_render_narrow_and_edges(void) {
    tui_screen *scr = tui_screen_new(2, 24);
    PtySession *s = pty_open(24, 2);
    ASSERT(scr && s, "fixtures");
    tui_header h;
    setup(&h);
    tui_header_set_warning(&h, "Sync failed: no route");
    show(scr, s, &h);
    char b[128];
    ASSERT_EQ_STR(pty_row_text(s, 0, b, sizeof(b)), " Sync failed\xE2\x80\xA6 email-tui", "the left text gave way");
    ASSERT_EQ_INT(pty_cell_bg(s, 0, 1), 41, "the warning is red at 24 columns");
    tui_header_render(NULL, tui_canvas_of(scr));
    tui_canvas none = tui_canvas_sub(tui_canvas_of(scr), tui_rect_make(0, 0, 0, 0));
    tui_header_render(&h, none);
    pty_close(s);
    tui_screen_free(scr);
}

static void test_format_limits_and_null(void) {
    tui_header h;
    setup(&h);
    char small[8];
    ASSERT_EQ_INT(tui_header_format(&h, 40, small, sizeof(small)), -1, "too small");
    ASSERT_EQ_STR(small, " Inbox ", "cut and terminated");
    char one[1];
    ASSERT_EQ_INT(tui_header_format(&h, 40, one, sizeof(one)), -1, "room for the NUL only");
    ASSERT_EQ_INT(tui_header_format(&h, 40, NULL, 10), -1, "NULL out");
    ASSERT_EQ_INT(tui_header_format(NULL, 40, small, sizeof(small)), -1, "NULL bar");
    tui_header_set_left(&h, "\xC3\xA9\xC3\xA9\xC3\xA9");
    char t[5];
    ASSERT_EQ_INT(tui_header_format(&h, 40, t, sizeof(t)), -1, "cut");
    ASSERT_EQ_STR(t, " \xC3\xA9", "whole characters only");
    char big[400];
    memset(big, 'x', sizeof(big));
    big[300] = '\0';
    tui_header_set_left(&h, big);
    tui_header_set_notice(&h, big, TUI_STYLE_OK);
    ASSERT_EQ_INT((int)strlen(tui_header_notice(&h)), 255, "a long notice is cut");
    tui_header_init(NULL);
    tui_header_set_left(NULL, "x");
    tui_header_set_right(NULL, "x");
    tui_header_set_notice(NULL, "x", TUI_STYLE_OK);
    tui_header_set_warning(NULL, "x");
    tui_header_clear_warning(NULL);
    ASSERT_EQ_STR(tui_header_notice(NULL), "", "NULL notice");
    ASSERT_EQ_INT(tui_header_warning_active(NULL), 0, "NULL warning");
    tui_event k = key('x');
    ASSERT_EQ_INT(tui_header_handle(NULL, &k), TUI_HEADER_IGNORED, "NULL bar");
    ASSERT_EQ_INT(tui_header_handle(&h, NULL), TUI_HEADER_IGNORED, "NULL event");
}

static const TestCase tests[] = {
    TEST_CASE(test_left_and_name),
    TEST_CASE(test_notice_placement),
    TEST_CASE(test_clipping_order),
    TEST_CASE(test_plain_notice_clears_on_key),
    TEST_CASE(test_warning_stands),
    TEST_CASE(test_warning_replaces_plain_notice),
    TEST_CASE(test_statusline_unchanged_by_messages),
    TEST_CASE(test_render_bar_and_colours),
    TEST_CASE(test_render_narrow_and_edges),
    TEST_CASE(test_format_limits_and_null),
};

int main(int argc, char **argv) {
    return RUN_SUITE("header", tests, 140, 60);
}
