/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-21 */
/* PTY suite of the history view: rows, markers, Revert asked and answered, refusal, states. */

/*
 * policies/undo-model.md through a real 80x24 terminal and a stub program
 * (tests/pty/histview_fixture.c): the rows with their markers, Revert asked
 * and answered No (nothing called) and Yes (called once with the step id),
 * the server's refusal in one line, a not-revertable step that opens no
 * dialog, the recent-changes mode, the help, the empty and error states.
 * Text, position and style are compared cell by cell.
 */

#include "test_helpers.h"

int g_tests_run = 0;
int g_tests_failed = 0;

#ifndef HISTVIEW_FIXTURE
#error HISTVIEW_FIXTURE must be defined by the build
#endif

static PtySession *start(const char *mode) {
    PtySession *s = pty_open(80, 24);
    if (!s) return NULL;
    const char *argv[] = { HISTVIEW_FIXTURE, mode, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

static const PtyStyleKey g_legend[] = {
    { '.', PTY_ATTR_NONE, PTY_FG_DEFAULT, PTY_BG_DEFAULT },
    { 'H', PTY_ATTR_BOLD, PTY_FG_WHITE, PTY_BG_BLUE },       /* the header bar */
    { 'O', PTY_ATTR_BOLD, PTY_FG_GREEN, PTY_BG_DEFAULT },    /* the stub's status line */
    { 'S', PTY_ATTR_BOLD, 97, PTY_COLOR_IDX(17) },         /* the selected row */
    { 'T', PTY_ATTR_NONE, 97, PTY_COLOR_IDX(17) },           /* the selected row, after the marker */
    { 'B', PTY_ATTR_BOLD, PTY_FG_DEFAULT, PTY_BG_DEFAULT },  /* the table header */
    { 'A', PTY_ATTR_BOLD, PTY_FG_CYAN, PTY_BG_DEFAULT },     /* "reverts N" */
    { 'Y', PTY_ATTR_NONE, PTY_FG_YELLOW, PTY_BG_DEFAULT },   /* "not revertable", the empty state */
    { 'R', PTY_ATTR_BOLD, PTY_FG_RED, PTY_BG_DEFAULT },      /* a refusal, the error state */
    { 0, 0, 0, 0 } };

#define SCREEN(s, ...) do { \
    const char *const _w[] = { __VA_ARGS__, NULL }; \
    ASSERT_WAIT_SCREEN(s, _w, 5000); \
} while (0)

#define STATUS(c, l, r) "calls=" #c " last=" #l " reloads=" #r

#define HEAD "  Time             Who    Operation Thing    Old value New value  Note          "
static const char *const g_body[5] = {
    "  2026-10-21 10:07 ann    set       status   OPEN      DONE                     ",
    "  2026-10-21 10:06 bob    revert    title    New       Old        reverts 3     ",
    "  2026-10-21 10:05 bob    set       title    Old       New                      ",
    "  2026-10-21 10:04 ann    send      mail               sent       not revertable",
    "  2026-10-21 10:03 ann    set       priority LOW       HIGH                     ",
};

/* The whole screen as the fixture draws it with row @p sel selected ('>' in the
 * first cell): the wait compares every row, the empty ones too. */
static char g_rows[5][96];
static void fill_rows(int sel) {
    for (int i = 0; i < 5; i++) {
        snprintf(g_rows[i], sizeof g_rows[i], "%s", g_body[i]);
        if (i == sel) g_rows[i][0] = '>';
    }
}

#define FULL(s, status, sel) do { \
    fill_rows(sel); \
    const char *const _w[] = { " History", status, "", HEAD, g_rows[0], g_rows[1], g_rows[2], \
                               g_rows[3], g_rows[4], NULL }; \
    ASSERT_WAIT_SCREEN(s, _w, 5000); \
} while (0)

/* The rows, the markers, the colours of the header, the selection and the notes. */
static void test_rows_and_markers(void) {
    PtySession *s = start("object");
    ASSERT(s != NULL, "start");
    FULL(s, STATUS(0, 0, 0), 0);
    const char *const head[] = { "HHHHHHHH", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 1, 8), head, g_legend);
    const char *const hdr[] = { "BBBBBB", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(3, 0, 1, 6), hdr, g_legend);
    const char *const sel[] = { "STTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(4, 0, 1, 80), sel, g_legend);
    const char *const rev[] = { "AAAAAAAAA", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(5, 66, 1, 9), rev, g_legend);
    const char *const nrev[] = { "YYYYYYYYYYYYYY", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(7, 66, 1, 14), nrev, g_legend);
    pty_close(s);
}

/* The list keys move; the row under the cursor is the selected one. */
static void test_scrolling(void) {
    PtySession *s = start("object");
    ASSERT(s != NULL, "start");
    FULL(s, STATUS(0, 0, 0), 0);
    pty_send_str(s, "jj");
    ASSERT_WAIT_FOR(s, "> 2026-10-21 10:05", 2000);
    const char *const sel[] = { "STTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTTT", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(6, 0, 1, 80), sel, g_legend);
    pty_send_str(s, "G");
    ASSERT_WAIT_FOR(s, "> 2026-10-21 10:03", 2000);
    ASSERT_REGION_STYLE(s, PTY_REGION(8, 0, 1, 80), sel, g_legend);
    pty_send_str(s, "g");
    ASSERT_WAIT_FOR(s, "> 2026-10-21 10:07", 2000);
    ASSERT_REGION_STYLE(s, PTY_REGION(4, 0, 1, 80), sel, g_legend);
    pty_close(s);
}

/* Enter asks; Enter on the default (No) does nothing; n and Esc do nothing. */
static void test_revert_answered_no(void) {
    PtySession *s = start("object");
    ASSERT(s != NULL, "start");
    FULL(s, STATUS(0, 0, 0), 0);
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "Revert step 5 (set, status): status becomes \"OPEN\"?", 3000);
    ASSERT_WAIT_FOR(s, "No", 1000);
    pty_send_key(s, PTY_KEY_ENTER);                         /* the default: No */
    ASSERT_WAIT_GONE(s, "Revert step 5", 2000);
    FULL(s, STATUS(0, 0, 0), 0);
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "Revert step 5", 3000);
    pty_send_str(s, "n");
    ASSERT_WAIT_GONE(s, "Revert step 5", 2000);
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "Revert step 5", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_GONE(s, "Revert step 5", 2000);
    FULL(s, STATUS(0, 0, 0), 0);
    ASSERT_EQ_INT(pty_screen_contains(s, "reverted"), 0, "nothing was reverted");
    pty_close(s);
}

/* Yes: the callback is called once with the step id and the view says to reload. */
static void test_revert_answered_yes(void) {
    PtySession *s = start("object");
    ASSERT(s != NULL, "start");
    FULL(s, STATUS(0, 0, 0), 0);
    pty_send_str(s, "j");
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "Revert step 4 (revert, title): title becomes \"New\"?", 3000);
    pty_send_str(s, "y");
    ASSERT_WAIT_FOR(s, STATUS(1, 4, 1), 3000);
    ASSERT_WAIT_FOR(s, "reverted step 4", 2000);
    const char *const ok[] = { "OOOOOOOOOOOOOOO", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(23, 0, 1, 15), ok, g_legend);
    pty_close(s);
}

/* The ':' menu has Revert; choosing it asks the same question. */
static void test_menu_revert(void) {
    PtySession *s = start("object");
    ASSERT(s != NULL, "start");
    FULL(s, STATUS(0, 0, 0), 0);
    pty_send_str(s, "jj:");
    ASSERT_WAIT_FOR(s, "Actions", 3000);
    ASSERT_WAIT_FOR(s, "Revert", 1000);
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "Revert step 3 (set, title): title becomes \"Old\"?", 3000);
    pty_send_str(s, "y");
    ASSERT_WAIT_FOR(s, STATUS(1, 3, 1), 3000);
    pty_close(s);
}

/* The server's refusal, in its words, in one line. */
static void test_refusal_line(void) {
    PtySession *s = start("object");
    ASSERT(s != NULL, "start");
    FULL(s, STATUS(0, 0, 0), 0);
    pty_send_str(s, "G");
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "Revert step 1 (set, priority)", 3000);
    pty_send_str(s, "y");
    ASSERT_WAIT_FOR(s, STATUS(1, 1, 0), 3000);
    ASSERT_WAIT_FOR(s, "step 1 was overtaken by a later change, not yours", 2000);
    const char *const err[] = { "RRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRRR", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(23, 0, 1, 49), err, g_legend);
    pty_send_str(s, "k");                                   /* the next key clears it */
    ASSERT_WAIT_GONE(s, "overtaken", 2000);
    pty_close(s);
}

/* A step marked not revertable shows a notice and opens no dialog, by Enter or by ':'. */
static void test_not_revertable(void) {
    PtySession *s = start("object");
    ASSERT(s != NULL, "start");
    FULL(s, STATUS(0, 0, 0), 0);
    pty_send_str(s, "jjj");
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "step 2 is not revertable", 2000);
    ASSERT_EQ_INT(pty_screen_contains(s, "Revert step"), 0, "no dialog");
    pty_send_str(s, "kj");                                  /* a key clears the notice */
    pty_send_str(s, ":");
    ASSERT_WAIT_FOR(s, "step 2 is not revertable", 2000);
    ASSERT_WAIT_GONE(s, "Actions", 1000);
    ASSERT_WAIT_GONE(s, "Revert step", 1000);
    pty_send_str(s, "y");
    ASSERT_WAIT_FOR(s, STATUS(0, 0, 0), 1000);
    pty_close(s);
}

/* Recent changes: a column "object" is shown. */
static void test_recent_mode(void) {
    PtySession *s = start("recent");
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "  Time             Who    Operation Object   Thing  Old    New    Note", 3000);
    ASSERT_WAIT_FOR(s, "> 2026-10-21 10:07 ann    set       TUI-9    status OPEN   DONE", 1000);
    ASSERT_WAIT_FOR(s, "  2026-10-21 10:06 bob    revert    TUI-9    title  New    Old    reverts 3", 1000);
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "Revert step 5 (set, status of TUI-9)", 3000);
    pty_close(s);
}

/* The help is made from the widget's key table; q closes it, and q then goes back. */
static void test_help_and_back(void) {
    PtySession *s = start("object");
    ASSERT(s != NULL, "start");
    FULL(s, STATUS(0, 0, 0), 0);
    pty_send_str(s, "?");
    ASSERT_WAIT_FOR(s, "History shortcuts", 3000);
    ASSERT_WAIT_FOR(s, "Revert step", 1000);
    ASSERT_WAIT_FOR(s, "Back", 1000);
    pty_send_str(s, "q");
    ASSERT_WAIT_GONE(s, "History shortcuts", 2000);
    pty_send_str(s, "q");
    ASSERT_WAIT_FOR(s, "back", 2000);
    pty_close(s);
}

/* No rows: the empty state; the rows cannot be asked about. */
static void test_empty_state(void) {
    PtySession *s = start("empty");
    ASSERT(s != NULL, "start");
    SCREEN(s, " History", STATUS(0, 0, 0), "", "no recorded changes");
    const char *const hint[] = { "YYYYYYYYYYYYYYYYYYY", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(3, 0, 1, 19), hint, g_legend);
    pty_send_key(s, PTY_KEY_ENTER);
    pty_send_str(s, ":");
    SCREEN(s, " History", STATUS(0, 0, 0), "", "no recorded changes");
    pty_close(s);
}

/* The history could not be read: the error state, in the error colour. */
static void test_error_state(void) {
    PtySession *s = start("failed");
    ASSERT(s != NULL, "start");
    SCREEN(s, " History", STATUS(0, 0, 0), "", "could not be read");
    const char *const err[] = { "RRRRRRRRRRRRRRRRR", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(3, 0, 1, 17), err, g_legend);
    pty_send_key(s, PTY_KEY_ENTER);
    SCREEN(s, " History", STATUS(0, 0, 0), "", "could not be read");
    pty_close(s);
}

/* Esc goes back too; ctrl-c quits. */
static void test_esc_and_quit(void) {
    PtySession *s = start("object");
    ASSERT(s != NULL, "start");
    FULL(s, STATUS(0, 0, 0), 0);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_FOR(s, "back", 2000);
    pty_send_str(s, "\x03");
    ASSERT_EQ_INT(pty_wait_exit(s, 3000), 0, "ctrl-c quits");
    pty_close(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_rows_and_markers),
    TEST_CASE(test_scrolling),
    TEST_CASE(test_revert_answered_no),
    TEST_CASE(test_revert_answered_yes),
    TEST_CASE(test_menu_revert),
    TEST_CASE(test_refusal_line),
    TEST_CASE(test_not_revertable),
    TEST_CASE(test_recent_mode),
    TEST_CASE(test_help_and_back),
    TEST_CASE(test_empty_state),
    TEST_CASE(test_error_state),
    TEST_CASE(test_esc_and_quit),
};

int main(int argc, char **argv) {
    return RUN_SUITE("histview-pty", tests, 60, 120);
}
