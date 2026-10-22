/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-18 */
/* PTY suite of the keyboard policy: the action menu, the confirmation, the help made from the table. */

/*
 * PTY suite of the keyboard policy of policies/tui-interface-model.md,
 * "Actions that change state: the action menu", through a real terminal and
 * a program (tests/pty/keys_fixture.c) whose every key is one tui_keyentry
 * table: ':' opens the menu that lists the actions with their direct keys; a
 * serious action asks a confirmation that names the row, No the default, and
 * Enter on it does nothing; the help is made from the table and matches what
 * the keys do; the keys of the reserved harmless set change nothing; q goes
 * back and never quits, Esc on the top level and ctrl-c quit.  Text, position
 * and style (colours included) are compared cell by cell.
 */

#include "test_helpers.h"

int g_tests_run = 0;
int g_tests_failed = 0;

#ifndef KEYS_FIXTURE
#error KEYS_FIXTURE must be defined by the build
#endif

#define ROWS 20
#define COLS 50

static PtySession *start(void) {
    PtySession *s = pty_open(COLS, ROWS);
    if (!s) return NULL;
    const char *argv[] = { KEYS_FIXTURE, NULL };
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
    { 'S', PTY_ATTR_BOLD | PTY_ATTR_REVERSE, 0, 0 },         /* the fixture's own selection, a button */
    { 'A', PTY_ATTR_BOLD, PTY_FG_CYAN, PTY_BG_DEFAULT },     /* a modal's frame */
    { 'B', PTY_ATTR_NONE, 97, PTY_COLOR_IDX(17) },           /* the selected menu row */
    { 'M', PTY_ATTR_BOLD, 97, PTY_COLOR_IDX(17) },           /* its '>' */
    { 'K', PTY_ATTR_BOLD, PTY_FG_DEFAULT, PTY_BG_DEFAULT },  /* a key in the help */
    { 0, 0, 0, 0 } };

static const char *const g_list[] = {
    " Keys · list",
    "done: nothing",
    "",
    "> Alpha",
    "  Bravo",
    "  Charlie",
    NULL };

#define WAIT_LIST(s) do { \
    ASSERT_WAIT_SCREEN(s, g_list, 5000); \
} while (0)

#define WAIT_DONE(s, what) do { \
    const char *const _d[] = { what, NULL }; \
    ASSERT_WAIT_REGION(s, PTY_REGION(1, 0, 1, (int)sizeof(what) - 1), _d, 3000); \
} while (0)

#define NOT_DONE(s) do { \
    const char *const _d[] = { "done: nothing", NULL }; \
    ASSERT_REGION_EQ(s, PTY_REGION(1, 0, 1, 13), _d); \
} while (0)

/* The list at rest: the header, what was done, the rows; the colours. */
static void test_list_at_rest(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    const char *const map[] = {
        "HHHHHHHHHHHH",
        "OOOOOOOOOOOOO",
        "SSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS" };
    const char *const m0[] = { map[0], NULL }, *const m1[] = { map[1], NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(0, 0, 1, 12), m0, g_legend);
    ASSERT_REGION_STYLE(s, PTY_REGION(1, 0, 1, 13), m1, g_legend);
    const char *const sel[] = { map[2], NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(3, 0, 1, 50), sel, g_legend);
    pty_close(s);
}

static const char *const g_menu[] = {
    " Keys · list",
    "done: nothing",
    "",
    "> Alpha",
    "  Bravo",
    "  Charlie",
    "  ┌─ Actions ──────────────────────────────────┐",
    "  │ > s  *  Star - toggle the star             │",
    "  │   u  !  Junk                               │",
    "  │   d  d  Delete                             │",
    "  │   t     Restart service - starts a process │",
    "  │   r     Rotate secret                      │",
    "  └────────────────────────────────────────────┘",
    NULL };

/* ':' lists every state-changing action of the view, the direct key beside it. */
static void test_menu_lists_actions_and_cancels(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    pty_send_str(s, ":");
    ASSERT_WAIT_SCREEN(s, g_menu, 3000);
    const char *const row4[] = { "A.MBMBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB.A", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(7, 2, 1, 46), row4, g_legend);
    const char *const row5[] = { "A...A........................................A", NULL };
    ASSERT_REGION_STYLE(s, PTY_REGION(8, 2, 1, 46), row5, g_legend);
    /* Esc cancels: nothing was done. */
    pty_send_key(s, PTY_KEY_ESC);
    WAIT_LIST(s);
    /* Keys the menu does not know change nothing. */
    pty_send_str(s, ":");
    ASSERT_WAIT_SCREEN(s, g_menu, 3000);
    pty_send_str(s, "xZ");
    ASSERT_WAIT_SCREEN(s, g_menu, 3000);
    pty_send_str(s, "\x1b");
    WAIT_LIST(s);
    pty_close(s);
}

/* A routine action is run from the menu at once, with no question. */
static void test_menu_runs_a_routine_action(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    pty_send_str(s, ":\r");
    WAIT_DONE(s, "done: Star Alpha");
    ASSERT_WAIT_GONE(s, "Actions", 3000);
    pty_send_str(s, "j:j\r");                               /* Junk on Bravo */
    WAIT_DONE(s, "done: Junk Bravo");
    pty_send_str(s, ":jjjj\r");                             /* Rotate secret: rare, menu only */
    WAIT_DONE(s, "done: Rotate secret Bravo");
    pty_close(s);
}

static const char *const g_confirm[] = {
    " Keys · list",
    "done: nothing",
    "",
    "> Alpha",
    "  Bravo",
    "  Charlie",
    "",
    "      ┌─ Delete ───────────────────────────┐",
    "      │ Delete Alpha? It cannot be undone. │",
    "      │                                    │",
    "      │          [ Yes ]  [ No ]           │",
    "      └────────────────────────────────────┘",
    NULL };

/* A serious action from the menu: the confirmation names the target, No is
 * the default, and Enter on the default never performs it. */
static void test_menu_serious_action_confirms(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    pty_send_str(s, ":jj\r");                               /* Delete */
    ASSERT_WAIT_SCREEN(s, g_confirm, 3000);
    ASSERT(pty_screen_contains(s, "Delete Alpha? It cannot be undone."), "the target is named");
    ASSERT(pty_screen_contains(s, "[ Yes ]  [ No ]"), "Yes and No");
    char d[160];
    int r = 0, c = 0;
    for (r = 0; r < ROWS; r++) {
        char row[256];
        pty_row_text(s, r, row, sizeof row);
        if (strstr(row, "[ Yes ]")) break;
    }
    ASSERT(r < ROWS, "the buttons are on a row");
    for (c = 0; c < COLS; c++)
        if (strcmp(pty_cell_text(s, r, c), "N") == 0) break;
    pty_cell_describe(s, r, c, d, sizeof d);
    ASSERT(strstr(d, "reverse") != NULL, "No is the focused button");
    pty_cell_describe(s, r, c - 8, d, sizeof d);
    ASSERT(strstr(d, "reverse") == NULL, "Yes is not");
    pty_send_str(s, "\r");                                  /* Enter on the default */
    ASSERT_WAIT_GONE(s, "Delete Alpha?", 3000);
    NOT_DONE(s);
    /* Esc and ctrl-z-free refusals: Esc, n. */
    pty_send_str(s, ":jj\r");
    pty_wait_for(s, "Delete Alpha?", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_GONE(s, "Delete Alpha?", 3000);
    NOT_DONE(s);
    pty_send_str(s, ":jj\r");
    pty_wait_for(s, "Delete Alpha?", 3000);
    pty_send_str(s, "n");
    ASSERT_WAIT_GONE(s, "Delete Alpha?", 3000);
    NOT_DONE(s);
    /* Moving to Yes and Enter performs. */
    pty_send_str(s, ":jj\r");
    pty_wait_for(s, "Delete Alpha?", 3000);
    pty_send_str(s, "\t\r");
    WAIT_DONE(s, "done: Delete Alpha");
    pty_close(s);
}

/* A menu-only serious action (a restart): the question names the row. */
static void test_menu_only_serious_action(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    pty_send_str(s, "j:jjj\r");                             /* Restart service on Bravo */
    pty_wait_for(s, "Restart the service of Bravo?", 3000);
    pty_send_str(s, "\r");
    ASSERT_WAIT_GONE(s, "Restart the service", 3000);
    NOT_DONE(s);
    pty_send_str(s, ":jjj\r");
    pty_wait_for(s, "Restart the service of Bravo?", 3000);
    pty_send_str(s, "y");
    WAIT_DONE(s, "done: Restart service Bravo");
    pty_close(s);
}

/* The direct keys: a routine one at once, a serious one with the question. */
static void test_direct_keys(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    pty_send_str(s, "*");
    WAIT_DONE(s, "done: Star Alpha");
    pty_send_str(s, "j!");
    WAIT_DONE(s, "done: Junk Bravo");
    pty_send_str(s, "d");
    pty_wait_for(s, "Delete Bravo? It cannot be undone.", 3000);
    pty_send_str(s, "\r");                                  /* the default is No */
    ASSERT_WAIT_GONE(s, "Delete Bravo?", 3000);
    WAIT_DONE(s, "done: Junk Bravo");
    pty_send_str(s, "d");
    pty_wait_for(s, "Delete Bravo?", 3000);
    pty_send_str(s, "y");
    WAIT_DONE(s, "done: Delete Bravo");
    pty_close(s);
}

/* The keys of the reserved harmless set never change state; unbound keys
 * change nothing and show nothing; r reloads; q on the top level does not quit. */
static void test_reserved_keys_change_nothing(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    /* Every letter of the set but r (reload), j and k (movement), q and ?: . */
    pty_send_str(s, "sfpocauilhx/gGU");
    pty_send_str(s, "jk");
    pty_send_str(s, "q");
    pty_send_str(s, "ZRDEXe");
    ASSERT_WAIT_SCREEN(s, g_list, 3000);
    NOT_DONE(s);
    pty_send_str(s, "r");
    WAIT_DONE(s, "done: reloaded");
    pty_close(s);
}

/* The way out: Enter opens, q and Esc go back, Esc on the top quits, ctrl-c quits. */
static void test_way_out(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    pty_send_str(s, "\r");
    pty_wait_for(s, "Detail of Alpha", 3000);
    ASSERT(pty_screen_contains(s, "Keys · detail"), "the detail view");
    pty_send_str(s, "q");
    WAIT_LIST(s);
    pty_send_str(s, "q");                                   /* never quits */
    pty_send_key(s, PTY_KEY_BACK);                     /* nor does Backspace */
    pty_send_str(s, "\r");
    pty_wait_for(s, "Detail of Alpha", 3000);
    pty_send_key(s, PTY_KEY_BACK);                     /* Backspace goes back */
    WAIT_LIST(s);
    pty_send_str(s, "\r");
    pty_wait_for(s, "Detail of Alpha", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    WAIT_LIST(s);
    pty_send_key(s, PTY_KEY_ESC);                           /* the top level, nothing open */
    ASSERT_EXIT_STATUS(s, 0, 3000);
    pty_close(s);

    /* ctrl-c from the detail view, the menu, the confirmation and the help. */
    static const char *const where[] = { "\r", ":", ":jj\r", "?" };
    for (int i = 0; i < 4; i++) {
        s = start();
        ASSERT(s != NULL, "start");
        WAIT_LIST(s);
        pty_send_str(s, where[i]);
        pty_send_key(s, PTY_KEY_CTRL('c'));
        ASSERT_EXIT_STATUS(s, 0, 3000);
        pty_close(s);
    }
    /* Esc closes a menu first; the program is still running. */
    s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    pty_send_str(s, ":");
    pty_wait_for(s, "Actions", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    WAIT_LIST(s);
    pty_send_str(s, "j");
    const char *const moved[] = { "  Alpha", "> Bravo", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(3, 0, 2, 7), moved, 3000);
    pty_close(s);
}

/* The detail view has its own actions; the list's keys are unbound there. */
static void test_other_view_other_actions(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    pty_send_str(s, "\r");
    pty_wait_for(s, "Detail of Alpha", 3000);
    pty_send_str(s, "*!d");
    NOT_DONE(s);
    pty_send_str(s, "+");
    WAIT_DONE(s, "done: Mark read Alpha");
    pty_send_str(s, ":");
    pty_wait_for(s, "Actions", 3000);
    ASSERT(pty_screen_contains(s, "+  Mark read"), "this view's action, its key beside it");
    ASSERT(pty_screen_contains(s, "Rotate secret"), "a menu-only action");
    ASSERT(!pty_screen_contains(s, "Junk"), "not the list's");
    pty_close(s);
}

/* The help is made from the table: it lists the keys the table binds in this
 * view and nothing else, and each key does what the table says. */
static void test_help_matches_the_keys(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    pty_send_str(s, "?");
    pty_wait_for(s, "List shortcuts", 3000);
    static const char *const rows[][2] = {
        { "?", "Help" }, { ":", "Action menu" }, { "Esc, q, Backspace", "Back" },
        { "Esc", "Quit on the top level" }, { "ctrl-c", "Quit" }, { "j", "Down" },
        { "k", "Up" }, { "Enter", "Open" }, { "r", "Reload" }, { "*", "Star - toggle the star" },
        { "!", "Junk" }, { "d", "Delete" } };
    for (size_t i = 0; i < sizeof rows / sizeof rows[0]; i++) {
        char row[256];
        int found = 0;
        for (int r = 0; r < ROWS && !found; r++) {
            pty_row_text(s, r, row, sizeof row);
            char want[160];
            snprintf(want, sizeof want, " %s ", rows[i][0]);
            found = strstr(row, want) && strstr(row, rows[i][1]);
        }
        ASSERT(found, rows[i][1]);
    }
    ASSERT(!pty_screen_contains(s, "Mark read"), "not another view's key");
    ASSERT(!pty_screen_contains(s, "Restart"), "not a menu-only action");
    /* Keys in bold, names plain. */
    char d[160];
    int kr = -1;
    for (int r = 0; r < ROWS; r++) {
        char row[256];
        pty_row_text(s, r, row, sizeof row);
        if (strstr(row, "Star - toggle")) kr = r;
    }
    ASSERT(kr >= 0, "the star row");
    int kc = 0;
    while (kc < COLS && strcmp(pty_cell_text(s, kr, kc), "*") != 0) kc++;
    pty_cell_describe(s, kr, kc, d, sizeof d);
    ASSERT(strstr(d, "bold") != NULL, "the key is bold");
    pty_cell_describe(s, kr, kc + 3, d, sizeof d);
    ASSERT(strstr(d, "bold") == NULL, "the name is not");
    /* Any other key changes nothing; Esc, q and Backspace close it. */
    pty_send_str(s, "x*");
    ASSERT(pty_screen_contains(s, "List shortcuts"), "still open");
    pty_send_str(s, "q");
    ASSERT_WAIT_GONE(s, "List shortcuts", 3000);
    NOT_DONE(s);
    pty_send_str(s, "?");
    pty_wait_for(s, "List shortcuts", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_GONE(s, "List shortcuts", 3000);
    WAIT_LIST(s);

    /* Each listed key does what the help says. */
    pty_send_str(s, "j");
    const char *const down[] = { "  Alpha", "> Bravo", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(3, 0, 2, 7), down, 3000);
    pty_send_str(s, "k");
    const char *const up[] = { "> Alpha", "  Bravo", NULL };
    ASSERT_WAIT_REGION(s, PTY_REGION(3, 0, 2, 7), up, 3000);
    pty_send_str(s, "r");
    WAIT_DONE(s, "done: reloaded");
    pty_send_str(s, "*");
    WAIT_DONE(s, "done: Star Alpha");
    pty_send_str(s, "!");
    WAIT_DONE(s, "done: Junk Alpha");
    pty_send_str(s, "d");
    pty_wait_for(s, "Delete Alpha?", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_GONE(s, "Delete Alpha?", 3000);
    pty_send_str(s, ":");
    pty_wait_for(s, "Actions", 3000);
    pty_send_key(s, PTY_KEY_ESC);
    ASSERT_WAIT_GONE(s, "Actions", 3000);
    pty_close(s);
}

/* The description of the view sits between the title and the keys, wrapped,
 * in the plain text style (the keys are bold); the view without one shows the
 * keys only. */
static void test_help_describes_the_view(void) {
    PtySession *s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    pty_send_str(s, "?");
    pty_wait_for(s, "List shortcuts", 3000);
    int title = -1, d1 = -1, d2 = -1, first = -1;
    for (int r = 0; r < ROWS; r++) {
        char row[256];
        pty_row_text(s, r, row, sizeof row);
        if (strstr(row, "List shortcuts")) title = r;
        if (strstr(row, "Three demo rows; the cursor")) d1 = r;
        if (strstr(row, "Enter opens it, : lists")) d2 = r;
        if (strstr(row, " Help") && first < 0) first = r;
    }
    ASSERT(title >= 0 && d1 == title + 1, "the description is right under the title");
    ASSERT(d2 == d1 + 1, "its second line follows");
    ASSERT(first == d2 + 2, "one blank row, then the keys");
    char row[256];
    pty_row_text(s, d2 + 1, row, sizeof row);
    ASSERT(strstr(row, "  ") != NULL && strstr(row, "Help") == NULL, "the separator row is blank");
    char d[160];
    int c = 0;
    while (c < COLS && strcmp(pty_cell_text(s, d1, c), "T") != 0) c++;
    pty_cell_describe(s, d1, c, d, sizeof d);
    ASSERT(strstr(d, "bold") == NULL, "the description is plain text");
    pty_close(s);

    s = start();
    ASSERT(s != NULL, "start");
    WAIT_LIST(s);
    pty_send_key(s, PTY_KEY_ENTER);
    pty_wait_for(s, "Detail of", 3000);
    pty_send_str(s, "?");
    pty_wait_for(s, "Detail shortcuts", 3000);
    ASSERT(!pty_screen_contains(s, "Three demo rows"), "a view without a description shows none");
    ASSERT(!pty_screen_contains(s, "Enter opens it"), "none of the list's text either");
    pty_close(s);
}

static const TestCase tests[] = {
    TEST_CASE(test_help_describes_the_view),
    TEST_CASE(test_list_at_rest),
    TEST_CASE(test_menu_lists_actions_and_cancels),
    TEST_CASE(test_menu_runs_a_routine_action),
    TEST_CASE(test_menu_serious_action_confirms),
    TEST_CASE(test_menu_only_serious_action),
    TEST_CASE(test_direct_keys),
    TEST_CASE(test_reserved_keys_change_nothing),
    TEST_CASE(test_way_out),
    TEST_CASE(test_other_view_other_actions),
    TEST_CASE(test_help_matches_the_keys),
};

int main(int argc, char **argv) {
    return RUN_SUITE("keys", tests, 100, 120);
}
