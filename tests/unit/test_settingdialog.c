/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* Unit tests of tui_settingdialog (TUI-138). */

/*
 * What the setting dialog does with keys (focus ring, OK, Cancel, Esc,
 * validation) and what it draws: the bytes through libptytest's emulator,
 * cell by cell, colours included.
 */

#include "test_helpers.h"
#include "tuikit/settingdialog.h"
#include "faults.h"

#include <errno.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static tui_dialog_state send(tui_settingdialog *d, const char *keys) {
    tui_decoder dec;
    tui_decoder_init(&dec);
    tui_decoder_feed(&dec, keys, strlen(keys));
    tui_event ev;
    tui_dialog_state st = d->state;
    while (tui_decoder_next(&dec, &ev)) st = tui_settingdialog_handle(d, &ev);
    if (tui_decoder_timeout(&dec, &ev)) st = tui_settingdialog_handle(d, &ev);
    tui_decoder_free(&dec);
    return st;
}

static int validate(void *user, const char *value, char *err, size_t errsz) {
    (*(int *)user)++;
    if (strcmp(value, "bad") == 0) {
        snprintf(err, errsz, "bad is not allowed");
        return 1;
    }
    if (strcmp(value, "silent") == 0) return 1;
    return 0;
}

typedef struct { tui_screen *scr; tui_buf buf; PtySession *s; } Fx;

static void fx_draw(Fx *f, tui_settingdialog *d, int rows, int cols) {
    f->scr = tui_screen_new(rows, cols);
    tui_buf_init(&f->buf);
    f->s = pty_open(cols, rows);
    tui_canvas all = tui_canvas_of(f->scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_settingdialog_render(d, all);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    pty_close(f->s);
}

static int find(PtySession *s, int rows, int cols, const char *text, int *row, int *col) {
    size_t n = strlen(text);
    for (int r = 0; r < rows; r++)
        for (int c = 0; c + (int)n <= cols; c++) {
            size_t k = 0;
            while (k < n && strcmp(pty_cell_text(s, r, c + (int)k), (char[2]){ text[k], 0 }) == 0) k++;
            if (k == n) {
                *row = r;
                *col = c;
                return 0;
            }
        }
    return -1;
}

static void test_ok_keeps_value(void) {
    char buf[32];
    tui_settingdialog d;
    ASSERT_EQ_INT(tui_settingdialog_init(&d, "Port", "The port.", "1-65535", buf, sizeof buf, "80"), 0, "init");
    ASSERT_EQ_INT(d.state, TUI_DIALOG_OPEN, "open");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_FIELD, "focus on the input");
    send(&d, "81");
    ASSERT_EQ_STR(buf, "8081", "typed into the buffer");
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OK, "Enter in the input presses OK");
    ASSERT_EQ_STR(buf, "8081", "the value stays");
    ASSERT_EQ_INT(send(&d, "x"), TUI_DIALOG_OK, "events after the close are ignored");
    ASSERT_EQ_STR(buf, "8081", "unchanged after close");
    tui_settingdialog_free(&d);
}

static void test_focus_ring_and_buttons(void) {
    char buf[32];
    tui_settingdialog d;
    tui_settingdialog_init(&d, "T", "text", NULL, buf, sizeof buf, "a");
    send(&d, "\t");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_OK, "Tab: OK");
    send(&d, "\t");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_CANCEL, "Tab: Cancel");
    send(&d, "\t");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_FIELD, "Tab: around to the input");
    send(&d, "\x1b[Z");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_CANCEL, "Shift-Tab: back around");
    send(&d, "\x1b[D");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_OK, "Left: OK");
    send(&d, "\x1b[C");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_CANCEL, "Right: Cancel");
    send(&d, "\x1b[C");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_OK, "Right: OK again");
    send(&d, "\x1b[B\x1b[A\x1b[6~\x1b[5~");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_OK, "scroll keys keep the focus");
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OK, "Enter on OK");
    tui_settingdialog_free(&d);

    tui_settingdialog_init(&d, "T", "text", NULL, buf, sizeof buf, "a");
    send(&d, "bc\t\t");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_CANCEL, "on Cancel");
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_CANCELLED, "Enter on Cancel");
    ASSERT_EQ_STR(buf, "a", "Cancel puts the initial value back");
    tui_settingdialog_free(&d);
}

static void test_esc_cancels(void) {
    char buf[32];
    tui_settingdialog d;
    tui_settingdialog_init(&d, "T", "text", NULL, buf, sizeof buf, "keep");
    send(&d, "zz");
    ASSERT_EQ_INT(send(&d, "\x1b"), TUI_DIALOG_CANCELLED, "Esc in the input");
    ASSERT_EQ_STR(buf, "keep", "value put back");
    tui_settingdialog_free(&d);
    tui_settingdialog_init(&d, "T", "text", NULL, buf, sizeof buf, "keep");
    send(&d, "\t");
    ASSERT_EQ_INT(send(&d, "\x1b"), TUI_DIALOG_CANCELLED, "Esc on a button");
    tui_settingdialog_free(&d);
    tui_settingdialog_init(&d, "T", "text", NULL, buf, sizeof buf, "keep");
    ASSERT_EQ_INT(send(&d, "\x03"), TUI_DIALOG_CANCELLED, "Ctrl-C");
    tui_settingdialog_free(&d);
}

static void test_validation_refuses(void) {
    char buf[32];
    int calls = 0;
    tui_settingdialog d;
    tui_settingdialog_init(&d, "T", "text", NULL, buf, sizeof buf, "bad");
    d.validate = validate;
    d.user = &calls;
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OPEN, "refused: stays open");
    ASSERT_EQ_INT(calls, 1, "validator called");
    ASSERT_EQ_STR(d.error, "bad is not allowed", "the reason");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_FIELD, "focus on the input");
    /* From the OK button: the focus goes back to the input. */
    send(&d, "\t");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_OK, "on OK");
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OPEN, "refused again");
    ASSERT_EQ_INT(d.focus, TUI_SETTING_FIELD, "focus goes back to the field");
    send(&d, "\x1b[D");   /* a cursor move does not clear the reason */
    ASSERT_EQ_STR(d.error, "bad is not allowed", "movement keeps the reason");
    send(&d, "x");
    ASSERT_EQ_STR(d.error, "", "typing clears the reason");
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OK, "valid: closes");
    tui_settingdialog_free(&d);

    tui_settingdialog_init(&d, "T", "text", NULL, buf, sizeof buf, "silent");
    d.validate = validate;
    d.user = &calls;
    ASSERT_EQ_INT(send(&d, "\r"), TUI_DIALOG_OPEN, "refused without a reason");
    ASSERT_EQ_STR(d.error, "Invalid value", "a default reason");
    tui_settingdialog_free(&d);
}

static void test_render_layout_and_colours(void) {
    char buf[32];
    int calls = 0;
    tui_settingdialog d;
    tui_settingdialog_init(&d, "Listen port", "The TCP port the server listens on.\nChange needs a restart.",
                           "1-65535", buf, sizeof buf, "8080");
    d.validate = validate;
    d.user = &calls;
    Fx f;
    fx_draw(&f, &d, 24, 80);
    int tr, tc, dr, dc, ir, ic, hr, hc, okr, okc, cr, cc;
    ASSERT_EQ_INT(find(f.s, 24, 80, "Listen port", &tr, &tc), 0, "title");
    ASSERT_EQ_INT(find(f.s, 24, 80, "The TCP port", &dr, &dc), 0, "description");
    ASSERT_EQ_INT(find(f.s, 24, 80, "8080", &ir, &ic), 0, "the value");
    ASSERT_EQ_INT(find(f.s, 24, 80, "1-65535", &hr, &hc), 0, "the hint");
    ASSERT_EQ_INT(find(f.s, 24, 80, "[ OK ]", &okr, &okc), 0, "OK button");
    ASSERT_EQ_INT(find(f.s, 24, 80, "[ Cancel ]", &cr, &cc), 0, "Cancel button");
    ASSERT(tr < dr && dr < ir && ir < hr && hr < okr, "order: title, description, input, hint, buttons");
    ASSERT_EQ_INT(okr, cr, "the buttons share a row");
    ASSERT_EQ_STR(pty_cell_text(f.s, tr, tc - 3), "┌", "the title is in the top border");
    int er = hr + 1;
    ASSERT(okr > er, "a row for the reason sits between the hint and the buttons");
    int pr, pc;
    pty_cursor(f.s, &pr, &pc);
    ASSERT_EQ_INT(pr, ir, "the cursor is in the input row");
    ASSERT_EQ_INT(pc, ic + 4, "after the value");
    ASSERT(pty_cell_attr(f.s, hr, hc) & PTY_ATTR_DIM, "the hint is dimmed");
    ASSERT_EQ_INT(pty_cell_fg(f.s, tr, tc), PTY_FG_CYAN, "the title in the frame colour");
    /* Both buttons are centred in the box together. */
    ASSERT_EQ_INT(cc + 10 - okc, 18, "the buttons are 18 cells together");
    ASSERT_EQ_INT(pty_cell_bg(f.s, cr, cc + 2), pty_cell_bg(f.s, dr, dc), "Cancel is not highlighted");
    fx_close(&f);

    /* A refusal is drawn in the warning role. */
    tui_lineedit_set_text(&d.edit, "bad");
    send(&d, "\r");
    ASSERT(d.error[0], "refused");
    fx_draw(&f, &d, 24, 80);
    int wr, wc;
    ASSERT_EQ_INT(find(f.s, 24, 80, "bad is not allowed", &wr, &wc), 0, "the reason is shown");
    ASSERT(wr > hr - 1, "below the input");
    tui_style warn = tui_scheme_style(TUI_ROLE_WARNING);
    ASSERT(warn.fg != 0, "the scheme has a warning colour");
    ASSERT(pty_cell_fg(f.s, wr, wc) != pty_cell_fg(f.s, dr, dc), "not in the text colour");
    ASSERT_EQ_INT(pty_cell_attr(f.s, wr, wc) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "bold, as the warning role is");
    fx_close(&f);

    /* Focus on a button: the cursor is gone and the button is highlighted. */
    send(&d, "x\t");     /* typing clears the reason; Tab to OK */
    ASSERT_EQ_INT(d.focus, TUI_SETTING_OK, "on OK");
    fx_draw(&f, &d, 24, 80);
    ASSERT_EQ_INT(find(f.s, 24, 80, "[ OK ]", &okr, &okc), 0, "OK still there");
    ASSERT(!pty_screen_contains(f.s, "bad is not allowed"), "the reason is gone");
    ASSERT(pty_cell_bg(f.s, okr, okc + 2) != pty_cell_bg(f.s, okr, okc - 6) ||
           (pty_cell_attr(f.s, okr, okc + 2) & PTY_ATTR_REVERSE), "OK is highlighted");
    ASSERT_EQ_INT(pty_cursor(f.s, &pr, &pc), 0, "no cursor on a button");
    fx_close(&f);
    tui_settingdialog_free(&d);
}

static void test_render_wraps_and_small(void) {
    char buf[16];
    tui_settingdialog d;
    char longtext[400] = "";
    for (int i = 0; i < 20; i++) strcat(longtext, "word number which wraps and wraps. ");
    tui_settingdialog_init(&d, "A title", longtext, "", buf, sizeof buf, "v");
    Fx f;
    fx_draw(&f, &d, 24, 80);
    int ir, ic, okr, okc;
    ASSERT_EQ_INT(find(f.s, 24, 80, "[ OK ]", &okr, &okc), 0, "buttons are always there");
    ASSERT(okr <= 22, "inside the screen less the margin");
    ASSERT_EQ_INT(find(f.s, 24, 80, "word number", &ir, &ic), 0, "the description is wrapped in");
    fx_close(&f);
    /* Too small: no box, one warning line. */
    fx_draw(&f, &d, 5, 40);
    ASSERT(pty_screen_contains(f.s, "Terminal too small for this dialog"), "too small");
    ASSERT(!pty_screen_contains(f.s, "[ OK ]"), "no box");
    fx_close(&f);
    fx_draw(&f, &d, 24, 14);
    ASSERT(pty_screen_contains(f.s, "Terminal too small for this dialog") ||
           pty_screen_contains(f.s, "Terminal too"), "too narrow");
    fx_close(&f);
    /* A tall description scrolls under the keys on a button. */
    fx_draw(&f, &d, 12, 60);
    ASSERT(pty_screen_contains(f.s, "[ OK ]"), "fits with a short description window");
    fx_close(&f);
    tui_screen *tmp = tui_screen_new(3, 10);
    tui_settingdialog_render(NULL, tui_canvas_of(tmp));
    tui_screen_free(tmp);
    tui_settingdialog_free(&d);
}

static void test_args_and_faults(void) {
    char buf[8];
    tui_settingdialog d;
    errno = 0;
    ASSERT_EQ_INT(tui_settingdialog_init(NULL, "t", "x", NULL, buf, sizeof buf, ""), -1, "no dialog");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_settingdialog_init(&d, "t", "x", NULL, NULL, 4, ""), -1, "no buffer");
    ASSERT_EQ_INT(tui_settingdialog_init(&d, "t", "x", NULL, buf, 0, ""), -1, "no size");
    ASSERT_EQ_INT(tui_settingdialog_init(&d, NULL, NULL, NULL, buf, sizeof buf, NULL), 0, "all NULL is fine");
    ASSERT_EQ_STR(buf, "", "no initial: empty");
    tui_settingdialog_free(&d);
    tui_settingdialog_free(NULL);
    ASSERT_EQ_INT(tui_settingdialog_handle(NULL, NULL), TUI_DIALOG_CANCELLED, "no dialog: cancelled");
    for (int skip = 0; skip < 4; skip++) {
        fault_arm(F_MALLOC, skip, 1, ENOMEM);
        errno = 0;
        int r = tui_settingdialog_init(&d, "t", "x", "h", buf, sizeof buf, "v");
        fault_reset();
        ASSERT_EQ_INT(r, -1, "malloc fails");
        ASSERT_EQ_INT(errno, ENOMEM, "ENOMEM");
    }
    tui_settingdialog_init(&d, "t", "x", NULL, buf, sizeof buf, "");
    ASSERT_EQ_INT(tui_settingdialog_handle(&d, NULL), TUI_DIALOG_OPEN, "no event");
    tui_settingdialog_free(&d);
}

static const TestCase tests[] = {
    TEST_CASE(test_ok_keeps_value),
    TEST_CASE(test_focus_ring_and_buttons),
    TEST_CASE(test_esc_cancels),
    TEST_CASE(test_validation_refuses),
    TEST_CASE(test_render_layout_and_colours),
    TEST_CASE(test_render_wraps_and_small),
    TEST_CASE(test_args_and_faults),
};

int main(int argc, char **argv) {
    return RUN_SUITE("settingdialog", tests, 80, 60);
}
