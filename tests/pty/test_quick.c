/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-21 */
/* PTY suite of the quick keys of the action menu (TUI-62): letters, colours, running, confirmation, narrow width. */

/*
 * policies/tui-interface-model.md, "Every action in the menu has a quick
 * key", through a real terminal and a stub program
 * (tests/pty/quick_fixture.c) whose ':' menu has ten entries: the library's
 * letters are in a column of their own in the accent colour, a quick key
 * runs its entry, a serious entry opens its confirmation instead of
 * performing, '?' lists the keys; at 80x24 and at 40 columns, where a long
 * entry is cut with an ellipsis and its letter stays.
 */

#include "test_helpers.h"

int g_tests_run = 0;
int g_tests_failed = 0;

#ifndef QUICK_FIXTURE
#error QUICK_FIXTURE must be defined by the build
#endif

static PtySession *start(int cols) {
    PtySession *s = pty_open(cols, 24);
    if (!s) return NULL;
    const char *argv[] = { QUICK_FIXTURE, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

static const PtyStyleKey g_legend[] = {
    { '.', PTY_ATTR_NONE, PTY_FG_DEFAULT, PTY_BG_DEFAULT },
    { 'A', PTY_ATTR_BOLD, PTY_FG_CYAN, PTY_BG_DEFAULT },     /* the frame, and a quick key */
    { 'B', PTY_ATTR_NONE, 97, PTY_COLOR_IDX(17) },           /* the selected row */
    { 'M', PTY_ATTR_BOLD, 97, PTY_COLOR_IDX(17) },           /* its '>' and its quick key */
    { 0, 0, 0, 0 } };

#define WAIT_DONE(s, what) do { \
    const char *const _d[] = { what, NULL }; \
    ASSERT_WAIT_REGION(s, PTY_REGION(0, 0, 1, (int)sizeof(what) - 1), _d, 3000); \
} while (0)

/* The letters the library assigns to the ten entries, top to bottom. */
static const char g_letters[] = "dtwalmeivz";

static const char *const g_menu80[] = {
    "done: nothing",
    "",
    "",
    "",
    "",
    "",
    "             ┌─ Actions ─────────────────────────────────────────┐",
    "             │ > d  d  Delete                                    │",
    "             │   t     Restart the whole service and its workers │",
    "             │   w     Rename                                    │",
    "             │   a     Archive                                   │",
    "             │   l     Label                                     │",
    "             │   m  t  Move                                      │",
    "             │   e     Export                                    │",
    "             │   i     Import                                    │",
    "             │   v     Verify                                    │",
    "             │   z     Zip                                       │",
    "             └───────────────────────────────────────────────────┘",
    NULL };

/* Ten entries at 80x24: the letters, in the accent colour, in a column of their own. */
static void test_ten_entries_at_80_columns(void) {
    PtySession *s = start(80);
    ASSERT(s != NULL, "start");
    pty_send_str(s, ":");
    ASSERT_WAIT_SCREEN(s, g_menu80, 3000);
    for (int i = 0; i < 10; i++) {
        char want[2] = { g_letters[i], '\0' }, w[64];
        snprintf(w, sizeof w, "row %d: the letter", i);
        ASSERT_EQ_STR(pty_cell_text(s, 7 + i, 17), want, w);
        if (i > 0) {
            snprintf(w, sizeof w, "row %d: the accent colour", i);
            ASSERT_EQ_INT(pty_cell_fg(s, 7 + i, 17), PTY_FG_CYAN, w);
            ASSERT_EQ_INT(pty_cell_bg(s, 7 + i, 17), PTY_BG_DEFAULT, w);
        }
    }
    const char *const sel[] = { "A.MBMBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.A",
                                "A...A...............................................A", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(7, 13, 2, 53), sel, g_legend);
    pty_send_str(s, "\x1b");
    pty_close(s);
}

/* A quick key runs its entry as Enter does; a serious entry asks first. */
static void test_quick_key_runs_and_confirms(void) {
    PtySession *s = start(80);
    ASSERT(s != NULL, "start");
    pty_send_str(s, ":l");
    WAIT_DONE(s, "done: Label");
    ASSERT_WAIT_GONE(s, "Actions", 3000);
    pty_send_str(s, ":m");
    WAIT_DONE(s, "done: Move");
    /* The serious entry: the confirmation opens, nothing is done yet. */
    pty_send_str(s, ":d");
    ASSERT_WAIT_FOR(s, "Delete Alpha? It cannot be undone.", 3000);
    ASSERT(pty_screen_contains(s, "[ Yes ]  [ No ]"), "Yes and No");
    ASSERT(pty_screen_contains(s, "done: Move"), "nothing was performed");
    pty_send_str(s, "\r");                                  /* Enter on the default No */
    ASSERT_WAIT_GONE(s, "Delete Alpha?", 3000);
    ASSERT(pty_screen_contains(s, "done: Move"), "still nothing performed");
    pty_send_str(s, ":t");                                  /* the other serious one */
    ASSERT_WAIT_FOR(s, "Restart Alpha?", 3000);
    pty_send_str(s, "y");
    WAIT_DONE(s, "done: Restart the whole service and its workers");
    /* A letter the menu does not have, and the list keys, still work. */
    pty_send_str(s, ":xjj\r");                              /* x is no quick key; j j Enter: Rename */
    WAIT_DONE(s, "done: Rename");
    pty_send_str(s, ":\x1b");
    ASSERT_WAIT_GONE(s, "Actions", 3000);
    pty_send_str(s, "\x1b");
    pty_close(s);
}

/* '?' in the menu lists the quick keys. */
static void test_menu_help(void) {
    PtySession *s = start(80);
    ASSERT(s != NULL, "start");
    pty_send_str(s, ":?");
    ASSERT_WAIT_FOR(s, "run the entry (a serious one asks first)", 3000);
    ASSERT(pty_screen_contains(s, "letter"), "the letter row");
    pty_send_str(s, "q");
    ASSERT_WAIT_FOR(s, "Restart the whole service", 3000);
    pty_send_str(s, "z");
    WAIT_DONE(s, "done: Zip");
    pty_close(s);
}

/* At 40 columns the long entry is cut with an ellipsis and its letter stays. */
static void test_narrow_width(void) {
    PtySession *s = start(40);
    ASSERT(s != NULL, "start");
    pty_send_str(s, ":");
    ASSERT_WAIT_FOR(s, "Actions", 3000);
    ASSERT(pty_screen_contains(s, "…"), "the long entry ends with an ellipsis");
    ASSERT(!pty_screen_contains(s, "workers"), "and is cut");
    int r = -1, c = -1;
    for (int row = 0; row < 24 && r < 0; row++)
        for (int col = 0; col < 40; col++)
            if (strcmp(pty_cell_text(s, row, col), "┌") == 0) {
                r = row;
                c = col;
                break;
            }
    ASSERT(r >= 0, "the box");
    for (int i = 0; i < 10; i++) {
        char want[2] = { g_letters[i], '\0' }, w[64];
        snprintf(w, sizeof w, "row %d: the letter stays visible", i);
        ASSERT_EQ_STR(pty_cell_text(s, r + 1 + i, c + 4), want, w);
    }
    ASSERT_EQ_STR(pty_cell_text(s, r + 2, c + 1), " ", "the long row, inside the box");
    ASSERT(pty_screen_contains(s, "t     Restart"), "the long entry's letter and the start of its name");
    ASSERT_EQ_INT(pty_cell_fg(s, r + 2, c + 4), PTY_FG_CYAN, "the letter keeps the accent colour");
    pty_send_str(s, "t");
    ASSERT_WAIT_FOR(s, "Restart Alpha?", 3000);
    pty_send_str(s, "y");
    ASSERT_WAIT_FOR(s, "done: Restart the whole", 3000);
    pty_close(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_ten_entries_at_80_columns),
    TEST_CASE(test_quick_key_runs_and_confirms),
    TEST_CASE(test_menu_help),
    TEST_CASE(test_narrow_width),
};

int main(int argc, char **argv) {
    return RUN_SUITE("quick", tests, 40, 120);
}
