/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-21 */
/* Unit tests of tui_viewdoc: the page, the description, the keys, the check of the columns. */

#include "test_helpers.h"
#include "faults.h"
#include "tuikit/viewdoc.h"

#include <errno.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static const tui_table_col TCOLS[] = {
    { "#", 3, 0, TUI_ALIGN_RIGHT, 0, 0, 0 },
    { NULL, 2, 0, TUI_ALIGN_LEFT, 0, 0, 0 },          /* a mark column: no title */
    { "KEY", 8, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "TITLE", 20, 1, TUI_ALIGN_LEFT, 0, 0, 0 },
};
static const tui_viewdoc_col DCOLS[] = {
    { "#", "place in the order" },
    { "KEY", "the ticket" },
    { "TITLE", "the title" },
};
static const char *const KEYS[] = { "Enter", "open the row", "p", "set the priority" };
static const char *const COMMON[] = { "?", "this page", "Esc", "back" };
static const tui_viewdoc VIEW = { "Queue", "The plan of the autopilot.", DCOLS, 3, KEYS, 2 };

static tui_event key(uint32_t cp) {
    tui_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = TUI_EV_KEY;
    ev.key = TUI_KEY_CHAR;
    ev.cp = cp;
    return ev;
}

static tui_event special(tui_key k) {
    tui_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = TUI_EV_KEY;
    ev.key = k;
    return ev;
}

static tui_dialog_state press(tui_viewdoc_page *p, uint32_t cp) {
    tui_event ev = key(cp);
    return tui_viewdoc_handle(p, &ev);
}

typedef struct { tui_screen *scr; tui_buf buf; PtySession *s; } Fx;

static int fx_open(Fx *f, int rows, int cols) {
    f->scr = tui_screen_new(rows, cols);
    tui_buf_init(&f->buf);
    f->s = pty_open(cols, rows);
    return (f->scr && f->s) ? 0 : -1;
}

static void fx_draw(Fx *f, tui_viewdoc_page *p) {
    tui_canvas all = tui_canvas_of(f->scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_viewdoc_render(p, all);
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    if (f->s) pty_close(f->s);
}

static void test_text(void) {
    char out[400];
    int n = tui_viewdoc_text(&VIEW, out, sizeof out);
    ASSERT_EQ_STR(out,
        "The plan of the autopilot.\n"
        "\n"
        "#      place in the order\n"
        "KEY    the ticket\n"
        "TITLE  the title", "the statement, a blank line, a line for each column");
    ASSERT_EQ_INT(n, (int)strlen(out), "the length");
    ASSERT_EQ_INT(tui_viewdoc_text(&VIEW, NULL, 0), n, "the length alone");
    char small[10];
    ASSERT_EQ_INT(tui_viewdoc_text(&VIEW, small, sizeof small), n, "cut: the full length");
    ASSERT_EQ_STR(small, "The plan ", "cut and terminated");
    tui_viewdoc none = { "Text", "Only a statement.", NULL, 0, NULL, 0 };
    ASSERT_EQ_INT(tui_viewdoc_text(&none, out, sizeof out), 17, "no columns");
    ASSERT_EQ_STR(out, "Only a statement.", "the statement alone");
    tui_viewdoc blank = { NULL, NULL, NULL, 0, NULL, 0 };
    ASSERT_EQ_INT(tui_viewdoc_text(&blank, out, sizeof out), 0, "nothing");
    tui_viewdoc_col nul[] = { { NULL, NULL } };
    tui_viewdoc nulv = { "x", "s", nul, 1, NULL, 0 };
    ASSERT_EQ_INT(tui_viewdoc_text(&nulv, out, sizeof out), 5, "an empty column entry");
    ASSERT_EQ_INT(tui_viewdoc_text(NULL, out, sizeof out), -1, "no view");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    tui_viewdoc badv = { "x", "s", NULL, 1, NULL, 0 };
    ASSERT_EQ_INT(tui_viewdoc_text(&badv, out, sizeof out), -1, "columns missing");
    tui_viewdoc badk = { "x", "s", NULL, 0, NULL, 1 };
    ASSERT_EQ_INT(tui_viewdoc_text(&badk, out, sizeof out), -1, "keys missing");
    tui_viewdoc negc = { "x", "s", DCOLS, -1, NULL, 0 };
    ASSERT_EQ_INT(tui_viewdoc_text(&negc, out, sizeof out), -1, "negative columns");
    tui_viewdoc negk = { "x", "s", NULL, 0, KEYS, -1 };
    ASSERT_EQ_INT(tui_viewdoc_text(&negk, out, sizeof out), -1, "negative keys");
    ASSERT_EQ_INT(tui_viewdoc_text(&VIEW, NULL, 5), -1, "no buffer");
}

static void test_page(void) {
    tui_viewdoc_page p;
    ASSERT_EQ_INT(tui_viewdoc_open(&p, &VIEW, COMMON, 2), 0, "open");
    ASSERT_EQ_INT(p.state, TUI_DIALOG_OPEN, "open state");
    ASSERT_EQ_INT(p.help.nitems, 6, "two headings, two keys of the view, two of every view");
    Fx f;
    ASSERT(fx_open(&f, 20, 70) == 0, "fixtures");
    fx_draw(&f, &p);
    ASSERT(pty_screen_contains(f.s, "Queue help"), "the title");
    ASSERT(pty_screen_contains(f.s, "Press d: what this view is and what each column means."),
           "the first line reminds of d");
    ASSERT(pty_screen_contains(f.s, "This view"), "the heading of the view's keys");
    ASSERT(pty_screen_contains(f.s, "set the priority"), "a key of the view");
    ASSERT(pty_screen_contains(f.s, "Every view"), "the heading of every view");
    ASSERT(pty_screen_contains(f.s, "this page"), "a key of every view");
    ASSERT(!pty_screen_contains(f.s, "place in the order"), "the columns are not on the page");

    ASSERT_EQ_INT(press(&p, 'x'), TUI_DIALOG_OPEN, "another key does nothing");
    ASSERT_EQ_INT(press(&p, 'd'), TUI_DIALOG_OPEN, "d opens the description");
    ASSERT_EQ_INT(p.in_desc, 1, "in the description");
    fx_close(&f);
    ASSERT(fx_open(&f, 20, 70) == 0, "fixtures");
    fx_draw(&f, &p);
    ASSERT(pty_screen_contains(f.s, "Queue view"), "the title of the dialog");
    ASSERT(pty_screen_contains(f.s, "The plan of the autopilot."), "the statement");
    ASSERT(pty_screen_contains(f.s, "#      place in the order"), "a line for the first column");
    ASSERT(pty_screen_contains(f.s, "TITLE  the title"), "a line for the last column");
    ASSERT(!pty_screen_contains(f.s, "Press d"), "a dialog of its own");
    ASSERT_EQ_INT(press(&p, 'd'), TUI_DIALOG_OPEN, "d in the description does nothing");
    tui_event esc = special(TUI_KEY_ESC);
    ASSERT_EQ_INT(tui_viewdoc_handle(&p, &esc), TUI_DIALOG_OK, "Esc closes it, and the page with it");
    ASSERT_EQ_INT(press(&p, 'd'), TUI_DIALOG_OK, "closed stays closed");
    fx_close(&f);
    tui_viewdoc_free(&p);

    /* Esc, q and Backspace close the page itself. */
    ASSERT_EQ_INT(tui_viewdoc_open(&p, &VIEW, COMMON, 2), 0, "open again");
    ASSERT_EQ_INT(tui_viewdoc_handle(&p, &esc), TUI_DIALOG_OK, "Esc closes the page");
    tui_viewdoc_free(&p);
    ASSERT_EQ_INT(tui_viewdoc_open(&p, &VIEW, COMMON, 2), 0, "open again");
    ASSERT_EQ_INT(press(&p, 'q'), TUI_DIALOG_OK, "q closes the page");
    tui_viewdoc_free(&p);
    /* q closes the description too. */
    ASSERT_EQ_INT(tui_viewdoc_open(&p, &VIEW, NULL, 0), 0, "open without the common keys");
    ASSERT_EQ_INT(p.help.nitems, 4, "two headings and the keys of the view");
    press(&p, 'd');
    ASSERT_EQ_INT(press(&p, 'q'), TUI_DIALOG_OK, "q closes the description");
    tui_viewdoc_free(&p);
    tui_viewdoc_free(&p);   /* twice is safe */
}

static void test_search_keeps_d(void) {
    tui_viewdoc_page p;
    ASSERT_EQ_INT(tui_viewdoc_open(&p, &VIEW, COMMON, 2), 0, "open");
    press(&p, '/');
    ASSERT_EQ_INT(press(&p, 'd'), TUI_DIALOG_OPEN, "a d typed in the search");
    ASSERT_EQ_INT(p.in_desc, 0, "does not open the description");
    Fx f;
    ASSERT(fx_open(&f, 20, 70) == 0, "fixtures");
    fx_draw(&f, &p);
    ASSERT(pty_screen_contains(f.s, "describe") == 0, "the search narrowed the keys to those with d");
    ASSERT(pty_screen_contains(f.s, "Queue help"), "still the page");
    tui_event enter = special(TUI_KEY_ENTER);
    tui_viewdoc_handle(&p, &enter);                 /* leaves the line, keeps the list */
    ASSERT_EQ_INT(press(&p, 'd'), TUI_DIALOG_OPEN, "d outside the line");
    ASSERT_EQ_INT(p.in_desc, 1, "opens the description");
    fx_close(&f);
    tui_viewdoc_free(&p);
}

static void test_arguments(void) {
    tui_viewdoc_page p;
    ASSERT_EQ_INT(tui_viewdoc_open(NULL, &VIEW, COMMON, 2), -1, "no page");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_viewdoc_open(&p, NULL, COMMON, 2), -1, "no view");
    ASSERT_EQ_INT(tui_viewdoc_open(&p, &VIEW, NULL, 2), -1, "common keys missing");
    ASSERT_EQ_INT(tui_viewdoc_open(&p, &VIEW, COMMON, -1), -1, "negative count");
    tui_viewdoc unnamed = { NULL, NULL, NULL, 0, NULL, 0 };
    ASSERT_EQ_INT(tui_viewdoc_open(&p, &unnamed, NULL, 0), 0, "a view with nothing registered");
    tui_viewdoc_free(&p);
    tui_viewdoc_free(NULL);
    tui_screen *scr = tui_screen_new(2, 2);
    tui_viewdoc_render(NULL, tui_canvas_of(scr));
    tui_screen_free(scr);
    tui_event ev = key('d');
    ASSERT_EQ_INT(tui_viewdoc_handle(NULL, &ev), TUI_DIALOG_CANCELLED, "no page");
    ASSERT_EQ_INT(tui_viewdoc_open(&p, &VIEW, COMMON, 2), 0, "open");
    ASSERT_EQ_INT(tui_viewdoc_handle(&p, NULL), TUI_DIALOG_CANCELLED, "no event");
    tui_viewdoc_free(&p);
}

static void test_check(void) {
    char msg[200];
    ASSERT_EQ_INT(tui_viewdoc_check(&VIEW, TCOLS, 4, msg, sizeof msg), 0, "they agree; the mark is left out");
    ASSERT_EQ_STR(msg, "", "and says nothing");
    ASSERT_EQ_INT(tui_viewdoc_check(&VIEW, TCOLS, 3, msg, sizeof msg), -1, "a described column is gone");
    ASSERT_EQ_STR(msg, "description of column \"TITLE\" has no column", "which one");
    tui_viewdoc less = { "Queue", "s", DCOLS, 2, KEYS, 2 };
    ASSERT_EQ_INT(tui_viewdoc_check(&less, TCOLS, 4, msg, sizeof msg), -1, "a column is not described");
    ASSERT_EQ_STR(msg, "column \"TITLE\" is not described", "which one");
    tui_viewdoc_col swapped[] = { DCOLS[0], DCOLS[2], DCOLS[1] };
    tui_viewdoc sw = { "Queue", "s", swapped, 3, KEYS, 2 };
    ASSERT_EQ_INT(tui_viewdoc_check(&sw, TCOLS, 4, msg, sizeof msg), -1, "another order");
    ASSERT_EQ_STR(msg, "column \"KEY\" is described as \"TITLE\"", "which one");
    ASSERT_EQ_INT(tui_viewdoc_check(&VIEW, TCOLS, 3, NULL, 0), -1, "no message wanted");
    ASSERT_EQ_INT(tui_viewdoc_check(&less, TCOLS, 4, NULL, 0), -1, "no message wanted, a column missing");
    ASSERT_EQ_INT(tui_viewdoc_check(&sw, TCOLS, 4, NULL, 0), -1, "no message wanted, another order");
    ASSERT_EQ_INT(tui_viewdoc_check(&VIEW, TCOLS, 4, NULL, 0), 0, "no message wanted, agreeing");
    tui_table_col blank[] = { { "", 1, 0, TUI_ALIGN_LEFT, 0, 0, 0 } };
    tui_viewdoc none = { "x", "s", NULL, 0, NULL, 0 };
    ASSERT_EQ_INT(tui_viewdoc_check(&none, blank, 1, msg, sizeof msg), 0, "an empty title is a mark column");
    ASSERT_EQ_INT(tui_viewdoc_check(NULL, TCOLS, 4, msg, sizeof msg), -1, "no view");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_viewdoc_check(&VIEW, NULL, 2, msg, sizeof msg), -1, "no columns");
    ASSERT_EQ_INT(tui_viewdoc_check(&VIEW, TCOLS, -1, msg, sizeof msg), -1, "negative count");
}

static void test_memory(void) {
    tui_viewdoc_page p;
    for (int skip = 0; skip < 12; skip++) {
        fault_arm(F_CALLOC, skip, 1, ENOMEM);
        int r = tui_viewdoc_open(&p, &VIEW, COMMON, 2);
        fault_reset();
        if (r == 0) tui_viewdoc_free(&p); else ASSERT_EQ_INT(errno, ENOMEM, "ENOMEM");
        fault_arm(F_MALLOC, skip, 1, ENOMEM);
        r = tui_viewdoc_open(&p, &VIEW, COMMON, 2);
        fault_reset();
        if (r == 0) tui_viewdoc_free(&p); else ASSERT_EQ_INT(errno, ENOMEM, "ENOMEM");
    }
    ASSERT_EQ_INT(tui_viewdoc_open(&p, &VIEW, COMMON, 2), 0, "open");
    /* The description cannot be built: the page stays as it is. */
    int opened = 0;
    for (int skip = 0; skip < 8 && !opened; skip++) {
        fault_arm(F_MALLOC, skip, 1, ENOMEM);
        press(&p, 'd');
        fault_reset();
        opened = p.in_desc;
        ASSERT_EQ_INT(p.state, TUI_DIALOG_OPEN, "still open after a failed d");
    }
    tui_viewdoc_free(&p);
}

static const TestCase tests[] = {
    TEST_CASE(test_text),
    TEST_CASE(test_page),
    TEST_CASE(test_search_keeps_d),
    TEST_CASE(test_arguments),
    TEST_CASE(test_check),
    TEST_CASE(test_memory),
};

int main(int argc, char **argv) {
    setenv("TERM", "xterm-256color", 1);
    unsetenv("COLORTERM");
    return RUN_SUITE("viewdoc", tests, 6, 60);
}
