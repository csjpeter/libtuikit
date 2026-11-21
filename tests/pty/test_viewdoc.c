/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-21 */
/* PTY suite of the help page and the view description. */

#include "test_helpers.h"

int g_tests_run = 0;
int g_tests_failed = 0;

#ifndef VIEWDOC_FIXTURE
#error VIEWDOC_FIXTURE must be defined by the build
#endif

static PtySession *start(const char *arg) {
    PtySession *s = pty_open(80, 24);
    if (!s) return NULL;
    const char *argv[] = { VIEWDOC_FIXTURE, arg, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

static void test_page_and_description(void) {
    PtySession *s = start(NULL);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "check: ok", 5000);
    pty_send_str(s, "?");
    ASSERT_WAIT_FOR(s, "Work help", 5000);
    ASSERT_SCREEN_CONTAINS(s, "Press d: what this view is and what each column means.");
    ASSERT_SCREEN_CONTAINS(s, "This view");
    ASSERT_SCREEN_CONTAINS(s, "open the row");
    ASSERT_SCREEN_CONTAINS(s, "Every view");
    ASSERT_SCREEN_CONTAINS(s, "this page");
    ASSERT_SCREEN_NOT_CONTAINS(s, "what it is doing now");
    pty_send_str(s, "d");
    ASSERT_WAIT_FOR(s, "Work view", 5000);
    ASSERT_SCREEN_CONTAINS(s, "The tickets in work and what works on them.");
    ASSERT_SCREEN_CONTAINS(s, "ITEM   the ticket or its session");
    ASSERT_SCREEN_CONTAINS(s, "STATE  what it is doing now");
    ASSERT_SCREEN_CONTAINS(s, "NOTE   why it needs you, if it does");
    ASSERT_SCREEN_NOT_CONTAINS(s, "Press d");
    pty_send_key(s, PTY_KEY_ESC);                    /* closes the description and the page */
    ASSERT_WAIT_GONE(s, "Work view", 5000);
    ASSERT_SCREEN_NOT_CONTAINS(s, "Work help");
    pty_send_str(s, "q");
    pty_close(s);
}

static void test_search_keeps_d(void) {
    PtySession *s = start(NULL);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "check: ok", 5000);
    pty_send_str(s, "?");
    ASSERT_WAIT_FOR(s, "Work help", 5000);
    pty_send_str(s, "/d");                           /* a d typed in the search is text */
    ASSERT_WAIT_FOR(s, "describe the row", 5000);
    ASSERT_SCREEN_NOT_CONTAINS(s, "Work view");
    pty_close(s);
}

static void test_a_column_not_described(void) {
    PtySession *s = start("bad");
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "check: column \"NOTE\" is not described", 5000);
    pty_close(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_page_and_description),
    TEST_CASE(test_search_keeps_d),
    TEST_CASE(test_a_column_not_described),
};

int main(int argc, char **argv) {
    (void)argc; (void)argv;
    return RUN_SUITE("viewdoc", tests, 3, 60);
}
