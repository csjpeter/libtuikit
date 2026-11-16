/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* PTY suite of the setting dialog (TUI-138). */

/*
 * Through a real terminal and a stub program (tests/pty/setting_fixture.c):
 * the dialog's appearance (frame, description, input, hint, buttons, the
 * cursor), OK, Cancel, Esc, and a refused value that keeps the dialog open
 * with the reason in the warning role and the focus on the input.
 */

#include "test_helpers.h"

#include <unistd.h>

int g_tests_run = 0;
int g_tests_failed = 0;

#ifndef SETTING_FIXTURE
#error SETTING_FIXTURE must be defined by the build
#endif

static PtySession *start(void) {
    PtySession *s = pty_open(80, 24);
    if (!s) return NULL;
    const char *argv[] = { SETTING_FIXTURE, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

static int find(PtySession *s, const char *text, int *row, int *col) {
    size_t n = strlen(text);
    for (int r = 1; r < 24; r++)
        for (int c = 0; c + (int)n <= 80; c++) {
            size_t k = 0;
            while (k < n && strcmp(pty_cell_text(s, r, c + (int)k), (char[2]){ text[k], 0 }) == 0) k++;
            if (k == n) {
                *row = r;
                *col = c;
                return 0;
            }
        }
    return -1;
}

static void test_appearance(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    pty_send_str(s, "s");
    ASSERT_WAIT_FOR(s, "Listen port", 3000);
    int tr, tc, dr, dc, ir, ic, hr, hc, br, bc, cr, cc, cur_r, cur_c;
    ASSERT_EQ_INT(find(s, "Listen port", &tr, &tc), 0, "title");
    ASSERT_EQ_INT(find(s, "The TCP port the server", &dr, &dc), 0, "description");
    ASSERT_EQ_INT(find(s, "8080", &ir, &ic), 0, "value");
    ASSERT_EQ_INT(find(s, "Allowed: 1-65535", &hr, &hc), 0, "hint");
    ASSERT_EQ_INT(find(s, "[ OK ]", &br, &bc), 0, "OK");
    ASSERT_EQ_INT(find(s, "[ Cancel ]", &cr, &cc), 0, "Cancel");
    ASSERT_EQ_STR(pty_cell_text(s, tr, tc - 3), "┌", "title in the top border");
    ASSERT(tr < dr && dr < ir && ir < hr && hr < br, "title, description, input, hint, buttons");
    ASSERT_EQ_INT(br, cr, "buttons on one row");
    ASSERT_EQ_INT(pty_cell_fg(s, tr, tc), PTY_FG_CYAN, "the title in the frame colour");
    ASSERT(pty_cell_attr(s, hr, hc) & PTY_ATTR_DIM, "the hint is dimmed");
    ASSERT_EQ_INT(pty_cursor(s, &cur_r, &cur_c), 1, "cursor visible");
    ASSERT_EQ_INT(cur_r, ir, "cursor in the input");
    ASSERT_EQ_INT(cur_c, ic + 4, "after the value");
    pty_send_str(s, "\t");
    pty_settle(s, 150);
    ASSERT_EQ_INT(pty_cursor(s, &cur_r, &cur_c), 0, "no cursor on a button");
    ASSERT(pty_cell_bg(s, br, bc + 2) != pty_cell_bg(s, cr, cc + 2) ||
           (pty_cell_attr(s, br, bc + 2) != pty_cell_attr(s, cr, cc + 2)), "OK is the focused one");
    pty_send_str(s, "\x1b");
    ASSERT_WAIT_FOR(s, "setting: cancelled 8080", 3000);
    pty_send_str(s, "q");
    pty_close(s);
}

static void test_ok(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    pty_send_str(s, "s");
    ASSERT_WAIT_FOR(s, "Listen port", 3000);
    pty_send_str(s, "\x7f" "9");           /* 8080 -> 808 -> 8089 */
    ASSERT_WAIT_FOR(s, "8089", 3000);
    pty_send_str(s, "\r");
    ASSERT_WAIT_FOR(s, "setting: ok 8089", 3000);
    ASSERT_WAIT_GONE(s, "Listen port", 3000);
    /* Tab to OK, Enter. */
    pty_send_str(s, "s\t\r");
    ASSERT_WAIT_FOR(s, "setting: ok 8080", 3000);
    pty_send_str(s, "q");
    pty_close(s);
}

static void test_cancel_button(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    pty_send_str(s, "s");
    ASSERT_WAIT_FOR(s, "Listen port", 3000);
    pty_send_str(s, "77\t\t\r");           /* edit, Tab to OK, Tab to Cancel, Enter */
    ASSERT_WAIT_FOR(s, "setting: cancelled 8080", 3000);
    ASSERT_WAIT_GONE(s, "Listen port", 3000);
    pty_send_str(s, "q");
    pty_close(s);
}

static void test_esc(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    pty_send_str(s, "s");
    ASSERT_WAIT_FOR(s, "Listen port", 3000);
    pty_send_str(s, "5\x1b");
    ASSERT_WAIT_FOR(s, "setting: cancelled 8080", 3000);
    ASSERT_WAIT_GONE(s, "Listen port", 3000);
    pty_send_str(s, "q");
    pty_close(s);
}

static void test_refused_value(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    pty_send_str(s, "s");
    ASSERT_WAIT_FOR(s, "Listen port", 3000);
    pty_send_str(s, "x\r");                /* 8080x: refused */
    ASSERT_WAIT_FOR(s, "Not a port: use 1-65535", 3000);
    ASSERT(pty_screen_contains(s, "Listen port"), "the dialog stays open");
    ASSERT(!pty_screen_contains(s, "setting: ok"), "nothing was accepted");
    int er, ec, dr, dc, ir, ic, cur_r, cur_c;
    ASSERT_EQ_INT(find(s, "Not a port: use 1-65535", &er, &ec), 0, "the reason");
    ASSERT_EQ_INT(find(s, "The TCP port the server", &dr, &dc), 0, "description");
    ASSERT_EQ_INT(find(s, "8080x", &ir, &ic), 0, "the refused value is kept for fixing");
    ASSERT(er > ir, "the reason is under the input");
    ASSERT(pty_cell_fg(s, er, ec) != pty_cell_fg(s, dr, dc), "in the warning colour, not the text's");
    ASSERT_EQ_INT(pty_cell_attr(s, er, ec) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "bold, as the role is");
    /* Refused from the OK button: the focus goes back to the input. */
    pty_send_str(s, "\t\r");
    ASSERT_WAIT_FOR(s, "Not a port: use 1-65535", 3000);
    ASSERT_EQ_INT(pty_cursor(s, &cur_r, &cur_c), 1, "the cursor is back in the input");
    ASSERT_EQ_INT(cur_r, ir, "on the input row");
    pty_send_str(s, "\x7f");               /* typing clears the reason; the value is valid again */
    ASSERT_WAIT_GONE(s, "Not a port", 3000);
    pty_send_str(s, "\r");
    ASSERT_WAIT_FOR(s, "setting: ok 8080", 3000);
    pty_send_str(s, "q");
    pty_close(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_appearance),
    TEST_CASE(test_ok),
    TEST_CASE(test_cancel_button),
    TEST_CASE(test_esc),
    TEST_CASE(test_refused_value),
};

int main(int argc, char **argv) {
    return RUN_SUITE("setting", tests, 20, 120);
}
