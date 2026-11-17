/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* PTY suite of the file chooser: the three modes, the keys, the colours. */

#include "fc_tree.h"
#include "test_helpers.h"

int g_tests_run = 0;
int g_tests_failed = 0;

#ifndef FILECHOOSER_FIXTURE
#error FILECHOOSER_FIXTURE must be defined by the build
#endif

#define ROWS 24
#define COLS 80

static char g_root[256];

static PtySession *start(const char *mode, const char *dir, const char *filter) {
    PtySession *s = pty_open(COLS, ROWS);
    if (!s) return NULL;
    const char *argv[] = { FILECHOOSER_FIXTURE, mode, dir, filter, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

static const PtyStyleKey g_legend[] = {
    { '.', PTY_ATTR_NONE, PTY_FG_DEFAULT, PTY_BG_DEFAULT },
    { 'S', PTY_ATTR_NONE, 97, PTY_COLOR_IDX(17) },          /* the selected row: dark blue */
    { 'M', PTY_ATTR_BOLD, 97, PTY_COLOR_IDX(17) },          /* its '>' */
    { 0, 0, 0, 0 } };

static void test_open_a_file(void) {
    PtySession *s = start("open", g_root, NULL);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "alpha/", 5000);
    ASSERT_SCREEN_CONTAINS(s, "Open file");
    ASSERT_SCREEN_CONTAINS(s, "6 entries");
    ASSERT_SCREEN_CONTAINS(s, "2026-01-02 03:04");
    ASSERT_SCREEN_CONTAINS(s, "  5.0M");
    /* The first row is selected: '>' in the first column, dark blue across the row. */
    const char *const sel[] = { "MS", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(5, 4, 1, 2), sel, g_legend);
    const char *const rest[] = { "..", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(6, 4, 1, 2), rest, g_legend);
    pty_send_key(s, PTY_KEY_DOWN);
    pty_send_key(s, PTY_KEY_ENTER);                       /* into alpha */
    ASSERT_WAIT_FOR(s, "inner.txt", 5000);
    ASSERT_SCREEN_CONTAINS(s, "1 entry");
    pty_send_key(s, PTY_KEY_DOWN);
    pty_send_key(s, PTY_KEY_ENTER);
    char want[400];
    snprintf(want, sizeof want, "chosen: %s/alpha/inner.txt", g_root);
    if (strlen(want) > 78) want[78] = '\0';       /* a deep build tree: the 80-column row cuts the path */
    ASSERT_WAIT_FOR(s, want, 5000);
    ASSERT_SCREEN_NOT_CONTAINS(s, "Open file");
    pty_close(s);
}

static void test_choose_a_directory(void) {
    PtySession *s = start("dir", g_root, NULL);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "3 entries", 5000);
    ASSERT_SCREEN_NOT_CONTAINS(s, "a.txt");
    pty_send_str(s, "beta\r");
    ASSERT_WAIT_FOR(s, "0 entries", 5000);
    pty_send_key(s, PTY_KEY_ENTER);
    char want[400];
    snprintf(want, sizeof want, "chosen: %s/beta", g_root);
    ASSERT_WAIT_FOR(s, want, 5000);
    pty_close(s);
}

static void test_save_and_replace(void) {
    PtySession *s = start("save", g_root, NULL);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "Save as", 5000);
    pty_send_str(s, "a.txt\r");
    ASSERT_WAIT_FOR(s, "a.txt exists.", 5000);
    ASSERT_SCREEN_CONTAINS(s, "[ Yes ]");
    pty_send_str(s, "n");
    ASSERT_WAIT_GONE(s, "Replace file", 5000);
    ASSERT_SCREEN_CONTAINS(s, "Save as");
    pty_send_str(s, "\r");
    ASSERT_WAIT_FOR(s, "Replace file", 5000);
    pty_send_str(s, "y");
    char want[400];
    snprintf(want, sizeof want, "chosen: %s/a.txt", g_root);
    ASSERT_WAIT_FOR(s, want, 5000);
    pty_close(s);
}

static void test_tab_hidden_filter_and_cancel(void) {
    PtySession *s = start("open", g_root, "*.eml");
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "filter *.eml", 5000);
    ASSERT_SCREEN_NOT_CONTAINS(s, "a.txt");
    pty_send_str(s, "\x1b" "h");                          /* Alt-H */
    ASSERT_WAIT_FOR(s, "hidden shown", 5000);
    ASSERT_SCREEN_CONTAINS(s, ".dots/");
    pty_send_str(s, "b.\t");                               /* completes b.eml */
    char want[400];
    snprintf(want, sizeof want, "%s/b.eml", g_root);
    ASSERT_WAIT_FOR(s, want, 5000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_FOR(s, "cancelled", 5000);
    pty_send_str(s, "o");                                 /* open again */
    ASSERT_WAIT_FOR(s, "Open file", 5000);
    pty_send_str(s, "\x03");
    ASSERT_WAIT_GONE(s, "Open file", 5000);
    pty_close(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_open_a_file),
    TEST_CASE(test_choose_a_directory),
    TEST_CASE(test_save_and_replace),
    TEST_CASE(test_tab_hidden_filter_and_cancel),
};

int main(int argc, char **argv) {
    setenv("TZ", "UTC", 1);
    setenv("TERM", "xterm-256color", 1);
    unsetenv("COLORTERM");
    snprintf(g_root, sizeof g_root, "%s/fc-pty-%d", TUIKIT_TEST_TMPDIR, (int)getpid());
    fc_tree_make(g_root);
    int rc = RUN_SUITE("filechooser-pty", tests, 20, 120);
    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf '%s/fc-pty-%d'", TUIKIT_TEST_TMPDIR, (int)getpid());
    if (system(cmd) != 0) fprintf(stderr, "cannot remove the test tree\n");
    return rc;
}
