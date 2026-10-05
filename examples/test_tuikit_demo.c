/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

/*
 * The example's own PTY test: tuikit-demo through a real pseudo-terminal,
 * the whole first screen compared cell by cell, then a quit through the
 * confirmation dialog.  Self-contained (libptytest only), so that it also
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
    PtySession *s = pty_open(60, 12);
    ASSERT(s != NULL, "pty_open");
    const char *argv[] = { g_demo, NULL };
    ASSERT(pty_run(s, argv) == 0, "start tuikit-demo");
    const char *const screen[] = {
        "┌─ Messages ───────┐┌─ Welcome ────────────────────────────┐",
        "│Welcome           ││Welcome to the tuikit demo.           │",
        "│Árvíztűrő tükörfú…││                                      │",
        "│Wide characters   ││Use the arrow keys to move, Tab to    │",
        "│Budget 2027       ││switch panes, / to search, c to write │",
        "│Meeting notes     ││and q to quit.                        │",
        "│Re: Budget 2027   ││                                      │",
        "│                  ││                                      │",
        "│                  ││                                      │",
        "│                  ││                                      │",
        "└──────────────────┘└──────────────────────────────────────┘",
        "Tab pane  / search  q quit                             12x60",
        NULL };
    ASSERT_WAIT_SCREEN(s, screen, 5000);

    static const PtyStyleKey legend[] = {
        { '.', PTY_ATTR_NONE, 0, 0 }, { 'R', PTY_ATTR_REVERSE, 0, 0 },
        { 'B', PTY_ATTR_BOLD, 0, 0 }, { 0, 0, 0, 0 } };
    const char *const selected[] = { "BRRRRRRRRRRRRRRRRRRB", NULL };   /* focused frame: bold */
    ASSERT_REGION_STYLE(s, PTY_REGION(1, 0, 1, 20), selected, legend);
    const char *const frame[] = { "BBBBBBBBBBBBBBBBBBBB....", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 1, 24), frame, legend);

    pty_send_str(s, "q");
    const char *const dialog[] = {
        "┌─ Quit ──────────┐",
        "│ Leave the demo? │",
        "│                 │",
        "│ [ Yes ]  [ No ] │",
        "└─────────────────┘",
        NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(3, 20, 5, 19), dialog, 3000);
    pty_send_str(s, "y");
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
