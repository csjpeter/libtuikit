/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* PTY suite of the framed form dialog and the masked prompt (TUI-131). */

/*
 * Through a real terminal and a stub program (tests/pty/form_fixture.c):
 * the form in its frame (title in the top border, key hint in the bottom
 * one, colours), Tab, the choice, Enter submitting and Esc cancelling; the
 * masked prompt showing one star per character and never the text.
 */

#include "test_helpers.h"

int g_tests_run = 0;
int g_tests_failed = 0;

#ifndef FORM_FIXTURE
#error FORM_FIXTURE must be defined by the build
#endif

static PtySession *start(int cols) {
    PtySession *s = pty_open(cols, 24);
    if (!s) return NULL;
    const char *argv[] = { FORM_FIXTURE, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

/* The first cell of @p text on the screen; -1 when it is not there. */
static int find(PtySession *s, const char *text, int *row, int *col) {
    size_t n = strlen(text);
    for (int r = 0; r < 24; r++)
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

static void test_form_dialog_frame_and_keys(void) {
    PtySession *s = start(80);
    ASSERT(s != NULL, "start");
    pty_send_str(s, "f");
    ASSERT_WAIT_FOR(s, "New client", 3000);
    ASSERT(pty_screen_contains(s, "Name:"), "the first label");
    ASSERT(pty_screen_contains(s, "Redirect URL:"), "the second label");
    ASSERT(pty_screen_contains(s, "web"), "the choice");
    int tr, tc, br, bc, nr, nc;
    ASSERT_EQ_INT(find(s, "New client", &tr, &tc), 0, "the title");
    ASSERT_EQ_INT(find(s, " Tab next  Enter OK  Esc cancel ", &br, &bc), 0, "the key hint");
    ASSERT_EQ_INT(find(s, "Name:", &nr, &nc), 0, "the label position");
    ASSERT_EQ_STR(pty_cell_text(s, tr, tc - 3), "┌", "the title is in the top border");
    ASSERT_EQ_STR(pty_cell_text(s, br, bc - 2), "└", "the hint is in the bottom border");
    ASSERT(br > nr + 2, "the hint is below the fields");
    ASSERT_EQ_STR(pty_cell_text(s, nr, nc - 2), "│", "the fields are inside the frame");
    ASSERT_EQ_INT(pty_cell_fg(s, tr, tc), PTY_FG_CYAN, "the title in the frame colour");
    ASSERT_EQ_INT(pty_cell_fg(s, br, bc + 1), PTY_FG_CYAN, "the hint in the frame colour");
    ASSERT_EQ_INT(pty_cell_bg(s, tr, tc), PTY_BG_DEFAULT, "no background colour");
    int cr, cc;
    pty_cursor(s, &cr, &cc);
    ASSERT_EQ_INT(cr, nr, "the cursor is in the first field");
    pty_send_str(s, "acme\thttps://x.example/cb\t\x1b[C");
    ASSERT_WAIT_FOR(s, "< native >", 3000);
    ASSERT(pty_screen_contains(s, "acme"), "the first value");
    ASSERT(pty_screen_contains(s, "https://x.example/cb"), "the second value");
    pty_send_str(s, "\r");
    ASSERT_WAIT_FOR(s, "form: acme|https://x.example/cb|native", 3000);
    ASSERT_WAIT_GONE(s, "New client", 3000);
    pty_send_str(s, "fxy\x1b");
    ASSERT_WAIT_FOR(s, "form: cancelled", 3000);
    ASSERT_WAIT_GONE(s, "New client", 3000);
    pty_send_str(s, "q");
    pty_close(s);
}

static void test_password_prompt_is_masked(void) {
    PtySession *s = start(80);
    ASSERT(s != NULL, "start");
    pty_send_str(s, "p");
    ASSERT_WAIT_FOR(s, "Change password", 3000);
    ASSERT(pty_screen_contains(s, "Current password:"), "the explaining line");
    pty_send_str(s, "sec");
    ASSERT_WAIT_FOR(s, "***", 3000);
    ASSERT(!pty_screen_contains(s, "sec"), "the typed letters are not shown");
    ASSERT(!pty_screen_contains(s, "****"), "one star per character");
    int sr, sc, cr, cc;
    ASSERT_EQ_INT(find(s, "***", &sr, &sc), 0, "the stars");
    pty_cursor(s, &cr, &cc);
    ASSERT_EQ_INT(cr, sr, "the cursor is on the input line");
    ASSERT_EQ_INT(cc, sc + 3, "after the stars");
    pty_send_str(s, "ret");
    ASSERT_WAIT_FOR(s, "******", 3000);
    ASSERT(!pty_screen_contains(s, "secret"), "the whole text is not shown");
    pty_send_str(s, "\r");
    ASSERT_WAIT_FOR(s, "password: len=6 ok=1", 3000);
    ASSERT_WAIT_GONE(s, "Change password", 3000);
    pty_send_str(s, "pabc\x1b");
    ASSERT_WAIT_FOR(s, "password: cancelled", 3000);
    pty_send_str(s, "q");
    pty_close(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_form_dialog_frame_and_keys),
    TEST_CASE(test_password_prompt_is_masked),
};

int main(int argc, char **argv) {
    return RUN_SUITE("form", tests, 20, 120);
}
