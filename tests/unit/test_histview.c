/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-21 */
/* Unit tests of tui_histview: rows, selection, the revert flow, states, drawing. */

/*
 * policies/undo-model.md: a list of recorded steps with a Revert entry, the
 * confirmation names the step, a refusal is shown in the server's words,
 * a step marked not revertable opens no dialog.
 */

#include "test_helpers.h"
#include "faults.h"
#include "tuikit/histview.h"
#include "tuikit/buf.h"
#include "tuikit/screen.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static const tui_histview_row g_rows[] = {
    { 5, "2026-10-21 10:07", "ann", "set", "TUI-9", "status", "OPEN", "DONE", 0, 1 },
    { 4, "2026-10-21 10:06", "bob", "revert", "TUI-9", "title", "New", "Old", 3, 1 },
    { 2, "2026-10-21 10:04", "ann", "send", "TUI-9", "mail", NULL, "sent", 0, 0 },
    { 1, "2026-10-21 10:03", "ann", "set", NULL, NULL, NULL, NULL, 0, 1 },
};
#define N_ROWS 4

static const tui_histview_row g_other[] = {
    { 9, "2026-10-21 11:00", "eve", "set", "TUI-1", "x", "a", "b", 0, 0 },
};

typedef struct { int calls; uint64_t last; int result; const char *why; } stub;

static int revert_stub(void *ctx, uint64_t id, char *refusal, size_t size) {
    stub *st = ctx;
    st->calls++;
    st->last = id;
    if (st->result != 0 && st->why) snprintf(refusal, size, "%s", st->why);
    return st->result;
}

static tui_event key_ev(tui_key k) {
    tui_event e;
    memset(&e, 0, sizeof e);
    e.type = TUI_EV_KEY;
    e.key = k;
    return e;
}

static tui_event char_ev(uint32_t cp) {
    tui_event e = key_ev(TUI_KEY_CHAR);
    e.cp = cp;
    return e;
}

static tui_histview_state press(tui_histview *h, tui_event e) { return tui_histview_handle(h, &e); }
#define KEY(h, k)  press((h), key_ev(k))
#define CH(h, c)   press((h), char_ev(c))

static void setup(tui_histview *h, tui_histview_mode mode, stub *st) {
    ASSERT_EQ_INT(tui_histview_init(h, mode, st ? revert_stub : NULL, st), 0, "init");
    tui_histview_set_rows(h, g_rows, N_ROWS);
}

/* The screen as rows of text. */
static tui_screen *draw(tui_histview *h, int rows, int cols, PtySession **out) {
    tui_screen *scr = tui_screen_new(rows, cols);
    tui_buf buf;
    tui_buf_init(&buf);
    PtySession *s = pty_open(cols, rows);
    if (!scr || !s) exit(2);
    tui_histview_render(h, tui_canvas_of(scr));
    tui_screen_render(scr, &buf);
    pty_feed(s, buf.data, buf.len);
    tui_buf_free(&buf);
    *out = s;
    return scr;
}

static int on_screen(tui_histview *h, int rows, int cols, const char *text) {
    PtySession *s;
    tui_screen *scr = draw(h, rows, cols, &s);
    int found = pty_screen_contains(s, text);
    pty_close(s);
    tui_screen_free(scr);
    return found;
}

static void test_init_and_free(void) {
    tui_histview h;
    errno = 0;
    ASSERT_EQ_INT(tui_histview_init(NULL, TUI_HISTVIEW_OBJECT, NULL, NULL), -1, "NULL view");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_histview_init(&h, TUI_HISTVIEW_RECENT, NULL, NULL), 0, "recent");
    ASSERT_EQ_INT(tui_histview_selected(&h), -1, "no rows, nothing selected");
    ASSERT_EQ_STR(tui_histview_notice(&h), "", "no notice");
    ASSERT_EQ_INT(tui_histview_modal(&h), 0, "not modal");
    tui_histview_free(&h);
    tui_histview_free(NULL);
    ASSERT_EQ_STR(tui_histview_notice(NULL), "", "NULL notice");
    ASSERT_EQ_INT(tui_histview_modal(NULL), 0, "NULL modal");
    ASSERT_EQ_INT(tui_histview_selected(NULL), -1, "NULL selected");
    tui_histview_set_rows(NULL, g_rows, N_ROWS);
    tui_histview_set_failed(NULL, 1);
    ASSERT_EQ_INT(CH(NULL, 'j'), TUI_HISTVIEW_OPEN, "NULL handle");
    tui_histview_render(NULL, (tui_canvas){ 0 });
}

static void test_init_failure(void) {
    int failed = 0;
    for (int skip = 0; skip < 40; skip++) {
        fault_fn fn = skip % 2 ? F_CALLOC : F_MALLOC;
        for (int k = 0; k < 40; k++) {
            tui_histview h;
            fault_arm(fn, k, 1, ENOMEM);
            int rc = tui_histview_init(&h, TUI_HISTVIEW_OBJECT, NULL, NULL);
            fault_reset();
            if (rc != 0) {
                failed = 1;
                ASSERT_EQ_INT(errno, ENOMEM, "ENOMEM");
            } else {
                tui_histview_free(&h);
            }
        }
    }
    ASSERT(failed, "an allocation failure is reported");
}

static void test_selection_by_id(void) {
    tui_histview h;
    setup(&h, TUI_HISTVIEW_OBJECT, NULL);
    ASSERT_EQ_INT(tui_histview_selected(&h), 0, "first row");
    CH(&h, 'j');
    CH(&h, 'j');
    ASSERT_EQ_INT(tui_histview_selected(&h), 2, "moved down");
    /* A reload that keeps the step keeps the selection on it. */
    static const tui_histview_row more[] = {
        { 7, "t", "a", "set", NULL, "n", "1", "2", 0, 1 },
        { 6, "t", "a", "set", NULL, "n", "1", "2", 0, 1 },
        { 5, "t", "a", "set", NULL, "n", "1", "2", 0, 1 },
        { 4, "t", "a", "set", NULL, "n", "1", "2", 0, 1 },
        { 2, "t", "a", "set", NULL, "n", "1", "2", 0, 0 },
    };
    tui_histview_set_rows(&h, more, 5);
    ASSERT_EQ_INT(tui_histview_selected(&h), 4, "still on step 2");
    tui_histview_set_rows(&h, g_other, 1);
    ASSERT_EQ_INT(tui_histview_selected(&h), 0, "step gone, clamped");
    tui_histview_set_rows(&h, NULL, 3);
    ASSERT_EQ_INT(tui_histview_selected(&h), -1, "NULL rows are none");
    tui_histview_set_rows(&h, g_rows, 0);
    ASSERT_EQ_INT(tui_histview_selected(&h), -1, "n = 0 is none");
    tui_histview_set_rows(&h, g_rows, N_ROWS);
    tui_histview_set_failed(&h, 1);
    ASSERT_EQ_INT(tui_histview_selected(&h), -1, "failed: nothing selected");
    tui_histview_set_failed(&h, 0);
    ASSERT_EQ_INT(tui_histview_selected(&h), 0, "readable again");
    tui_histview_free(&h);
}

static void test_revert_yes_and_no(void) {
    tui_histview h;
    stub st = { 0, 0, 0, NULL };
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    ASSERT_EQ_INT(KEY(&h, TUI_KEY_ENTER), TUI_HISTVIEW_OPEN, "Enter opens");
    ASSERT_EQ_INT(tui_histview_modal(&h), 1, "modal");
    ASSERT(on_screen(&h, 24, 80, "Revert step 5 (set, status): status becomes \"OPEN\"?"), "names the step");
    ASSERT_EQ_INT(CH(&h, 'n'), TUI_HISTVIEW_OPEN, "No");
    ASSERT_EQ_INT(st.calls, 0, "nothing asked");
    ASSERT_EQ_INT(tui_histview_modal(&h), 0, "closed");
    KEY(&h, TUI_KEY_ENTER);
    ASSERT_EQ_INT(KEY(&h, TUI_KEY_ENTER), TUI_HISTVIEW_OPEN, "Enter on the default answer");
    ASSERT_EQ_INT(st.calls, 0, "the default is No");
    KEY(&h, TUI_KEY_ENTER);
    ASSERT_EQ_INT(CH(&h, 'y'), TUI_HISTVIEW_REVERTED, "Yes");
    ASSERT_EQ_INT(st.calls, 1, "one call");
    ASSERT_EQ_INT((int)st.last, 5, "of step 5");
    ASSERT_EQ_STR(tui_histview_notice(&h), "reverted step 5", "notice");
    ASSERT(on_screen(&h, 24, 80, "reverted step 5"), "shown");
    CH(&h, 'j');
    ASSERT_EQ_STR(tui_histview_notice(&h), "", "a key clears the notice");
    tui_histview_free(&h);
}

static void test_refusal(void) {
    tui_histview h;
    stub st = { 0, 0, -1, "later change\nover\ttook it" };
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    KEY(&h, TUI_KEY_ENTER);
    ASSERT_EQ_INT(CH(&h, 'y'), TUI_HISTVIEW_OPEN, "refused: stay");
    ASSERT_EQ_STR(tui_histview_notice(&h), "later change over took it", "server words, one line");
    st.why = NULL;
    KEY(&h, TUI_KEY_ENTER);
    CH(&h, 'y');
    ASSERT_EQ_STR(tui_histview_notice(&h), "refused", "no words given");
    char longtext[400];
    memset(longtext, 'x', sizeof longtext - 1);
    longtext[sizeof longtext - 1] = '\0';
    st.why = longtext;
    KEY(&h, TUI_KEY_ENTER);
    CH(&h, 'y');
    ASSERT(strlen(tui_histview_notice(&h)) < 160, "cut to the notice size");
    tui_histview_free(&h);
}

static void test_menu_path(void) {
    tui_histview h;
    stub st = { 0, 0, 0, NULL };
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    CH(&h, ':');
    ASSERT_EQ_INT(tui_histview_modal(&h), 1, "menu open");
    ASSERT(on_screen(&h, 24, 80, "Revert"), "the Revert entry");
    /* The first entry is the harmless one; the arrows reach Revert. */
    KEY(&h, TUI_KEY_DOWN);
    KEY(&h, TUI_KEY_ENTER);
    ASSERT_EQ_INT(tui_histview_modal(&h), 1, "confirming");
    ASSERT_EQ_INT(CH(&h, 'y'), TUI_HISTVIEW_REVERTED, "reverted from the menu");
    ASSERT_EQ_INT(st.calls, 1, "asked once");
    CH(&h, ':');
    KEY(&h, TUI_KEY_ESC);
    ASSERT_EQ_INT(tui_histview_modal(&h), 0, "Esc closes the menu");
    tui_histview_free(&h);
}

static void test_not_revertable_and_missing(void) {
    tui_histview h;
    stub st = { 0, 0, 0, NULL };
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    CH(&h, 'j');
    CH(&h, 'j');
    KEY(&h, TUI_KEY_ENTER);
    ASSERT_EQ_INT(tui_histview_modal(&h), 0, "no dialog");
    ASSERT_EQ_STR(tui_histview_notice(&h), "step 2 is not revertable", "notice");
    CH(&h, ':');
    ASSERT_EQ_INT(tui_histview_modal(&h), 0, "no menu either");
    ASSERT_EQ_INT(st.calls, 0, "nothing asked");
    tui_histview_free(&h);

    setup(&h, TUI_HISTVIEW_OBJECT, NULL);
    KEY(&h, TUI_KEY_ENTER);
    ASSERT_EQ_INT(tui_histview_modal(&h), 0, "no callback: no dialog");
    ASSERT_EQ_STR(tui_histview_notice(&h), "reverting is not available here", "notice");
    tui_histview_free(&h);

    tui_histview_init(&h, TUI_HISTVIEW_OBJECT, revert_stub, &st);
    KEY(&h, TUI_KEY_ENTER);
    ASSERT_EQ_INT(tui_histview_modal(&h), 0, "no rows: no dialog");
    ASSERT_EQ_STR(tui_histview_notice(&h), "", "and no notice");
    tui_histview_free(&h);
}

static void test_rows_change_under_dialog(void) {
    tui_histview h;
    stub st = { 0, 0, 0, NULL };
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    KEY(&h, TUI_KEY_ENTER);
    tui_histview_set_rows(&h, g_other, 1);              /* the new step is not revertable */
    ASSERT_EQ_INT(CH(&h, 'y'), TUI_HISTVIEW_OPEN, "answered after a reload");
    ASSERT_EQ_INT(st.calls, 0, "nothing asked");
    ASSERT_EQ_STR(tui_histview_notice(&h), "step 9 is not revertable", "refused by the view");
    KEY(&h, TUI_KEY_ENTER);
    ASSERT_EQ_INT(tui_histview_modal(&h), 0, "still none");
    tui_histview_set_rows(&h, g_rows, N_ROWS);
    KEY(&h, TUI_KEY_ENTER);
    tui_histview_set_rows(&h, NULL, 0);
    CH(&h, 'y');
    ASSERT_EQ_INT(st.calls, 0, "emptied under the dialog");
    tui_histview_free(&h);
}

static tui_event ctrl_c(void) {
    tui_event e = char_ev('c');
    e.mods = TUI_MOD_CTRL;
    return e;
}

static void test_quit_and_back(void) {
    tui_histview h;
    stub st = { 0, 0, 0, NULL };
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    ASSERT_EQ_INT(CH(&h, 'q'), TUI_HISTVIEW_BACK, "q goes back");
    ASSERT_EQ_INT(press(&h, ctrl_c()), TUI_HISTVIEW_QUIT, "Ctrl-C quits");
    ASSERT_EQ_INT(KEY(&h, TUI_KEY_ESC), TUI_HISTVIEW_BACK, "Esc");
    ASSERT_EQ_INT(KEY(&h, TUI_KEY_BACKSPACE), TUI_HISTVIEW_BACK, "Backspace");
    KEY(&h, TUI_KEY_ENTER);
    ASSERT_EQ_INT(press(&h, ctrl_c()), TUI_HISTVIEW_QUIT, "Ctrl-C under a dialog");
    tui_histview_free(&h);
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    CH(&h, '?');
    ASSERT_EQ_INT(press(&h, ctrl_c()), TUI_HISTVIEW_QUIT, "Ctrl-C in the help");
    tui_histview_free(&h);
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    tui_event mouse;
    memset(&mouse, 0, sizeof mouse);
    mouse.type = TUI_EV_RESIZE;
    ASSERT_EQ_INT(press(&h, mouse), TUI_HISTVIEW_OPEN, "other events are ignored");
    tui_histview_free(&h);
}

static void test_help(void) {
    tui_histview h;
    stub st = { 0, 0, 0, NULL };
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    CH(&h, '?');
    ASSERT_EQ_INT(tui_histview_modal(&h), 1, "help open");
    ASSERT(on_screen(&h, 24, 80, "History shortcuts"), "titled");
    ASSERT(on_screen(&h, 24, 80, "Revert step"), "lists Enter");
    CH(&h, 'j');
    ASSERT_EQ_INT(tui_histview_modal(&h), 1, "other keys keep it open");
    KEY(&h, TUI_KEY_ESC);
    ASSERT_EQ_INT(tui_histview_modal(&h), 0, "Esc closes it");
    CH(&h, '?');
    tui_histview_free(&h);                              /* freed while open */
}

static void test_menu_free_while_open(void) {
    tui_histview h;
    stub st = { 0, 0, 0, NULL };
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    CH(&h, ':');
    tui_histview_free(&h);
}

static void test_draw_object(void) {
    tui_histview h;
    stub st = { 0, 0, 0, NULL };
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    PtySession *s;
    tui_screen *scr = draw(&h, 24, 80, &s);
    char row[128];
    ASSERT_EQ_STR(pty_row_text(s, 0, row, sizeof row),
                  "  Time             Who    Operation Thing    Old value New value  Note", "header");
    ASSERT_EQ_STR(pty_row_text(s, 1, row, sizeof row),
                  "> 2026-10-21 10:07 ann    set       status   OPEN      DONE", "selected row");
    ASSERT_EQ_STR(pty_row_text(s, 2, row, sizeof row),
                  "  2026-10-21 10:06 bob    revert    title    New       Old        reverts 3", "reverts");
    ASSERT_EQ_STR(pty_row_text(s, 3, row, sizeof row),
                  "  2026-10-21 10:04 ann    send      mail               sent       not revertable", "marker");
    ASSERT_EQ_STR(pty_row_text(s, 4, row, sizeof row),
                  "  2026-10-21 10:03 ann    set", "empty strings");
    pty_close(s);
    tui_screen_free(scr);
    tui_histview_free(&h);
}

static void test_draw_recent(void) {
    tui_histview h;
    stub st = { 0, 0, 0, NULL };
    setup(&h, TUI_HISTVIEW_RECENT, &st);
    PtySession *s;
    tui_screen *scr = draw(&h, 24, 80, &s);
    char row[128];
    ASSERT_EQ_STR(pty_row_text(s, 0, row, sizeof row),
                  "  Time             Who    Operation Object   Thing  Old    New    Note", "header");
    ASSERT_EQ_STR(pty_row_text(s, 2, row, sizeof row),
                  "  2026-10-21 10:06 bob    revert    TUI-9    title  New    Old    reverts 3", "row");
    pty_close(s);
    tui_screen_free(scr);
    KEY(&h, TUI_KEY_ENTER);
    ASSERT(on_screen(&h, 24, 80, "Revert step 5 (set, status of TUI-9): status becomes \"OPEN\"?"),
           "the object is named");
    CH(&h, 'n');
    CH(&h, 'j');
    CH(&h, 'j');
    CH(&h, 'j');
    KEY(&h, TUI_KEY_ENTER);
    ASSERT(on_screen(&h, 24, 80, "step 1 is not revertable") == 0, "step 1 is revertable");
    ASSERT(on_screen(&h, 24, 80, "Revert step 1 (set): the thing becomes \"\"?"), "no thing, no object");
    tui_histview_free(&h);
}

static void test_draw_notice_and_states(void) {
    tui_histview h;
    stub st = { 0, 0, -1, "refused by the server" };
    setup(&h, TUI_HISTVIEW_OBJECT, &st);
    KEY(&h, TUI_KEY_ENTER);
    CH(&h, 'y');
    PtySession *s;
    tui_screen *scr = draw(&h, 24, 80, &s);
    char row[128];
    ASSERT_EQ_STR(pty_row_text(s, 23, row, sizeof row), "refused by the server", "error notice");
    ASSERT_EQ_INT(pty_cell_fg(s, 23, 0), PTY_FG_RED, "error colour");
    pty_close(s);
    tui_screen_free(scr);
    st.result = 0;
    KEY(&h, TUI_KEY_ENTER);
    CH(&h, 'y');
    scr = draw(&h, 24, 80, &s);
    ASSERT_EQ_INT(pty_cell_fg(s, 23, 0), PTY_FG_GREEN, "ok colour");
    pty_close(s);
    tui_screen_free(scr);
    tui_histview_set_rows(&h, NULL, 0);
    ASSERT(on_screen(&h, 24, 80, "no recorded changes"), "empty state");
    tui_histview_set_failed(&h, 1);
    ASSERT(on_screen(&h, 24, 80, "could not be read"), "error state");
    ASSERT(on_screen(&h, 24, 80, "no recorded changes") == 0, "the error replaces the empty state");
    tui_histview_set_rows(&h, g_rows, N_ROWS);
    ASSERT(on_screen(&h, 24, 80, "could not be read") == 0, "set_rows clears the error");
    ASSERT(on_screen(&h, 1, 80, "Time") == 0, "one row: nothing drawn");
    ASSERT(on_screen(&h, 2, 30, "Time"), "two rows still draw");
    tui_histview_free(&h);
}

static const TestCase tests[] = {
    TEST_CASE(test_init_and_free),
    TEST_CASE(test_init_failure),
    TEST_CASE(test_selection_by_id),
    TEST_CASE(test_revert_yes_and_no),
    TEST_CASE(test_refusal),
    TEST_CASE(test_menu_path),
    TEST_CASE(test_not_revertable_and_missing),
    TEST_CASE(test_rows_change_under_dialog),
    TEST_CASE(test_quit_and_back),
    TEST_CASE(test_help),
    TEST_CASE(test_menu_free_while_open),
    TEST_CASE(test_draw_object),
    TEST_CASE(test_draw_recent),
    TEST_CASE(test_draw_notice_and_states),
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return RUN_SUITE("histview", tests, 80, 60);
}
