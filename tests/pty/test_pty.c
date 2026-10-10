/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* PTY suite: terminal-mode behaviour of real programs through a pseudo-terminal. */

/*
 * PTY suite: real programs through a real pseudo-terminal, every screen
 * compared cell by cell with libptytest.
 *
 * The terminal-mode cases run the fixture inside a small shell script that
 * prints the terminal's flags (stty) before and after it, and the exit
 * status it left with -- so "the terminal is given back" is checked by the
 * terminal itself, not by the library under test.
 */

#include "test_helpers.h"

int g_tests_run = 0;
int g_tests_failed = 0;

#ifndef TERM_FIXTURE
#error TERM_FIXTURE must be defined by the build
#endif

/* Prints the four flags that matter, sorted, as one line. */
#define FLAGS "stty -a | tr ' ;' '\\n\\n' | grep -x -e icanon -e -icanon -e echo -e -echo " \
              "-e ixon -e -ixon -e isig -e -isig | LC_ALL=C sort | tr '\\n' ' '; echo"

static const char *const g_script =
    FLAGS "; \"$0\"; echo \"status=$?\"; " FLAGS "; echo END";

static PtySession *start_fixture(int cols, int rows) {
    PtySession *s = pty_open(cols, rows);
    if (!s) return NULL;
    const char *argv[] = { "/bin/sh", "-c", g_script, TERM_FIXTURE, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

/* Leaves the fixture with @p key and checks the screen the shell shows
 * afterwards: the flags as before, and the exit status. */
static void check_leave(const char *key, const char *shell_note, const char *status_line) {
    PtySession *s = start_fixture(40, 6);
    ASSERT(s != NULL, "start the fixture under sh");
    const char *const ready[] = { "READY 6x40", "key: none", "", "", "", "bottom", NULL };
    ASSERT_WAIT_SCREEN(s, ready, 5000);
    pty_send_str(s, key);
    /* Killed by a signal, the shell says so on a line of its own. */
    const char *const after_note[] = {
        "echo icanon isig ixon", shell_note ? shell_note : "", status_line,
        "echo icanon isig ixon", "END", "", NULL };
    const char *const after_plain[] = {
        "echo icanon isig ixon", status_line,
        "echo icanon isig ixon", "END", "", "", NULL };
    const char *const *after = shell_note ? after_note : after_plain;
    ASSERT_WAIT_SCREEN(s, after, 5000);
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

static void test_quit_restores(void)      { check_leave("q", NULL, "status=0"); }
static void test_exit_restores(void)      { check_leave("e", NULL, "status=3"); }
static void test_sigterm_restores(void)   { check_leave("t", "Terminated", "status=143"); }
static void test_sighup_restores(void)    { check_leave("h", "Hangup", "status=129"); }

static void test_keys_through_the_terminal(void) {
    PtySession *s = start_fixture(40, 6);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "READY 6x40", 5000);
    static const struct { PtyKey key; const char *name; } keys[] = {
        { PTY_KEY_UP, "key: Up" },
        { PTY_KEY_PGDN, "key: PgDn" },
        { PTY_KEY_F5, "key: F5" },
        { PTY_KEY_SHIFT_TAB, "key: Shift+Tab" },
        { PTY_KEY_CTRL('s'), "key: Ctrl+S" },   /* flow control is off */
        { PTY_KEY_CTRL('c'), "key: Ctrl+C" },   /* signal keys are off */
        { PTY_KEY_CTRL('z'), "key: Ctrl+Z" },
        { PTY_KEY_ESC, "key: Esc" },
    };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
        pty_send_key(s, keys[i].key);
        const char *const row[] = { keys[i].name, NULL };
        ASSERT_WAIT_REGION(s, PTY_REGION(1, 0, 1, 40), row, 3000);
    }
    pty_send_str(s, "\xC5\x91");
    const char *const accented[] = { "READY 6x40", "key: \xC5\x91", "", "", "", "bottom", NULL };
    ASSERT_WAIT_SCREEN(s, accented, 3000);
    pty_send_str(s, "q");
    ASSERT_WAIT_FOR(s, "END", 5000);
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

static void test_resize_redraws(void) {
    PtySession *s = start_fixture(40, 6);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "READY 6x40", 5000);
    ASSERT_EQ_INT(pty_resize(s, 30, 8), 0, "resize the terminal");
    const char *const bigger[] = { "READY 8x30", "key: none", "", "", "", "", "", "bottom", NULL };
    ASSERT_WAIT_SCREEN(s, bigger, 5000);
    static const PtyStyleKey legend[] = {
        { '.', PTY_ATTR_NONE, 0, 0 }, { 'R', PTY_ATTR_REVERSE, 0, 0 }, { 0, 0, 0, 0 } };
    const char *const bottom[] = { "RRRRRR........................", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(7, 0, 1, 30), bottom, legend);
    ASSERT_EQ_INT(pty_resize(s, 20, 4), 0, "resize smaller");
    const char *const smaller[] = { "READY 4x20", "key: none", "", "bottom", NULL };
    ASSERT_WAIT_SCREEN(s, smaller, 5000);
    pty_send_str(s, "q");
    ASSERT_WAIT_FOR(s, "END", 5000);
    pty_close(s);
}

static void test_suspend_and_resume(void) {
    PtySession *s = start_fixture(40, 6);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "READY 6x40", 5000);
    pty_send_str(s, "x");
    ASSERT_WAIT_FOR(s, "key: x", 3000);
    pty_send_str(s, "s");
    /* The normal screen is back, with the shell's first line on it, and
     * the terminal is in canonical mode: Enter ends the fixture's read. */
    const char *const suspended[] = {
        "echo icanon isig ixon", "SUSPENDED, press Enter", "", "", "", "", NULL };
    ASSERT_WAIT_SCREEN(s, suspended, 3000);
    pty_send_key(s, PTY_KEY_ENTER);
    const char *const resumed[] = { "READY 6x40", "key: x", "", "", "", "bottom", NULL };
    ASSERT_WAIT_SCREEN(s, resumed, 3000);
    pty_send_str(s, "q");
    ASSERT_WAIT_FOR(s, "END", 5000);
    pty_close(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_quit_restores),
    TEST_CASE(test_exit_restores),
    TEST_CASE(test_sigterm_restores),
    TEST_CASE(test_sighup_restores),
    TEST_CASE(test_keys_through_the_terminal),
    TEST_CASE(test_resize_redraws),
    TEST_CASE(test_suspend_and_resume),
};

int main(int argc, char **argv) {
    return RUN_SUITE("pty", tests, 40, 120);
}
