/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

/*
 * PTY suite of tuikit-demo: every view and element of the demo through a
 * real terminal, each compared cell by cell -- text, position, attributes,
 * foreground and background -- in the states it has
 * (policies/testing-model.md, "A test of an element checks all of it";
 * policies/tui-interface-model.md for what each should look like and do).
 * The inventory of the elements, their states and the test that checks
 * each is docs/demo-elements.md; a new element gets a row there and a test
 * here.
 */

#include "test_helpers.h"

#include <signal.h>

int g_tests_run = 0;
int g_tests_failed = 0;

#ifndef DEMO
#error DEMO must be defined by the build
#endif

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

#define BODYHELP_ROW 7
#define BODYHELP_COL 19
#define BODYHELP_W 42

/* The palette's roles as libptytest reports them. */
static const PtyStyleKey g_legend[] = {
    { '.', PTY_ATTR_NONE, PTY_FG_DEFAULT, PTY_BG_DEFAULT },
    { 'H', PTY_ATTR_BOLD, PTY_FG_WHITE, PTY_BG_BLUE },      /* header bar */
    { 'S', PTY_ATTR_BOLD, PTY_FG_BLACK, PTY_BG_CYAN },      /* selection */
    { 'A', PTY_ATTR_BOLD, PTY_FG_CYAN, PTY_BG_DEFAULT },    /* accent */
    { 'B', PTY_ATTR_BOLD, PTY_FG_DEFAULT, PTY_BG_DEFAULT }, /* bold, no colour */
    { 'Y', PTY_ATTR_NONE, PTY_FG_YELLOW, PTY_BG_DEFAULT },  /* hints */
    { 'O', PTY_ATTR_BOLD, PTY_FG_GREEN, PTY_BG_DEFAULT },   /* status ok */
    { 'E', PTY_ATTR_BOLD, PTY_FG_RED, PTY_BG_DEFAULT },     /* error */
    { 0, 0, 0, 0 } };

/* The full screen and the full style map of the reader at rest. */
static const char *const g_reader[] = {
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
static const char *const g_reader_map[] = {
    "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH",
    "AAAAAAAAAAAAAAAAAAAAAAAAAA......................................................",
    "ABBBBBBB.................A......................................................",
    "ASSSSSSSSSSSSSSSSSSSSSSSSA......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "A........................A......................................................",
    "AAAAAAAAAAAAAAAAAAAAAAAAAA......................................................",
    "YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY.................",
    NULL };

#define WAIT_READER(s) do { \
    ASSERT_WAIT_SCREEN(s, g_reader, 5000); \
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 24, 80), g_reader_map, g_legend); \
} while (0)

/* Header bar, both panes, the list's column header, the selection, the
 * hints; no cursor. */
static void test_reader_at_rest(void) {
    PtySession *s = start_demo(80, 24);
    ASSERT(s != NULL, "start");
    WAIT_READER(s);
    int r, col;
    ASSERT_EQ_INT(pty_cursor(s, &r, &col), 0, "the cursor is hidden");
    /* q never quits; an unbound key changes nothing. */
    pty_send_str(s, "qxZ");
    pty_send_str(s, "j");
    const char *const moved[] = { "SSSSSSSSSSSSSSSSSSSSSSSS", NULL };
    ASSERT_WAIT_REGION_STYLE(s, PTY_REGION(4, 1, 1, 24), moved, g_legend, 3000);
    const char *const header[] = { " Inbox \xC2\xB7 6 messages", NULL };
    ASSERT_REGION_EQ(s, PTY_REGION(0, 0, 1, 40), header);
    /* Esc on the top level quits, status 0. */
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

/* The list's keys, reading a message and going back: focus moves from
 * pane to pane, frame and selection colours with it. */
static void test_reader_list_and_body(void) {
    PtySession *s = start_demo(80, 24);
    ASSERT(s != NULL, "start");
    WAIT_READER(s);
    pty_send_str(s, "jjj");
    pty_send_key(s, PTY_KEY_ENTER);
    const char *const body[] = {
        " Inbox · 6 messages                                                 tuikit-demo",
        "┌─ Messages ─────────────┐┌─ Budget 2027 ──────────────────────────────────────┐",
        "│Subject                 ││Line 1 of the budget.                               │",
        "│Welcome                 ││Line 2.                                             │",
        "│Árvíztűrő tükörfúrógép  ││Line 3.                                             │",
        "│Wide characters         ││Line 4.                                             │",
        "│Budget 2027             ││Line 5.                                             │",
        "│Meeting notes           ││Line 6.                                             │",
        "│Re: Budget 2027         ││Line 7.                                             │",
        "│                        ││Line 8.                                             │",
        "│                        ││Line 9.                                             │",
        "│                        ││Line 10.                                            │",
        "│                        ││Line 11.                                            │",
        "│                        ││Line 12.                                            │",
        "│                        ││Line 13.                                            │",
        "│                        ││Line 14.                                            │",
        "│                        ││Line 15.                                            │",
        "│                        ││Line 16.                                            │",
        "│                        ││Line 17.                                            │",
        "│                        ││Line 18.                                            │",
        "│                        ││Line 19.                                            │",
        "│                        ││Line 20.                                            │",
        "└────────────────────────┘└────────────────────────────────────────────────────┘",
        "↑↓ scroll  Esc back  n new  ? help",
        NULL };
    const char *const body_map[] = {
        "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH",
        "..........................AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        ".BBBBBBB..................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        ".BBBBBBBBBBBBBBBBBBBBBBBB.A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................A....................................................A",
        "..........................AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY..............................................",
        NULL };
    ASSERT_WAIT_SCREEN(s, body, 3000);
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 24, 80), body_map, g_legend);
    /* The body: 24 lines in 20 rows; a page is the view less one line. */
    pty_send_key(s, PTY_KEY_CTRL('f'));
    const char *const end[] = { "Line 5.", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(2, 27, 1, 7), end, 3000);
    pty_send_str(s, "g");
    const char *const top[] = { "Line 1 of the budget.", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(2, 27, 1, 21), top, 3000);
    pty_send_str(s, "j");
    const char *const one[] = { "Line 2.", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(2, 27, 1, 7), one, 3000);
    pty_send_str(s, "G");
    ASSERT_WAIT_REGION(s, PTY_REGION(2, 27, 1, 7), end, 3000);
    pty_send_key(s, PTY_KEY_CTRL('b'));
    ASSERT_WAIT_REGION(s, PTY_REGION(2, 27, 1, 21), top, 3000);

    /* Every way back to the list, and every way in again. */
    static const char *const back[] = { "\x1b", "q", "\t", "h", "\x1b[D" };
    static const char *const in[] = { "\r", "\t", "l", "\x1b[C", "\r" };
    const char *const list_focused[] = { "AAAAAAAAAAAAAAAAAAAAAAAAAA", NULL };
    const char *const body_focused[] = { "..........................A", NULL };
    for (int i = 0; i < 5; i++) {
        pty_send_str(s, back[i]);
        ASSERT_WAIT_REGION_STYLE(s, PTY_REGION(1, 0, 1, 26), list_focused, g_legend, 3000);
        pty_send_str(s, in[i]);
        ASSERT_WAIT_REGION_STYLE(s, PTY_REGION(1, 0, 1, 27), body_focused, g_legend, 3000);
    }
    pty_send_key(s, PTY_KEY_ESC);
    /* A letter right after Esc would arrive as Alt+letter: wait first. */
    ASSERT_WAIT_REGION_STYLE(s, PTY_REGION(1, 0, 1, 26), list_focused, g_legend, 3000);
    /* The list: G, k, g, ctrl-f, ctrl-b, the arrows. */
    static const struct { const char *keys; int row; } moves[] = {
        { "G", 8 }, { "k", 7 }, { "g", 3 }, { "\x06", 8 }, { "\x02", 3 },
        { "\x1b[B", 4 }, { "\x1b[A", 3 }, { "\x1b[F", 8 }, { "\x1b[H", 3 } };
    const char *const sel[] = { "SSSSSSSSSSSSSSSSSSSSSSSS", NULL };
    for (size_t i = 0; i < sizeof(moves) / sizeof(moves[0]); i++) {
        pty_send_str(s, moves[i].keys);
        ASSERT_WAIT_REGION_STYLE(s, PTY_REGION(moves[i].row, 1, 1, 24), sel, g_legend, 3000);
    }
    WAIT_READER(s);
    pty_send_key(s, PTY_KEY_CTRL('c'));
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

/* ? opens the help of the view it was pressed in; any key closes it. */
static void test_help(void) {
    PtySession *s = start_demo(80, 24);
    ASSERT(s != NULL, "start");
    WAIT_READER(s);
    pty_send_str(s, "?");
    const char *const help[] = {
        " Inbox · 6 messages                                                 tuikit-demo",
        "┌─ Messages ─────────────┐┌─ Welcome ──────────────────────────────────────────┐",
        "│Subject                 ││Welcome to the tuikit demo.                         │",
        "│Welcome                 ││                                                    │",
        "│Árvíztűrő tükörfúrógép  ││The arrow keys or j and k move, Enter reads, /      │",
        "│Wide characters   ┌─ Message list shortcuts ──────────────┐ys and Esc quits.  │",
        "│Budget 2027       │ j, k, ↓, ↑        move one row        │                   │",
        "│Meeting notes     │ ctrl-f, ctrl-b    move one page       │                   │",
        "│Re: Budget 2027   │ g, G              first, last row     │                   │",
        "│                  │ Enter, Tab, l, →  read the message    │                   │",
        "│                  │ /                 filter the subjects │                   │",
        "│                  │ c                 clear the filter    │                   │",
        "│                  │ n                 write a new message │                   │",
        "│                  │ ?                 this help           │                   │",
        "│                  │ Esc               quit                │                   │",
        "│                  │ ctrl-c            quit at once        │                   │",
        "│                  │ ctrl-z            suspend             │                   │",
        "│                  └───────────────────────────────────────┘                   │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "└────────────────────────┘└────────────────────────────────────────────────────┘",
        "↑↓ move  Enter read  / filter  c clear  n new  ? help  Esc quit",
        NULL };
    const char *const help_map[] = {
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "A.BBBBBBBBBB............................A",
        "A.BBBBBBBBBBBBBB........................A",
        "A.BBBB..................................A",
        "A.BBBBBBBBBBBBBBBB......................A",
        "A.B.....................................A",
        "A.B.....................................A",
        "A.B.....................................A",
        "A.B.....................................A",
        "A.BBB...................................A",
        "A.BBBBBB................................A",
        "A.BBBBBB................................A",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        NULL };
    ASSERT_WAIT_SCREEN(s, help, 3000);
    ASSERT_REGION_STYLE(s, PTY_REGION(5, 19, 13, 41), help_map, g_legend);
    pty_send_str(s, "j");                        /* it fits: j closes it too */
    WAIT_READER(s);
    pty_send_str(s, "jjj");
    pty_send_key(s, PTY_KEY_ENTER);
    pty_send_str(s, "?");
    const char *const bodyhelp[] = {
        " Inbox · 6 messages                                                 tuikit-demo",
        "┌─ Messages ─────────────┐┌─ Budget 2027 ──────────────────────────────────────┐",
        "│Subject                 ││Line 1 of the budget.                               │",
        "│Welcome                 ││Line 2.                                             │",
        "│Árvíztűrő tükörfúrógép  ││Line 3.                                             │",
        "│Wide characters         ││Line 4.                                             │",
        "│Budget 2027             ││Line 5.                                             │",
        "│Meeting notes     ┌─ Message shortcuts ────────────────────┐                  │",
        "│Re: Budget 2027   │ j, k, ↓, ↑         scroll one line     │                  │",
        "│                  │ ctrl-f, ctrl-b     scroll one page     │                  │",
        "│                  │ g, G               top, end            │                  │",
        "│                  │ Esc, q, Tab, h, ←  back to the list    │                  │",
        "│                  │ n                  write a new message │                  │",
        "│                  │ ?                  this help           │                  │",
        "│                  │ ctrl-c             quit at once        │                  │",
        "│                  └────────────────────────────────────────┘                  │",
        "│                        ││Line 15.                                            │",
        "│                        ││Line 16.                                            │",
        "│                        ││Line 17.                                            │",
        "│                        ││Line 18.                                            │",
        "│                        ││Line 19.                                            │",
        "│                        ││Line 20.                                            │",
        "└────────────────────────┘└────────────────────────────────────────────────────┘",
        "↑↓ scroll  Esc back  n new  ? help",
        NULL };
    const char *const bodyhelp_map[] = {
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "A.BBBBBBBBBB.............................A",
        "A.BBBBBBBBBBBBBB.........................A",
        "A.BBBB...................................A",
        "A.BBBBBBBBBBBBBBBBB......................A",
        "A.B......................................A",
        "A.B......................................A",
        "A.BBBBBB.................................A",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        NULL };
    ASSERT_WAIT_SCREEN(s, bodyhelp, 3000);
    ASSERT_REGION_STYLE(s, PTY_REGION(BODYHELP_ROW, BODYHELP_COL, 9, BODYHELP_W), bodyhelp_map, g_legend);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_GONE(s, "Message shortcuts", 3000);
    ASSERT_WAIT_FOR(s, "\xE2\x86\x91\xE2\x86\x93 scroll  Esc back", 3000);
    pty_send_key(s, PTY_KEY_CTRL('c'));
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

/* / filters the subjects, the header names the filter, c clears it; an
 * empty result says so and names the key that fills it. */
static void test_filter(void) {
    PtySession *s = start_demo(80, 24);
    ASSERT(s != NULL, "start");
    WAIT_READER(s);
    pty_send_str(s, "/Budget");
    const char *const prompt[] = {
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
        "│                      ┌─ Filter the subjects ──────────┐                      │",
        "│                      │ Subject contains:              │                      │",
        "│                      │ Budget                         │                      │",
        "│                      └────────────────────────────────┘                      │",
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
    const char *const prompt_map[] = {
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        "A................................A",
        "A................................A",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
        NULL };
    ASSERT_WAIT_SCREEN(s, prompt, 3000);
    ASSERT_REGION_STYLE(s, PTY_REGION(10, 23, 4, 34), prompt_map, g_legend);
    ASSERT_WAIT_CURSOR(s, 12, 31, 3000, "the cursor in the input line");
    pty_send_key(s, PTY_KEY_ENTER);
    const char *const filtered[] = {
        " Inbox · 2 of 6 · filter: Budget                                    tuikit-demo",
        "┌─ Messages ─────────────┐┌─ Budget 2027 ──────────────────────────────────────┐",
        "│Subject                 ││Line 1 of the budget.                               │",
        "│Budget 2027             ││Line 2.                                             │",
        "│Re: Budget 2027         ││Line 3.                                             │",
        "│                        ││Line 4.                                             │",
        "│                        ││Line 5.                                             │",
        "│                        ││Line 6.                                             │",
        "│                        ││Line 7.                                             │",
        "│                        ││Line 8.                                             │",
        "│                        ││Line 9.                                             │",
        "│                        ││Line 10.                                            │",
        "│                        ││Line 11.                                            │",
        "│                        ││Line 12.                                            │",
        "│                        ││Line 13.                                            │",
        "│                        ││Line 14.                                            │",
        "│                        ││Line 15.                                            │",
        "│                        ││Line 16.                                            │",
        "│                        ││Line 17.                                            │",
        "│                        ││Line 18.                                            │",
        "│                        ││Line 19.                                            │",
        "│                        ││Line 20.                                            │",
        "└────────────────────────┘└────────────────────────────────────────────────────┘",
        "↑↓ move  Enter read  / filter  c clear  n new  ? help  Esc quit",
        NULL };
    ASSERT_WAIT_SCREEN(s, filtered, 3000);
    const char *const filtered_header[] = {
        "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 1, 80), filtered_header, g_legend);
    const char *const sel[] = { "SSSSSSSSSSSSSSSSSSSSSSSS", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(3, 1, 1, 24), sel, g_legend);
    /* A cancelled prompt leaves the filter as it was. */
    pty_send_str(s, "/x");
    ASSERT_WAIT_FOR(s, "Budgetx", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_SCREEN(s, filtered, 3000);
    pty_send_str(s, "c");
    const char *const cleared[] = { " Inbox \xC2\xB7 6 messages", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(0, 0, 1, 40), cleared, 3000);
    const char *const kept[] = { "SSSSSSSSSSSSSSSSSSSSSSSS", NULL };   /* still on Budget 2027 */
    ASSERT_REGION_STYLE(s, PTY_REGION(6, 1, 1, 24), kept, g_legend);
    pty_send_str(s, "c");                        /* no filter: c changes nothing */
    pty_send_str(s, "/");
    ASSERT_WAIT_FOR(s, "Subject contains:", 3000);
    /* The prompt starts from the filter in force (none): type a new one. */
    pty_send_str(s, "zzz");
    pty_send_key(s, PTY_KEY_ENTER);
    const char *const empty[] = {
        " Inbox · 0 of 6 · filter: zzz                                       tuikit-demo",
        "┌─ Messages ─────────────┐┌────────────────────────────────────────────────────┐",
        "│Subject                 ││                                                    │",
        "│Nothing matches.        ││                                                    │",
        "│c clears the filter.    ││                                                    │",
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
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "│                        ││                                                    │",
        "└────────────────────────┘└────────────────────────────────────────────────────┘",
        "↑↓ move  Enter read  / filter  c clear  n new  ? help  Esc quit",
        NULL };
    ASSERT_WAIT_SCREEN(s, empty, 3000);
    const char *const empty_map[] = { "........................", "YYYYYYYYYYYYYYYYYYYY....", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(3, 1, 2, 24), empty_map, g_legend);
    pty_send_key(s, PTY_KEY_ENTER);              /* nothing to read */
    pty_send_str(s, "jG");
    ASSERT_REGION_EQ(s, PTY_REGION(0, 0, 24, 80), empty);
    pty_send_str(s, "c");
    WAIT_READER(s);
    pty_send_key(s, PTY_KEY_CTRL('c'));
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

/* Below 80x24 a notice instead of the layout; back at 80x24 the view as it
 * was; bigger, the layout grows. */
static void test_too_small_and_resize(void) {
    PtySession *s = start_demo(80, 24);
    ASSERT(s != NULL, "start");
    WAIT_READER(s);
    ASSERT_EQ_INT(pty_resize(s, 79, 24), 0, "resize");
    const char *const small[] = {
        "", "", "", "", "", "", "", "", "", "", "",
        "                              Terminal too small",
        "                The terminal is 79x24; tuikit-demo needs 80x24.",
        "                                  Esc quits.",
        "", "", "", "", "", "", "", "", "", "", NULL };
    ASSERT_WAIT_SCREEN(s, small, 3000);
    const char *const small_map[] = {
        "..............................EEEEEEEEEEEEEEEEEE",
        "...............................................................................",
        "..................................YYYYYYYYYY", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(11, 0, 3, 79), small_map, g_legend);
    pty_send_str(s, "n?/j");                     /* ignored while too small */
    ASSERT_EQ_INT(pty_resize(s, 80, 23), 0, "too short as well");
    ASSERT_WAIT_FOR(s, "The terminal is 80x23", 3000);
    ASSERT_EQ_INT(pty_resize(s, 80, 24), 0, "back");
    WAIT_READER(s);
    ASSERT_EQ_INT(pty_resize(s, 90, 26), 0, "bigger");
    const char *const big_header[] = {
        " Inbox \xC2\xB7 6 messages                                                           tuikit-demo", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(0, 0, 1, 90), big_header, 3000);
    const char *const big[] = { "┌─ Messages ─────────────────┐", NULL };   /* a third of 90 */
    ASSERT_WAIT_REGION(s, PTY_REGION(1, 0, 1, 30), big, 3000);
    const char *const status[] = { "\xE2\x86\x91\xE2\x86\x93 move  Enter read", NULL };
    ASSERT_REGION_EQ(s, PTY_REGION(25, 0, 1, 20), status);
    ASSERT_EQ_INT(pty_resize(s, 60, 20), 0, "small again");
    ASSERT_WAIT_FOR(s, "Terminal too small", 3000);
    pty_send_key(s, PTY_KEY_ESC);                /* Esc quits from the notice */
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

/* The compose view from n to the sent message: completion, fields, the
 * body editor, the labels, the list and the status after sending. */
static void test_compose_and_send(void) {
    PtySession *s = start_demo(80, 24);
    ASSERT(s != NULL, "start");
    WAIT_READER(s);
    pty_send_str(s, "n");
    const char *const fresh[] = {
        " New message                                                        tuikit-demo",
        "To:",
        "Subject:",
        "",
        "",
        "",
        "",
        "────────────────────────────────────────────────────────────────────────────────",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "Tab next field  ctrl-s send  Esc discard",
        NULL };
    ASSERT_WAIT_SCREEN(s, fresh, 3000);
    ASSERT_WAIT_CURSOR(s, 1, 9, 3000, "the cursor in To");
    const char *const fresh_map[] = {
        "HHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHHH",
        "AAA.....", "........", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 3, 80), fresh_map, g_legend);
    const char *const hints_map[] = { "YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY.", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(23, 0, 1, 41), hints_map, g_legend);

    pty_send_str(s, "al");
    const char *const dropdown[] = {
        " New message                                                        tuikit-demo",
        "To:      al",
        "         ▶ alice@example.com  Alice Smith",
        "         ▶ albert@example.org  Albert",
        "Subject:",
        "",
        "",
        "────────────────────────────────────────────────────────────────────────────────",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "Tab next field  ctrl-s send  Esc discard",
        NULL };
    ASSERT_WAIT_SCREEN(s, dropdown, 3000);
    const char *const drop_map[] = {
        ".........SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS.",
        "..........................................", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(2, 0, 2, 42), drop_map, g_legend);
    pty_send_key(s, PTY_KEY_DOWN);
    const char *const drop_map2[] = {
        "..........................................",
        ".........SSSSSSSSSSSSSSSSSSSSSSSSSSSS.....", NULL };
    ASSERT_WAIT_REGION_STYLE(s, PTY_REGION(2, 0, 2, 42), drop_map2, g_legend, 3000);
    pty_send_key(s, PTY_KEY_TAB);
    const char *const accepted[] = {
        " New message                                                        tuikit-demo",
        "To:      albert@example.org",
        "Subject:",
        "",
        "",
        "",
        "",
        "────────────────────────────────────────────────────────────────────────────────",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "Tab next field  ctrl-s send  Esc discard",
        NULL };
    ASSERT_WAIT_SCREEN(s, accepted, 3000);
    pty_send_str(s, "; bo");
    const char *const second[] = {
        " New message                                                        tuikit-demo",
        "To:      albert@example.org; bo",
        "         ▶ bob@example.net  Bob",
        "Subject:",
        "",
        "",
        "",
        "────────────────────────────────────────────────────────────────────────────────",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "Tab next field  ctrl-s send  Esc discard",
        NULL };
    ASSERT_WAIT_SCREEN(s, second, 3000);
    pty_send_key(s, PTY_KEY_ENTER);              /* Enter accepts the candidate */
    ASSERT_WAIT_CURSOR(s, 1, 44, 3000, "after the second address");
    pty_send_key(s, PTY_KEY_TAB);
    ASSERT_WAIT_CURSOR(s, 2, 9, 3000, "Tab: the Subject field");
    const char *const subj_focus[] = { "...", "AAAAAAAA", NULL };
    ASSERT_WAIT_REGION_STYLE(s, PTY_REGION(1, 0, 2, 8), subj_focus, g_legend, 3000);
    pty_send_str(s, "Budget");
    pty_send_key(s, PTY_KEY_TAB);
    ASSERT_WAIT_CURSOR(s, 8, 0, 3000, "Tab after the last field: the body");
    const char *const no_focus[] = { "...", "........", NULL };   /* no label is focused */
    ASSERT_WAIT_REGION_STYLE(s, PTY_REGION(1, 0, 2, 8), no_focus, g_legend, 3000);
    pty_send_str(s, "Line one\rLine two\tend");
    const char *const written[] = {
        " New message                                                        tuikit-demo",
        "To:      albert@example.org; bob@example.net",
        "Subject: Budget",
        "",
        "",
        "",
        "",
        "────────────────────────────────────────────────────────────────────────────────",
        "Line one",
        "Line two        end",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "shift-Tab fields  ctrl-s send  Esc discard  line 2, col 20",
        NULL };
    ASSERT_WAIT_SCREEN(s, written, 3000);
    ASSERT_WAIT_CURSOR(s, 9, 19, 3000, "after end");
    const char *const body_hints[] = {           /* the hints, line and column included */
        "YYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYYY.", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(23, 0, 1, 59), body_hints, g_legend);

    pty_send_key(s, PTY_KEY_CTRL('s'));
    const char *const labels[] = {
        " New message                                                        tuikit-demo",
        "To:      albert@example.org; bob@example.net",
        "Subject: Budget",
        "",
        "",
        "",
        "",
        "────────────────────────────────────────────────────────────────────────────────",
        "Line one",
        "Line two        end            ┌─ Labels ───────┐",
        "                               │ [ ] Work       │",
        "                               │ [ ] Personal   │",
        "                               │ [ ] Urgent     │",
        "                               └────────────────┘",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "shift-Tab fields  ctrl-s send  Esc discard  line 2, col 20",
        NULL };
    const char *const labels_map[] = {
        "AAAAAAAAAAAAAAAAAA",
        "A.SSSSSSSSSSSSSS.A",
        "A................A",
        "A................A",
        "AAAAAAAAAAAAAAAAAA",
        NULL };
    ASSERT_WAIT_SCREEN(s, labels, 3000);
    ASSERT_REGION_STYLE(s, PTY_REGION(9, 31, 5, 18), labels_map, g_legend);
    pty_send_key(s, PTY_KEY_DOWN);
    pty_send_str(s, " ");
    const char *const ticked[] = {
        " New message                                                        tuikit-demo",
        "To:      albert@example.org; bob@example.net",
        "Subject: Budget",
        "",
        "",
        "",
        "",
        "────────────────────────────────────────────────────────────────────────────────",
        "Line one",
        "Line two        end            ┌─ Labels ───────┐",
        "                               │ [ ] Work       │",
        "                               │ [x] Personal   │",
        "                               │ [ ] Urgent     │",
        "                               └────────────────┘",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "",
        "shift-Tab fields  ctrl-s send  Esc discard  line 2, col 20",
        NULL };
    const char *const ticked_map[] = {
        "AAAAAAAAAAAAAAAAAA",
        "A................A",
        "A.SSSSSSSSSSSSSS.A",
        "A................A",
        "AAAAAAAAAAAAAAAAAA",
        NULL };
    ASSERT_WAIT_SCREEN(s, ticked, 3000);
    ASSERT_REGION_STYLE(s, PTY_REGION(9, 31, 5, 18), ticked_map, g_legend);
    pty_send_key(s, PTY_KEY_ENTER);
    const char *const sent[] = {
        " Inbox · 7 messages                                                 tuikit-demo",
        "┌─ Messages ─────────────┐┌─ Budget ───────────────────────────────────────────┐",
        "│Subject                 ││To: albert@example.org; bob@example.net             │",
        "│Welcome                 ││Labels: Personal                                    │",
        "│Árvíztűrő tükörfúrógép  ││                                                    │",
        "│Wide characters         ││Line one                                            │",
        "│Budget 2027             ││Line two        end                                 │",
        "│Meeting notes           ││                                                    │",
        "│Re: Budget 2027         ││                                                    │",
        "│Budget                  ││                                                    │",
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
        "Sent to albert@example.org; bob@example.net.",
        NULL };
    ASSERT_WAIT_SCREEN(s, sent, 3000);
    const char *const sent_map[] = {
        "SSSSSSSSSSSSSSSSSSSSSSSS", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(9, 1, 1, 24), sent_map, g_legend);
    const char *const ok_map[] = { "OOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOOO.", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(23, 0, 1, 45), ok_map, g_legend);
    /* The message lasts until the next key; then the hints are back. */
    pty_send_str(s, "x");
    ASSERT_WAIT_FOR(s, "Esc quit", 3000);
    pty_send_key(s, PTY_KEY_CTRL('c'));
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

/* Sending without a recipient points at the field, in the error style;
 * Esc discards at once when nothing was typed, and asks when something
 * was; a cancelled label list goes back to the form. */
static void test_compose_refusals_and_discard(void) {
    PtySession *s = start_demo(80, 24);
    ASSERT(s != NULL, "start");
    WAIT_READER(s);
    pty_send_str(s, "n");
    ASSERT_WAIT_FOR(s, "New message", 3000);
    pty_send_key(s, PTY_KEY_TAB);
    pty_send_key(s, PTY_KEY_TAB);
    ASSERT_WAIT_CURSOR(s, 8, 0, 3000, "in the body");
    pty_send_key(s, PTY_KEY_CTRL('s'));
    const char *const error[] = {
        "To:", "Subject:", "To: is empty \xE2\x80\x94 enter a recipient.", "", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(1, 0, 4, 80), error, 3000);
    const char *const error_map[] = {
        "AAA", "........", "EEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEEE.", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(1, 0, 3, 34), error_map, g_legend);
    ASSERT_WAIT_CURSOR(s, 1, 9, 3000, "back in To");
    pty_send_key(s, PTY_KEY_ESC);                /* nothing typed: no question */
    const char *const discarded[] = { "Discarded.", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(23, 0, 1, 80), discarded, 3000);
    const char *const plain_map[] = { "...........", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(23, 0, 1, 11), plain_map, g_legend);

    pty_send_str(s, "n");
    ASSERT_WAIT_FOR(s, "New message", 3000);
    pty_send_str(s, "zoe");                      /* no contact matches: no drop-down */
    pty_send_key(s, PTY_KEY_CTRL('s'));
    ASSERT_WAIT_FOR(s, "Labels", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_GONE(s, "Labels", 3000);
    const char *const kept[] = { "To:      zoe", NULL };
    ASSERT_REGION_EQ(s, PTY_REGION(1, 0, 1, 80), kept);
    pty_send_key(s, PTY_KEY_ESC);
    const char *const ask[] = {
        "┌─ Discard the message ──────┐",
        "│ Throw away what was typed? │",
        "│                            │",
        "│      [ Yes ]  [ No ]       │",
        "└────────────────────────────┘",
        NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(9, 25, 5, 30), ask, 3000);
    const char *const ask_map[] = {
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", "A............................A",
        "A............................A", "A...............SSSSSS.......A",
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(9, 25, 5, 30), ask_map, g_legend);
    pty_send_str(s, "n");                        /* No: back, nothing lost */
    ASSERT_WAIT_GONE(s, "Throw away", 3000);
    ASSERT_REGION_EQ(s, PTY_REGION(1, 0, 1, 80), kept);
    pty_send_key(s, PTY_KEY_TAB);
    pty_send_key(s, PTY_KEY_TAB);
    pty_send_key(s, PTY_KEY_ESC);                /* from the body, too */
    ASSERT_WAIT_FOR(s, "Throw away", 3000);
    pty_send_str(s, "y");
    ASSERT_WAIT_REGION(s, PTY_REGION(23, 0, 1, 80), discarded, 3000);
    const char *const six[] = { " Inbox \xC2\xB7 6 messages", NULL };
    ASSERT_REGION_EQ(s, PTY_REGION(0, 0, 1, 40), six);
    pty_send_key(s, PTY_KEY_CTRL('c'));
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

/* The body is a full-screen editor: it scrolls down to the cursor and
 * sideways along a long line, the hints show the line and column and say
 * when the limit is reached; shift-Tab goes back to the fields. */
static void test_compose_body_editor(void) {
    PtySession *s = start_demo(80, 24);
    ASSERT(s != NULL, "start");
    WAIT_READER(s);
    pty_send_str(s, "n");
    ASSERT_WAIT_FOR(s, "New message", 3000);
    pty_send_str(s, "\t\t");
    for (int i = 1; i <= 18; i++) {
        char line[16];
        snprintf(line, sizeof(line), i < 18 ? "L%d\r" : "L%d", i);
        pty_send_str(s, line);
    }
    /* 18 lines in 15 rows: L4 at the top, the cursor on the last row. */
    const char *const bottom[] = {
        "L4", "L5", "L6", "L7", "L8", "L9", "L10", "L11", "L12", "L13", "L14", "L15",
        "L16", "L17", "L18",
        "shift-Tab fields  ctrl-s send  Esc discard  line 18, col 4", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(8, 0, 16, 80), bottom, 3000);
    ASSERT_WAIT_CURSOR(s, 22, 3, 3000, "after L18");
    pty_send_str(s, "\x1b[1;5H");               /* ctrl-Home */
    const char *const top[] = { "L1", "L2", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(8, 0, 2, 80), top, 3000);
    ASSERT_WAIT_CURSOR(s, 8, 0, 3000, "at the start");
    pty_send_str(s, "\x1b[1;5F");               /* ctrl-End, then a long line */
    for (int i = 0; i < 90; i++) pty_send_str(s, "x");
    ASSERT_WAIT_CURSOR(s, 22, 79, 3000, "the view follows the cursor sideways");
    const char *const hint[] = { "shift-Tab fields  ctrl-s send  Esc discard  line 18, col 94", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(23, 0, 1, 80), hint, 3000);
    pty_send_key(s, PTY_KEY_SHIFT_TAB);
    ASSERT_WAIT_CURSOR(s, 2, 9, 3000, "shift-Tab: back to the field it left, Subject");
    pty_send_key(s, PTY_KEY_ENTER);              /* Enter in the fields: the body */
    ASSERT_WAIT_CURSOR(s, 22, 79, 3000, "back in the body where it was");
    /* The text is 1024 bytes at most: the hints say when it is full. */
    char fill[1100];
    memset(fill, 'y', sizeof(fill) - 1);
    fill[sizeof(fill) - 1] = '\0';
    pty_send_str(s, "\x1b[200~");
    pty_send_str(s, fill);
    pty_send_str(s, "\x1b[201~");
    ASSERT_WAIT_FOR(s, "limit reached", 3000);
    const char *const lim_map[] = { "YYYYYYYYYYYYY", NULL };
    char row[160];
    pty_row_text(s, 23, row, sizeof(row));
    const char *at = strstr(row, "limit reached");
    ASSERT(at != NULL, "the limit is on the hint line");
    ASSERT_REGION_STYLE(s, PTY_REGION(23, (int)(at - row), 1, 13), lim_map, g_legend);
    pty_send_key(s, PTY_KEY_CTRL('c'));
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

/* ctrl-z gives the terminal back and stops the program; fg brings the
 * screen back as it was.  Run under sh -m, which does job control. */
static void test_suspend(void) {
    PtySession *s = pty_open(80, 24);
    ASSERT(s != NULL, "pty_open");
    const char *argv[] = { "/bin/sh", "-c",
        "set -m; \"$0\"; echo \"stopped: $?\"; read x; fg >/dev/null; echo \"ended: $?\"",
        DEMO, NULL };
    ASSERT(pty_run(s, argv) == 0, "start under sh -m");
    WAIT_READER(s);
    pty_send_str(s, "j");
    pty_send_key(s, PTY_KEY_CTRL('z'));
    ASSERT_WAIT_FOR(s, "stopped: 148", 3000);   /* 128 + SIGTSTP */
    pty_send_key(s, PTY_KEY_ENTER);
    const char *const sel[] = { "SSSSSSSSSSSSSSSSSSSSSSSS", NULL };
    ASSERT_WAIT_REGION_STYLE(s, PTY_REGION(4, 1, 1, 24), sel, g_legend, 3000);
    ASSERT_WAIT_FOR(s, "Esc quit", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_FOR(s, "ended: 0", 3000);
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);
}

/* ctrl-c quits at once from a modal and from the compose view. */
static void test_ctrl_c_everywhere(void) {
    static const char *const before[] = { "?", "/", "n", "n\t\t" };
    static const char *const shown[] = {
        "Message list shortcuts", "Subject contains:", "Tab next field", "line 1, col 1" };
    for (int i = 0; i < 4; i++) {
        PtySession *s = start_demo(80, 24);
        ASSERT(s != NULL, "start");
        WAIT_READER(s);
        pty_send_str(s, before[i]);
        ASSERT_WAIT_FOR(s, shown[i], 3000);
        pty_send_key(s, PTY_KEY_CTRL('c'));
        ASSERT_EXIT_STATUS(s, 0, 3000);
        pty_close(s);
    }
}

/* Not a terminal on standard input: the plain listing (email-cli's
 * non-TTY fallback). */
static void test_not_a_tty(void) {
    PtySession *s = pty_open(40, 8);
    ASSERT(s != NULL, "pty_open");
    const char *argv[] = { "/bin/sh", "-c", "exec \"$0\" </dev/null", DEMO, NULL };
    ASSERT(pty_run(s, argv) == 0, "start");
    ASSERT_EXIT_STATUS(s, 0, 5000);
    const char *const expect[] = {
        "1. Welcome", "2. \xC3\x81rv\xC3\xADzt\xC5\xB1r\xC5\x91 t\xC3\xBCk\xC3\xB6rf\xC3\xBAr\xC3\xB3g\xC3\xA9p",
        "3. Wide characters", "4. Budget 2027", "5. Meeting notes", "6. Re: Budget 2027", "", "", NULL };
    ASSERT_SCREEN_EQ(s, expect);
    pty_close(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_reader_at_rest),
    TEST_CASE(test_reader_list_and_body),
    TEST_CASE(test_help),
    TEST_CASE(test_filter),
    TEST_CASE(test_too_small_and_resize),
    TEST_CASE(test_compose_and_send),
    TEST_CASE(test_compose_refusals_and_discard),
    TEST_CASE(test_compose_body_editor),
    TEST_CASE(test_suspend),
    TEST_CASE(test_ctrl_c_everywhere),
    TEST_CASE(test_not_a_tty),
};

int main(int argc, char **argv) {
    return RUN_SUITE("demo", tests, 150, 240);
}
