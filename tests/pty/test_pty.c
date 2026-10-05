/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

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
#ifndef DEMO
#error DEMO must be defined by the build
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

/* ── The demo program ───────────────────────────────────────────────── */

static PtySession *start_demo(int cols, int rows) {
    PtySession *s = pty_open(cols, rows);
    if (!s) return NULL;
    const char *argv[] = { DEMO, NULL };
    if (pty_run(s, argv) != 0) {
        pty_close(s);
        return NULL;
    }
    return s;
}

/* Waits until @p text is no longer on the screen: a dialog has closed.
 * (Waiting for text that is on the screen either way proves nothing.) */
static int text_gone(PtySession *s, void *ctx) {
    return !pty_screen_contains(s, (const char *)ctx);
}

#define ASSERT_WAIT_GONE(s, text, timeout_ms) do { \
    g_tests_run++; \
    if (pty_wait_until(s, text_gone, (void *)(text), timeout_ms) != 0) { \
        PTY_ASSERT_FAIL_S_(s, (const char *)NULL, "\"%s\" still on the screen", text); \
        pty_screen_dump(s, PTY_ASSERT_OUT); \
        return; \
    } \
} while (0)

static const PtyStyleKey g_legend[] = {
    { '.', PTY_ATTR_NONE, 0, 0 }, { 'R', PTY_ATTR_REVERSE, 0, 0 },
    { 'B', PTY_ATTR_BOLD, 0, 0 }, { 0, 0, 0, 0 } };

static void test_demo_navigation_and_scroll(void) {
    PtySession *s = start_demo(50, 10);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "q quit", 5000);
    for (int i = 0; i < 3; i++) pty_send_key(s, PTY_KEY_DOWN);
    const char *const budget[] = {
        "┌─ Messages ───┐┌─ Budget 2027 ──────────────────┐",
        "│Welcome       ││Line 1 of the budget.           │",
        "│Árvíztűrő tük…││Line 2.                         │",
        "│Wide characte…││Line 3.                         │",
        "│Budget 2027   ││Line 4.                         │",
        "│Meeting notes ││Line 5.                         │",
        "│Re: Budget 20…││Line 6.                         │",
        "│              ││Line 7.                         │",
        "└──────────────┘└────────────────────────────────┘",
        "Tab pane  / search  q quit                   10x50",
        NULL };
    ASSERT_WAIT_SCREEN(s, budget, 3000);
    const char *const sel[] = { "BRRRRRRRRRRRRRRB", NULL };   /* focused frame bold */
    ASSERT_REGION_STYLE(s, PTY_REGION(4, 0, 1, 16), sel, g_legend);

    /* Tab: the body has the focus; the list keeps its selection, bold. */
    pty_send_key(s, PTY_KEY_TAB);
    pty_send_key(s, PTY_KEY_PGDN);
    const char *const scrolled[] = {
        "│Line 6.                         │",     /* never scrolled past the end */
        "│Line 7.                         │",
        "│Line 8.                         │",
        "│Line 9.                         │",
        "│Line 10.                        │",
        "│Line 11.                        │",
        "│Line 12.                        │",
        NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(1, 16, 7, 34), scrolled, 3000);
    const char *const unfocused[] = { ".BBBBBBBBBBBBBB.", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(4, 0, 1, 16), unfocused, g_legend);
    pty_send_key(s, PTY_KEY_CTRL('c'));
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

static void test_demo_search(void) {
    PtySession *s = start_demo(50, 10);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "q quit", 5000);
    pty_send_str(s, "/");
    ASSERT_WAIT_FOR(s, "Subject contains:", 3000);
    pty_send_str(s, "Re:");
    const char *const prompt[] = {
        "┌─ Search ───────────────────────┐",
        "│ Subject contains:              │",
        "│ Re:                            │",
        "└────────────────────────────────┘",
        NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(3, 8, 4, 34), prompt, 3000);
    ASSERT_WAIT_CURSOR(s, 5, 13, 3000, "cursor in the input line, after Re:");
    pty_send_key(s, PTY_KEY_ENTER);
    const char *const status[] = { "Found \"Re:\"                                  10x50", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(9, 0, 1, 50), status, 3000);
    const char *const sel[] = { "BRRRRRRRRRRRRRRB", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(6, 0, 1, 16), sel, g_legend);
    ASSERT_WAIT_FOR(s, "Looks good to me.", 3000);

    /* No match, and a cancelled search. */
    pty_send_str(s, "/zzz");
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_WAIT_FOR(s, "No match for \"zzz\"", 3000);
    pty_send_str(s, "/abc");
    ASSERT_WAIT_FOR(s, "abc", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_GONE(s, "Subject contains:", 3000);
    /* A new command clears the previous message; a cancelled search
     * leaves no message of its own. */
    const char *const help[] = { "Tab pane  / search  q quit                   10x50", NULL };
    ASSERT_REGION_EQ(s, PTY_REGION(9, 0, 1, 50), help);
    /* Quit: No keeps it running, Yes ends it. */
    pty_send_str(s, "q");
    ASSERT_WAIT_FOR(s, "Leave the demo?", 3000);
    pty_send_str(s, "n");
    ASSERT_WAIT_GONE(s, "Leave the demo?", 3000);
    pty_send_str(s, "q");
    ASSERT_WAIT_FOR(s, "Leave the demo?", 3000);
    pty_send_key(s, PTY_KEY_LEFT);
    pty_send_key(s, PTY_KEY_ENTER);
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

static void test_demo_resize(void) {
    PtySession *s = start_demo(50, 10);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "10x50", 5000);
    ASSERT_EQ_INT(pty_resize(s, 36, 7), 0, "resize");
    const char *const small[] = {
        "┌─ Messag…─┐┌─ Welcome ────────────┐",
        "│Welcome   ││Welcome to the tuikit │",
        "│Árvíztűrő…││demo.                 │",
        "│Wide char…││                      │",
        "│Budget 20…││Use the arrow keys to │",
        "└──────────┘└──────────────────────┘",
        "Tab pane  / search  q quit      7x36",
        NULL };
    ASSERT_WAIT_SCREEN(s, small, 5000);
    pty_send_key(s, PTY_KEY_CTRL('c'));
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

/* Not a terminal on standard input: the plain listing (email-cli's
 * non-TTY fallback). */
static void test_demo_not_a_tty(void) {
    PtySession *s = pty_open(40, 8);
    ASSERT(s != NULL, "pty_open");
    const char *argv[] = { "/bin/sh", "-c", "exec \"$0\" </dev/null", DEMO, NULL };
    ASSERT(pty_run(s, argv) == 0, "start");
    ASSERT_EXIT_STATUS(s, 0, 5000);
    const char *const expect[] = {
        "1. Welcome", "2. Árvíztűrő tükörfúrógép", "3. Wide characters",
        "4. Budget 2027", "5. Meeting notes", "6. Re: Budget 2027", "", "", NULL };
    ASSERT_SCREEN_EQ(s, expect);
    pty_close(s);
}

/* ── The compose view: the form, the editor and the checklist ───────── */

static void test_demo_compose_and_send(void) {
    PtySession *s = start_demo(60, 14);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "q quit", 5000);
    pty_send_str(s, "c");
    const char *const empty[] = {
        " New message",
        "To:",
        "Subject:",
        "", "", "", "",
        "────────────────────────────────────────────────────────────",
        "", "", "", "", "",
        "[header]  Ctrl-O switch  Ctrl-S send  Esc discard      14x60",
        NULL };
    ASSERT_WAIT_SCREEN(s, empty, 3000);
    ASSERT_WAIT_CURSOR(s, 1, 9, 3000, "cursor in the To field");

    /* Completion: both contacts with "al", the second one taken. */
    pty_send_str(s, "al");
    const char *const dropdown[] = {
        "To:      al",
        "         \xE2\x96\xB6 alice@example.com  Alice Smith",
        "         \xE2\x96\xB6 albert@example.org  Albert",
        "Subject:",
        NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(1, 0, 4, 60), dropdown, 3000);
    pty_send_key(s, PTY_KEY_DOWN);
    pty_send_key(s, PTY_KEY_TAB);
    /* After a separator the next address completes on its own. */
    pty_send_str(s, "; bo");
    ASSERT_WAIT_FOR(s, "bob@example.net  Bob", 3000);
    pty_send_key(s, PTY_KEY_ENTER);
    pty_send_key(s, PTY_KEY_TAB);
    pty_send_str(s, "Budget");
    ASSERT_WAIT_CURSOR(s, 2, 15, 3000, "cursor in the Subject field");
    pty_send_key(s, PTY_KEY_CTRL('o'));
    pty_send_str(s, "Line one\rLine two\tend");
    const char *const written[] = {
        " New message",
        "To:      albert@example.org; bob@example.net",
        "Subject: Budget",
        "", "", "", "",
        "────────────────────────────────────────────────────────────",
        "Line one",
        "Line two        end",
        "", "", "",
        "[body]  Ctrl-O switch  Ctrl-S send  Esc discard  2:20  14x60",
        NULL };
    ASSERT_WAIT_SCREEN(s, written, 3000);
    ASSERT_WAIT_CURSOR(s, 9, 19, 3000, "cursor after \"end\"");

    /* Send: the labels first. */
    pty_send_key(s, PTY_KEY_CTRL('s'));
    ASSERT_WAIT_FOR(s, "[ ] Personal", 3000);
    pty_send_key(s, PTY_KEY_DOWN);
    pty_send_str(s, " ");
    const char *const labels[] = {
        " New message",
        "To:      albert@example.org; bob@example.net",
        "Subject: Budget",
        "",
        "                     ┌─ Labels ───────┐",
        "                     │ [ ] Work       │",
        "                     │ [x] Personal   │",
        "─────────────────────│ [ ] Urgent     │─────────────────────",
        "Line one             └────────────────┘",
        "Line two        end",
        "", "", "",
        "[body]  Ctrl-O switch  Ctrl-S send  Esc discard  2:20  14x60",
        NULL };
    ASSERT_WAIT_SCREEN(s, labels, 3000);
    pty_send_key(s, PTY_KEY_ENTER);
    const char *const sent[] = {
        "┌─ Messages ───────┐┌─ Budget ─────────────────────────────┐",
        "│Welcome           ││To: albert@example.org;               │",
        "│Árvíztűrő tükörfú…││bob@example.net                       │",
        "│Wide characters   ││Labels: Personal                      │",
        "│Budget 2027       ││                                      │",
        "│Meeting notes     ││Line one                              │",
        "│Re: Budget 2027   ││Line two        end                   │",
        "│Budget            ││                                      │",
        "│                  ││                                      │",
        "│                  ││                                      │",
        "│                  ││                                      │",
        "│                  ││                                      │",
        "└──────────────────┘└──────────────────────────────────────┘",
        "Sent to albert@example.org; bob@example.net            14x60",
        NULL };
    ASSERT_WAIT_SCREEN(s, sent, 3000);
    const char *const sel[] = { "BRRRRRRRRRRRRRRRRRRB", NULL };   /* the new message, selected */
    ASSERT_REGION_STYLE(s, PTY_REGION(7, 0, 1, 20), sel, g_legend);
    pty_send_key(s, PTY_KEY_CTRL('c'));
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

/* Sending without a recipient points at the field; Esc discards from the
 * header and from the body; a cancelled checklist goes back to the form. */
static void test_demo_compose_validation_and_discard(void) {
    PtySession *s = start_demo(60, 14);
    ASSERT(s != NULL, "start");
    ASSERT_WAIT_FOR(s, "q quit", 5000);
    pty_send_str(s, "c");
    ASSERT_WAIT_FOR(s, "New message", 3000);
    pty_send_key(s, PTY_KEY_CTRL('o'));
    ASSERT_WAIT_FOR(s, "[body]", 3000);
    pty_send_key(s, PTY_KEY_CTRL('s'));
    const char *const error[] = {
        "To:",
        "Subject:",
        "To: is empty \xE2\x80\x94 enter a recipient.",
        "",
        NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(1, 0, 4, 60), error, 3000);
    ASSERT_WAIT_CURSOR(s, 1, 9, 3000, "back in the To field");
    pty_send_key(s, PTY_KEY_ESC);
    const char *const discarded[] = { "Discarded                                              14x60", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(13, 0, 1, 60), discarded, 3000);

    pty_send_str(s, "c");
    ASSERT_WAIT_FOR(s, "New message", 3000);
    pty_send_str(s, "zoe");                      /* no contact matches: no drop-down */
    pty_send_key(s, PTY_KEY_CTRL('s'));
    ASSERT_WAIT_FOR(s, "Labels", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_GONE(s, "Labels", 3000);
    const char *const back[] = { "To:      zoe", NULL };
    ASSERT_REGION_EQ(s, PTY_REGION(1, 0, 1, 60), back);
    pty_send_key(s, PTY_KEY_ENTER);              /* Enter in the header: on to the body */
    ASSERT_WAIT_FOR(s, "[body]", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_REGION(s, PTY_REGION(13, 0, 1, 60), discarded, 3000);
    ASSERT_WAIT_FOR(s, "Welcome to the tuikit demo.", 3000);
    pty_send_key(s, PTY_KEY_CTRL('c'));
    ASSERT_EXIT_STATUS(s, 0, 3000);
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
    TEST_CASE(test_demo_navigation_and_scroll),
    TEST_CASE(test_demo_search),
    TEST_CASE(test_demo_resize),
    TEST_CASE(test_demo_not_a_tty),
    TEST_CASE(test_demo_compose_and_send),
    TEST_CASE(test_demo_compose_validation_and_discard),
};

int main(int argc, char **argv) {
    return RUN_SUITE("pty", tests, 60, 120);
}
