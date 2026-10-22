/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-20 */
/* PTY suite of the tree: closed on opening, markers, counts, keys, the open state, colours. */

/*
 * policies/tui-interface-model.md, "Trees", through a real terminal and a
 * program (tests/pty/tree_fixture.c): every branch closed on opening; "+ ",
 * "- " and two blanks in the first two cells; a closed row shows its child
 * count; Right/l open (on an open row: first child), Left/h close (on a
 * closed row or a leaf: the parent); Enter does not toggle; j/k move over
 * visible rows; a reload keeps what is open; open all and close all come
 * from the ':' menu.  Text, position and style are compared cell by cell.
 */

#include "test_helpers.h"

int g_tests_run = 0;
int g_tests_failed = 0;

#ifndef TREE_FIXTURE
#error TREE_FIXTURE must be defined by the build
#endif

#define ROWS 14
#define COLS 40

static PtySession *start(void) {
    PtySession *s = pty_open(COLS, ROWS);
    if (!s) return NULL;
    const char *argv[] = { TREE_FIXTURE, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

static const PtyStyleKey g_legend[] = {
    { '.', PTY_ATTR_NONE, PTY_FG_DEFAULT, PTY_BG_DEFAULT },
    { 'H', PTY_ATTR_BOLD, PTY_FG_WHITE, PTY_BG_BLUE },       /* the header bar */
    { 'O', PTY_ATTR_BOLD, PTY_FG_GREEN, PTY_BG_DEFAULT },    /* what was done */
    { 'S', PTY_ATTR_BOLD | PTY_ATTR_REVERSE, 0, 0 },         /* the selected row, marker included */
    { 0, 0, 0, 0 } };

#define SCREEN(s, ...) do { \
    const char *const _w[] = { __VA_ARGS__, NULL }; \
    ASSERT_WAIT_SCREEN(s, _w, 5000); \
} while (0)

#define DONE(what) " Tree", what

/* Closed on opening: the top level only, markers, counts, the selection. */
static void test_closed_on_opening(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    SCREEN(s, " Tree", "done: nothing", "", "+ Alpha (2)", "+ Bravo (1)", "  Charlie");
    const char *const sel[] = { "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(3, 0, 1, COLS), sel, g_legend);
    const char *const rest[] = { "........................................", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(4, 0, 1, COLS), rest, g_legend);
    ASSERT_REGION_STYLE(s, PTY_REGION(5, 0, 1, COLS), rest, g_legend);
    const char *const head[] = { "HHHHH", NULL }, *const done[] = { "OOOOOOOOOOOOO", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 1, 5), head, g_legend);
    ASSERT_REGION_STYLE(s, PTY_REGION(1, 0, 1, 13), done, g_legend);
    pty_close(s);
}

/* Right and Left on closed, open and leaf rows; l and h are the same. */
static void test_right_and_left(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    SCREEN(s, " Tree", "done: nothing", "", "+ Alpha (2)", "+ Bravo (1)", "  Charlie");
    pty_send_key(s, PTY_KEY_RIGHT);                                 /* open Alpha */
    SCREEN(s, " Tree", "done: nothing", "", "- Alpha", "+   Alpha1 (1)", "    Alpha2",
           "+ Bravo (1)", "  Charlie");
    const char *const sel[] = { "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(3, 0, 1, COLS), sel, g_legend);
    pty_send_key(s, PTY_KEY_RIGHT);                                 /* open row: first child */
    pty_send_str(s, "l");                                           /* open Alpha1 */
    SCREEN(s, " Tree", "done: nothing", "", "- Alpha", "-   Alpha1", "      Alpha1a",
           "    Alpha2", "+ Bravo (1)", "  Charlie");
    ASSERT_REGION_STYLE(s, PTY_REGION(4, 0, 1, COLS), sel, g_legend);
    pty_send_str(s, "l");                                           /* first child */
    pty_send_str(s, "l");                                           /* a leaf: nothing */
    pty_send_str(s, "h");                                           /* leaf: the parent */
    pty_send_str(s, "h");                                           /* open: closes */
    SCREEN(s, " Tree", "done: nothing", "", "- Alpha", "+   Alpha1 (1)", "    Alpha2",
           "+ Bravo (1)", "  Charlie");
    ASSERT_REGION_STYLE(s, PTY_REGION(4, 0, 1, COLS), sel, g_legend);
    pty_send_key(s, PTY_KEY_LEFT);                                  /* closed: the parent */
    pty_send_key(s, PTY_KEY_LEFT);                                  /* closes Alpha */
    SCREEN(s, " Tree", "done: nothing", "", "+ Alpha (2)", "+ Bravo (1)", "  Charlie");
    ASSERT_REGION_STYLE(s, PTY_REGION(3, 0, 1, COLS), sel, g_legend);
    pty_send_key(s, PTY_KEY_LEFT);                                  /* top and closed: nothing */
    pty_close(s);
}

/* Enter keeps its meaning (the program's) and does not toggle; j and k skip hidden rows. */
static void test_enter_and_movement(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    SCREEN(s, " Tree", "done: nothing", "", "+ Alpha (2)", "+ Bravo (1)", "  Charlie");
    pty_send_key(s, PTY_KEY_ENTER);
    SCREEN(s, " Tree", "done: open Alpha", "", "+ Alpha (2)", "+ Bravo (1)", "  Charlie");
    pty_send_str(s, "jj");
    pty_send_key(s, PTY_KEY_ENTER);
    SCREEN(s, " Tree", "done: open Charlie", "", "+ Alpha (2)", "+ Bravo (1)", "  Charlie");
    pty_send_str(s, "kl");                                          /* Bravo opens */
    SCREEN(s, " Tree", "done: open Charlie", "", "+ Alpha (2)", "- Bravo", "    Bravo1",
           "  Charlie");
    pty_send_str(s, "jjk");                                         /* Bravo1, Charlie, Bravo1 */
    pty_send_key(s, PTY_KEY_ENTER);
    SCREEN(s, " Tree", "done: open Bravo1", "", "+ Alpha (2)", "- Bravo", "    Bravo1",
           "  Charlie");
    pty_close(s);
}

/* A reload keeps what is open and where the cursor is, by identity. */
static void test_reload_keeps_state(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    SCREEN(s, " Tree", "done: nothing", "", "+ Alpha (2)", "+ Bravo (1)", "  Charlie");
    pty_send_str(s, "ljl");                                         /* Alpha open, Alpha1 open */
    pty_send_str(s, "r");
    SCREEN(s, " Tree", "done: reloaded", "", "- Alpha", "-   Alpha1", "      Alpha1a",
           "    Alpha2", "+ Bravo (1)", "  Charlie");
    const char *const sel[] = { "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(4, 0, 1, COLS), sel, g_legend);
    pty_close(s);
}

/* Open all and close all are entries of the ':' menu; no letter is taken. */
static void test_menu_open_all_close_all(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    SCREEN(s, " Tree", "done: nothing", "", "+ Alpha (2)", "+ Bravo (1)", "  Charlie");
    pty_send_str(s, "O");
    pty_send_str(s, "C");
    SCREEN(s, " Tree", "done: nothing", "", "+ Alpha (2)", "+ Bravo (1)", "  Charlie");
    pty_send_str(s, ":\r");
    SCREEN(s, " Tree", "done: Open all", "", "- Alpha", "-   Alpha1", "      Alpha1a",
           "    Alpha2", "- Bravo", "    Bravo1", "  Charlie");
    pty_send_str(s, "G:j\r");
    SCREEN(s, " Tree", "done: Close all", "", "+ Alpha (2)", "+ Bravo (1)", "  Charlie");
    pty_close(s);
}

/* Esc quits on the top level. */
static void test_esc_quits(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    SCREEN(s, " Tree", "done: nothing", "", "+ Alpha (2)", "+ Bravo (1)", "  Charlie");
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_EQ_INT(pty_wait_exit(s, 3000), 0, "Esc quits");
    pty_close(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_closed_on_opening),
    TEST_CASE(test_right_and_left),
    TEST_CASE(test_enter_and_movement),
    TEST_CASE(test_reload_keeps_state),
    TEST_CASE(test_menu_open_all_close_all),
    TEST_CASE(test_esc_quits),
};

int main(int argc, char **argv) {
    return RUN_SUITE("tree-pty", tests, 30, 120);
}
