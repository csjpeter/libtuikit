/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Unit tests of tui_dialog. */

/*
 * Unit tests of tui_dialog: what each kind does with keys, and what it
 * draws -- rendered, fed to libptytest's emulator, compared cell by cell.
 */

#include "test_helpers.h"
#include "tuikit/dialog.h"

#include <errno.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static tui_dialog_state send(tui_dialog *d, const char *keys) {
    tui_decoder dec;
    tui_decoder_init(&dec);
    tui_decoder_feed(&dec, keys, strlen(keys));
    tui_event ev;
    tui_dialog_state st = d->state;
    while (tui_decoder_next(&dec, &ev)) st = tui_dialog_handle(d, &ev);
    if (tui_decoder_timeout(&dec, &ev)) st = tui_dialog_handle(d, &ev);
    tui_decoder_free(&dec);
    return st;
}

typedef struct {
    tui_screen *scr;
    tui_buf     buf;
    PtySession *s;
} Fx;

static int fx_open(Fx *f, int rows, int cols) {
    f->scr = tui_screen_new(rows, cols);
    tui_buf_init(&f->buf);
    f->s = pty_open(cols, rows);
    return (f->scr && f->s) ? 0 : -1;
}

static void fx_draw(Fx *f, tui_dialog *d) {
    tui_canvas all = tui_canvas_of(f->scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_dialog_render(d, all);
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    if (f->s) pty_close(f->s);
}

/* The palette's roles (policies/tui-interface-model.md): the frame in the
 * accent, the focused button and the selected item in the selection. */
static const PtyStyleKey legend[] = {
    { '.', PTY_ATTR_NONE, 0, 0 },
    { 'A', PTY_ATTR_BOLD, PTY_FG_CYAN, PTY_BG_DEFAULT },
    { 'S', PTY_ATTR_BOLD | PTY_ATTR_REVERSE, 0, 0 },
    { 0, 0, 0, 0 } };

static void test_message(void) {
    tui_dialog d;
    ASSERT_EQ_INT(tui_dialog_message(&d, "Note", "Saved."), 0, "create");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    fx_draw(&f, &d);
    const char *const expect[] = {
        "", "",
        "         ┌─ Note ───┐",
        "         │ Saved.   │",
        "         │          │",
        "         │  [ OK ]  │",
        "         └──────────┘",
        "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    const char *const map[] = { "A..SSSSSS..A", NULL };
    ASSERT_REGION_STYLE(f.s, PTY_REGION(5, 9, 1, 12), map, legend);
    ASSERT_EQ_INT(send(&d, "x"), TUI_DIALOG_OPEN, "other keys leave it open");
    ASSERT_EQ_INT(send(&d, " "), TUI_DIALOG_OK, "Space closes it");
    ASSERT_EQ_INT(send(&d, "\x1b"), TUI_DIALOG_OK, "closed stays closed");
    tui_dialog_free(&d);
    tui_dialog_message(&d, "Note", "x");
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OK, "Enter closes it");
    tui_dialog_free(&d);
    tui_dialog_message(&d, "Note", "x");
    ASSERT_EQ_INT(send(&d, "\x1b"), TUI_DIALOG_OK, "Esc closes it");
    tui_dialog_free(&d);
    fx_close(&f);
}

static void test_confirm(void) {
    tui_dialog d;
    ASSERT_EQ_INT(tui_dialog_confirm(&d, "Delete?", "Delete 3 files?", 0), 0, "create");
    ASSERT_EQ_INT(d.answer, 0, "No by default");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    fx_draw(&f, &d);
    const char *const expect[] = {
        "", "",
        "     ┌─ Delete? ───────┐",
        "     │ Delete 3 files? │",
        "     │                 │",
        "     │ [ Yes ]  [ No ] │",
        "     └─────────────────┘",
        "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    const char *const map_no[] = { "A..........SSSSSS.A", NULL };
    ASSERT_REGION_STYLE(f.s, PTY_REGION(5, 5, 1, 19), map_no, legend);
    ASSERT_EQ_INT(send(&d, "\x1b[D"), TUI_DIALOG_OPEN, "Left");
    ASSERT_EQ_INT(d.answer, 1, "focus on Yes");
    fx_draw(&f, &d);
    const char *const map_yes[] = { "A.SSSSSSS.........A", NULL };
    ASSERT_REGION_STYLE(f.s, PTY_REGION(5, 5, 1, 19), map_yes, legend);
    send(&d, "\t");
    ASSERT_EQ_INT(d.answer, 0, "Tab moves back to No");
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OK, "Enter takes the focused button");
    ASSERT_EQ_INT(d.answer, 0, "answer No");
    tui_dialog_free(&d);

    tui_dialog_confirm(&d, "Q", "Sure?", 0);
    ASSERT_EQ_INT(send(&d, "y"), TUI_DIALOG_OK, "y answers at once");
    ASSERT_EQ_INT(d.answer, 1, "yes");
    tui_dialog_free(&d);
    tui_dialog_confirm(&d, "Q", "Sure?", 1);
    ASSERT_EQ_INT(d.answer, 1, "default yes");
    ASSERT_EQ_INT(send(&d, "N"), TUI_DIALOG_OK, "N answers at once");
    ASSERT_EQ_INT(d.answer, 0, "no");
    tui_dialog_free(&d);
    tui_dialog_confirm(&d, "Q", "Sure?", 1);
    ASSERT_EQ_INT(send(&d, "\x03"), TUI_DIALOG_CANCELLED, "Ctrl-C cancels");
    ASSERT_EQ_INT(d.answer, 0, "a cancelled question is not a yes");
    tui_dialog_free(&d);
    fx_close(&f);
}

static void test_prompt(void) {
    tui_dialog d;
    char buf[32];
    ASSERT_EQ_INT(tui_dialog_prompt(&d, "Rename", "New name:", buf, sizeof(buf), "old.txt"), 0,
                  "create");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    fx_draw(&f, &d);
    const char *const expect[] = {
        "", "",
        " ┌─ Rename ─────────────────┐",
        " │ New name:                │",
        " │ old.txt                  │",
        " └──────────────────────────┘",
        "", "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    int r, c;
    pty_cursor(f.s, &r, &c);
    ASSERT_EQ_INT(r, 4, "cursor in the input line");
    ASSERT_EQ_INT(c, 10, "cursor after the text");
    ASSERT_EQ_INT(send(&d, "\x7f\x7f\x7fmd"), TUI_DIALOG_OPEN, "editing");
    ASSERT_EQ_STR(buf, "old.md", "edited in the caller's buffer");
    ASSERT_EQ_INT(send(&d, "\x04\x04\x04"), TUI_DIALOG_OPEN, "Ctrl-D does not close a prompt");
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OK, "Enter confirms");
    tui_dialog_free(&d);
    tui_dialog_prompt(&d, "Rename", NULL, buf, sizeof(buf), NULL);
    ASSERT_EQ_INT(send(&d, "abc\x1b"), TUI_DIALOG_CANCELLED, "Esc cancels");
    tui_dialog_free(&d);
    errno = 0;
    ASSERT_EQ_INT(tui_dialog_prompt(&d, "x", "y", NULL, 4, NULL), -1, "no buffer");
    ASSERT_EQ_INT(errno, EINVAL, "errno");
    fx_close(&f);
}

static void test_menu(void) {
    static const char *const items[] = { "Open", "Save as\xE2\x80\xA6", "Quit" };
    tui_dialog d;
    ASSERT_EQ_INT(tui_dialog_menu(&d, "File", items, 3, 1), 0, "create");
    ASSERT_EQ_INT(d.answer, 1, "initial selection");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    fx_draw(&f, &d);
    const char *const expect[] = {
        "", "",
        "        ┌─ File ─────┐",
        "        │ Open       │",
        "        │ Save as…   │",
        "        │ Quit       │",
        "        └────────────┘",
        "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    const char *const map[] = { "A............A", "A.SSSSSSSSSS.A", "A............A", NULL };
    ASSERT_REGION_STYLE(f.s, PTY_REGION(3, 8, 3, 14), map, legend);
    send(&d, "\x1b[B");
    ASSERT_EQ_INT(d.answer, 2, "Down");
    send(&d, "\x1b[B");
    ASSERT_EQ_INT(d.answer, 2, "Down at the end");
    send(&d, "\x1b[H");
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OK, "Enter chooses");
    ASSERT_EQ_INT(d.answer, 0, "the first item");
    tui_dialog_free(&d);
    tui_dialog_menu(&d, "File", items, 3, 0);
    ASSERT_EQ_INT(send(&d, "\x1b"), TUI_DIALOG_CANCELLED, "Esc cancels");
    tui_dialog_free(&d);
    tui_dialog_menu(&d, "Empty", NULL, 0, 0);
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OPEN, "Enter in an empty menu chooses nothing");
    fx_draw(&f, &d);
    tui_dialog_free(&d);
    ASSERT_EQ_INT(tui_dialog_menu(&d, "x", NULL, 2, 0), -1, "items missing");
    fx_close(&f);
}

/* TUI-1: several rows ticked -- issue-manager's status filter. */
static void test_checklist(void) {
    static const char *const items[] = { "NEW", "APPROVED", "DEVELOPMENT", "CLOSED" };
    const unsigned char initial[] = { 1, 1, 1, 0 };     /* everything but CLOSED */
    tui_dialog d;
    ASSERT_EQ_INT(tui_dialog_checklist(&d, "Status", items, 4, initial, 0), 0, "create");
    ASSERT_EQ_INT(d.kind, TUI_DIALOG_CHECKLIST, "kind");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    fx_draw(&f, &d);
    const char *const expect[] = {
        "",
        "    ┌─ Status ──────────┐",
        "    │ [x] NEW           │",
        "    │ [x] APPROVED      │",
        "    │ [x] DEVELOPMENT   │",
        "    │ [ ] CLOSED        │",
        "    └───────────────────┘",
        "", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    const char *const map[] = { "A.SSSSSSSSSSSSSSSSS.A", "A...................A", NULL };
    ASSERT_REGION_STYLE(f.s, PTY_REGION(2, 4, 2, 21), map, legend);

    ASSERT_EQ_INT(send(&d, "\x1b[B "), TUI_DIALOG_OPEN, "Down, Space");
    ASSERT_EQ_INT(tui_dialog_is_checked(&d, 1), 0, "APPROVED unticked");
    ASSERT_EQ_INT(send(&d, "\x1b[B\x1b[B "), TUI_DIALOG_OPEN, "to CLOSED, Space");
    ASSERT_EQ_INT(tui_dialog_is_checked(&d, 3), 1, "CLOSED ticked");
    fx_draw(&f, &d);
    const char *const rows[] = { "│ [x] NEW           │", "│ [ ] APPROVED      │",
                                 "│ [x] DEVELOPMENT   │", "│ [x] CLOSED        │", NULL };
    ASSERT_REGION_EQ(f.s, PTY_REGION(2, 4, 4, 21), rows);
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OK, "Enter confirms");
    ASSERT_EQ_INT(d.answer, 3, "answer: the row Enter was pressed on");
    ASSERT(d.checked[0] && !d.checked[1] && d.checked[2] && d.checked[3], "the ticked set");
    ASSERT_EQ_INT(send(&d, " "), TUI_DIALOG_OK, "closed: Space ignored");
    ASSERT_EQ_INT(tui_dialog_is_checked(&d, 0), 1, "unchanged after close");
    ASSERT_EQ_INT(tui_dialog_is_checked(&d, 4), 0, "out of range");
    ASSERT_EQ_INT(tui_dialog_is_checked(&d, -1), 0, "negative");
    tui_dialog_free(&d);

    /* Cancelled: the caller's set is untouched (the dialog has a copy). */
    unsigned char mine[] = { 0, 1, 0, 0 };
    tui_dialog_checklist(&d, "Status", items, 4, mine, 0);
    ASSERT_EQ_INT(send(&d, "   \x1b"), TUI_DIALOG_CANCELLED, "Space three times, Esc");
    ASSERT(mine[0] == 0 && mine[1] == 1, "caller's set unchanged");
    tui_dialog_free(&d);

    /* No initial set, and an empty list. */
    tui_dialog_checklist(&d, "S", items, 4, NULL, 2);
    ASSERT_EQ_INT(tui_dialog_is_checked(&d, 2), 0, "nothing ticked");
    ASSERT_EQ_INT(d.list.selected, 2, "initial selection");
    tui_dialog_free(&d);
    ASSERT_EQ_INT(tui_dialog_checklist(&d, "Empty", NULL, 0, NULL, 0), 0, "empty checklist");
    ASSERT_EQ_INT(send(&d, " \r"), TUI_DIALOG_OPEN, "Space and Enter do nothing");
    fx_draw(&f, &d);
    tui_dialog_free(&d);
    ASSERT_EQ_INT(tui_dialog_checklist(&d, "x", NULL, 3, NULL, 0), -1, "items missing");
    tui_dialog_message(&d, "m", "x");
    ASSERT_EQ_INT(tui_dialog_is_checked(&d, 0), 0, "not a checklist");
    tui_dialog_free(&d);
    ASSERT_EQ_INT(tui_dialog_is_checked(NULL, 0), 0, "NULL");
    fx_close(&f);
}

/* Long text in a small terminal: wrapped, clipped, never outside. */
static void test_small_terminal(void) {
    tui_dialog d;
    tui_dialog_message(&d, "A rather long title", "This message is far longer than the "
                       "terminal is wide, so it has to be wrapped onto many lines.");
    Fx f;
    ASSERT(fx_open(&f, 6, 16) == 0, "fixtures");
    fx_draw(&f, &d);
    const char *const expect[] = {
        "",
        " ┌─ A rather…─┐",
        " │ This       │",
        " │   [ OK ]   │",
        " └────────────┘",
        "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    tui_dialog_free(&d);
    tui_dialog_render(NULL, tui_canvas_of(f.scr));
    tui_dialog_free(NULL);
    ASSERT_EQ_INT(tui_dialog_message(NULL, "x", "y"), -1, "NULL dialog");
    ASSERT_EQ_INT(tui_dialog_handle(NULL, NULL), TUI_DIALOG_CANCELLED, "NULL handle");
    fx_close(&f);
}

/* TUI-12: the help screen of policies/tui-interface-model.md -- a modal
 * overlay, two columns, the keys in bold, any key closes it. */
static void test_help(void) {
    static const char *const keys[] = {
        "?",                "this help",
        "j, \xE2\x86\x93",  "down one row",
        "q",                "back",
    };
    tui_dialog d;
    ASSERT_EQ_INT(tui_dialog_help(&d, "List shortcuts", keys, 3), 0, "create");
    ASSERT_EQ_INT(d.kind, TUI_DIALOG_HELP, "kind");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    fx_draw(&f, &d);
    const char *const expect[] = {
        "", "",
        "    ┌─ List shortcuts ───┐",
        "    │ ?     this help    │",
        "    │ j, ↓  down one row │",
        "    │ q     back         │",
        "    └────────────────────┘",
        "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    static const PtyStyleKey help_legend[] = {
        { '.', PTY_ATTR_NONE, 0, 0 },
        { 'A', PTY_ATTR_BOLD, PTY_FG_CYAN, PTY_BG_DEFAULT },      /* the frame */
        { 'K', PTY_ATTR_BOLD, PTY_FG_DEFAULT, PTY_BG_DEFAULT },   /* a key */
        { 0, 0, 0, 0 } };
    const char *const map[] = {
        "AAAAAAAAAAAAAAAAAAAAAA",
        "A.K..................A",
        "A.KKKK...............A",
        "A.K..................A",
        "AAAAAAAAAAAAAAAAAAAAAA", NULL };
    ASSERT_REGION_STYLE(f.s, PTY_REGION(2, 4, 5, 22), map, help_legend);
    int r, c;
    ASSERT_EQ_INT(pty_cursor(f.s, &r, &c), 0, "no cursor in the help");

    /* TUI-18: Esc, q and Backspace go back; other keys do nothing. */
    static const char *const ignored[] = { "j", "x", "\r", " ", "?", "\t", "G", "\x1b[6~" };
    for (size_t i = 0; i < sizeof(ignored) / sizeof(ignored[0]); i++) {
        ASSERT_EQ_INT(send(&d, ignored[i]), TUI_DIALOG_OPEN, "another key leaves it open");
        ASSERT_EQ_INT(d.list.scroll_top, 0, "and it all fits: nothing scrolls");
    }
    ASSERT_EQ_INT(send(&d, "q"), TUI_DIALOG_OK, "q closes it");
    ASSERT_EQ_INT(send(&d, "x"), TUI_DIALOG_OK, "closed stays closed");
    tui_dialog_free(&d);
    static const char *const backs[] = { "\x1b", "\x7f", "\x03" };
    static const char *const what[] = { "Esc closes it", "Backspace closes it", "Ctrl-C closes it" };
    for (int i = 0; i < 3; i++) {
        tui_dialog_help(&d, "Keys", keys, 3);
        fx_draw(&f, &d);
        ASSERT_EQ_INT(send(&d, backs[i]), TUI_DIALOG_OK, what[i]);
        tui_dialog_free(&d);
    }
    tui_dialog_help(&d, "Keys", keys, 3);
    tui_event resize;
    memset(&resize, 0, sizeof(resize));
    resize.type = TUI_EV_RESIZE;
    ASSERT_EQ_INT(tui_dialog_handle(&d, &resize), TUI_DIALOG_OPEN, "not a key: stays open");
    ASSERT_EQ_INT(send(&d, "j"), TUI_DIALOG_OPEN, "never drawn: j does nothing");
    ASSERT_EQ_INT(d.list.scroll_top, 0, "nothing scrolled before it was drawn");
    ASSERT_EQ_INT(send(&d, "\x08"), TUI_DIALOG_OK, "0x08 is Backspace on some terminals: closes");
    tui_dialog_free(&d);

    /* Longer than the screen: the movement keys scroll it, others close. */
    static const char *const many[] = {
        "a", "one", "b", "two", "c", "three", "d", "four", "e", "five", "f", NULL };
    Fx g;
    ASSERT(fx_open(&g, 7, 20) == 0, "fixtures");
    tui_dialog_help(&d, "Many", many, 6);
    fx_draw(&g, &d);
    const char *const top[] = {
        "", "    ┌─ Many ───┐", "    │ a  one   │", "    │ b  two   │", "    │ c  three │",
        "    └──────────┘", "", NULL };
    ASSERT_SCREEN_EQ(g.s, top);
    static const struct { const char *keys; int top; const char *what; } moves[] = {
        { "j", 1, "j scrolls a row" },        { "\x1b[B", 2, "Down" },
        { "\x1b[B", 3, "Down to the end" },   { "j", 3, "no further" },
        { "k", 2, "k" },                       { "\x1b[A", 1, "Up" },
        { "G", 3, "G: the end" },              { "g", 0, "g: the top" },
        { "\x1b[6~", 2, "PgDn: a page less one row" },
        { "\x1b[5~", 0, "PgUp" },              { "\x1b[F", 3, "End" },
        { "\x1b[H", 0, "Home" },               { "\x06", 2, "ctrl-f" },
        { "\x02", 0, "ctrl-b" },               { "\x1b[A", 0, "Up at the top" },
    };
    for (size_t i = 0; i < sizeof(moves) / sizeof(moves[0]); i++) {
        ASSERT_EQ_INT(send(&d, moves[i].keys), TUI_DIALOG_OPEN, moves[i].what);
        ASSERT_EQ_INT(d.list.scroll_top, moves[i].top, moves[i].what);
    }
    send(&d, "G");
    fx_draw(&g, &d);
    const char *const bottom[] = {
        "", "    ┌─ Many ───┐", "    │ d  four  │", "    │ e  five  │", "    │ f        │",
        "    └──────────┘", "", NULL };
    ASSERT_SCREEN_EQ(g.s, bottom);
    ASSERT_EQ_INT(send(&d, "x"), TUI_DIALOG_OPEN, "a scrolled help: x does nothing");
    ASSERT_EQ_INT(d.list.scroll_top, 3, "and does not scroll");
    ASSERT_EQ_INT(send(&d, "q"), TUI_DIALOG_OK, "q closes it");
    tui_dialog_free(&d);

    /* A taller screen after scrolling: the window is pulled back in. */
    tui_dialog_help(&d, "Many", many, 6);
    fx_draw(&g, &d);
    send(&d, "G");
    Fx big;
    ASSERT(fx_open(&big, 10, 20) == 0, "fixtures");
    fx_draw(&big, &d);
    ASSERT_EQ_INT(d.list.scroll_top, 0, "everything fits: from the top");
    ASSERT_EQ_INT(send(&d, "j"), TUI_DIALOG_OPEN, "j does not close it");
    ASSERT_EQ_INT(d.list.scroll_top, 0, "nor scrolls what fits");
    ASSERT_EQ_INT(send(&d, "\x7f"), TUI_DIALOG_OK, "Backspace closes it");
    tui_dialog_free(&d);
    fx_close(&big);

    /* No rows; errors. */
    tui_dialog_help(&d, "Nothing", NULL, 0);
    fx_draw(&g, &d);
    ASSERT_EQ_INT(send(&d, " "), TUI_DIALOG_OPEN, "an empty help: Space does nothing");
    ASSERT_EQ_INT(send(&d, "\x1b"), TUI_DIALOG_OK, "Esc closes it");
    tui_dialog_free(&d);
    errno = 0;
    ASSERT_EQ_INT(tui_dialog_help(&d, "x", NULL, 2), -1, "pairs missing");
    ASSERT_EQ_INT(errno, EINVAL, "errno");
    ASSERT_EQ_INT(tui_dialog_help(&d, "x", keys, -1), -1, "negative count");
    ASSERT_EQ_INT(tui_dialog_help(NULL, "x", keys, 1), -1, "no dialog");
    fx_close(&g);
    fx_close(&f);
}

static const TestCase tests[] = {
    TEST_CASE(test_message),
    TEST_CASE(test_confirm),
    TEST_CASE(test_prompt),
    TEST_CASE(test_menu),
    TEST_CASE(test_checklist),
    TEST_CASE(test_help),
    TEST_CASE(test_small_terminal),
};

int main(int argc, char **argv) {
    return RUN_SUITE("dialog", tests, 50, 60);
}
