/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

/*
 * The example's own PTY test: tuikit-demo through a real pseudo-terminal,
 * the whole first screen compared cell by cell with its colours, the help
 * overlay, then Esc, which quits from the top level.  Self-contained (libptytest only), so that it also
 * serves as the consumer check against the installed packages.
 *
 *   test-tuikit-demo /path/to/tuikit-demo
 */

#include "ptytest.h"
#include "pty_assert.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static const char *g_demo;

static void test_first_screen_and_quit(void) {
    PtySession *s = pty_open(80, 24);
    ASSERT(s != NULL, "pty_open");
    const char *argv[] = { g_demo, NULL };
    ASSERT(pty_run(s, argv) == 0, "start tuikit-demo");
    const char *const screen[] = {
        " Inbox · 6 messages                                                 tuikit-demo",
        "┌─ Messages ─────────────┐┌─ Welcome ──────────────────────────────────────────┐",
        "│Subject                 ││Welcome to the tuikit demo.                         │",
        "│Welcome                 ││                                                    │",
        "│Árvíztűrő tükörfúrógép  ││The arrow keys or j and k move, Enter reads, /      │",
        "│Wide characters         ││filters, n writes, ? lists the keys and Esc quits.  │",
        "│Budget 2027             ││                                                    │",
        "│Meeting notes           ││                                                    │",
        "│Re: Budget 2027         ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "└────────────────────────┘└────────────────────────────────────────────────────┘",
        "↑↓ move  Enter read  / filter  c clear  n new  ? help  Esc quit",
        NULL };
    ASSERT_WAIT_SCREEN(s, screen, 5000);

    /* The palette: header bar, focused frame, selection, hints. */
    static const PtyStyleKey legend[] = {
        { '.', PTY_ATTR_NONE, 0, 0 },
        { 'H', PTY_ATTR_BOLD, PTY_FG_WHITE, PTY_BG_BLUE },
        { 'A', PTY_ATTR_BOLD, PTY_FG_CYAN, PTY_BG_DEFAULT },
        { 'S', PTY_ATTR_BOLD, PTY_FG_BLACK, PTY_BG_CYAN },
        { 'Y', PTY_ATTR_NONE, PTY_FG_YELLOW, PTY_BG_DEFAULT },
        { 0, 0, 0, 0 } };
    const char *const header[] = { "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 1, 80), header, legend);
    const char *const selected[] = { "ASSSSSSSSSSSSSSSSSSSSSSSSA", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(3, 0, 1, 26), selected, legend);
    const char *const hints[] = { "YYYYYYYYYY", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(23, 0, 1, 10), hints, legend);

    pty_send_str(s, "?");
    ASSERT_WAIT_FOR(s, "Message list shortcuts", 3000);
    pty_send_str(s, "x");
    ASSERT_WAIT_SCREEN(s, screen, 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s /path/to/tuikit-demo\n", argv[0]);
        return 2;
    }
    g_demo = argv[1];
    setvbuf(stdout, NULL, _IOLBF, 0);
    alarm(60);
    test_first_screen_and_quit();
    printf("Assertions run:   %d\nAssertions failed:%d\n", g_tests_run, g_tests_failed);
    if (g_tests_run < 6) {
        printf("FAIL: %d assertions, fewer than the floor of 6\n", g_tests_run);
        return 1;
    }
    return g_tests_failed ? 1 : 0;
}
