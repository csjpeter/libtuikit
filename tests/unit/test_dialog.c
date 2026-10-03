/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

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

static const PtyStyleKey legend[] = {
    { '.', PTY_ATTR_NONE, 0, 0 }, { 'R', PTY_ATTR_REVERSE, 0, 0 }, { 0, 0, 0, 0 } };

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
    const char *const map[] = { "...RRRRRR...", NULL };
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
    const char *const map_no[] = { "...........RRRRRR..", NULL };
    ASSERT_REGION_STYLE(f.s, PTY_REGION(5, 5, 1, 19), map_no, legend);
    ASSERT_EQ_INT(send(&d, "\x1b[D"), TUI_DIALOG_OPEN, "Left");
    ASSERT_EQ_INT(d.answer, 1, "focus on Yes");
    fx_draw(&f, &d);
    const char *const map_yes[] = { "..RRRRRRR..........", NULL };
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
    const char *const map[] = { "..............", "..RRRRRRRRRR..", "..............", NULL };
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

static const TestCase tests[] = {
    TEST_CASE(test_message),
    TEST_CASE(test_confirm),
    TEST_CASE(test_prompt),
    TEST_CASE(test_menu),
    TEST_CASE(test_small_terminal),
};

int main(int argc, char **argv) {
    return RUN_SUITE("dialog", tests, 50, 60);
}
