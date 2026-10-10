/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-05 */
/* Unit tests of tui_textedit. */

/*
 * Unit tests of tui_textedit (TUI-2).  The contract of issue-manager's own
 * text-area widget is checked first -- newlines and tabs kept, CR LF folded,
 * truncation at a boundary, the keys it leaves to the program, Ctrl-K --
 * then what the library adds: display widths, tab stops, scrolling in both
 * directions, the summary.  What it draws is fed to libptytest's emulator.
 */

#include "test_helpers.h"
#include "tuikit/textedit.h"

int g_tests_run = 0;
int g_tests_failed = 0;

static int send(tui_textedit *te, const char *keys) {
    tui_decoder d;
    tui_decoder_init(&d);
    tui_decoder_feed(&d, keys, strlen(keys));
    tui_event ev;
    int r = -1;
    while (tui_decoder_next(&d, &ev)) r = tui_textedit_handle(te, &ev);
    if (tui_decoder_timeout(&d, &ev)) r = tui_textedit_handle(te, &ev);
    tui_decoder_free(&d);
    return r;
}

#define ASSERT_CURSOR_AT(te, row, col, what) do { \
    int _r, _c; \
    tui_textedit_cursor(te, &_r, &_c); \
    char _w[160]; \
    snprintf(_w, sizeof(_w), "%s: cursor row", what); ASSERT_EQ_INT(_r, row, _w); \
    snprintf(_w, sizeof(_w), "%s: cursor column", what); ASSERT_EQ_INT(_c, col, _w); \
} while (0)

/* ── issue-manager's contract ────────────────────────────────────────── */

static void test_init_and_insert(void) {
    char buf[64];
    tui_textedit te;
    tui_textedit_init(&te, buf, sizeof(buf), "first\nsecond");
    ASSERT_EQ_STR(buf, "first\nsecond", "initial text");
    ASSERT_EQ_INT(te.cur, 0, "cursor at the start");
    ASSERT_EQ_INT(te.dirty, 0, "not dirty");
    ASSERT_EQ_INT(tui_textedit_lines(&te), 2, "two lines");

    tui_textedit_set_text(&te, "");
    ASSERT_EQ_INT(tui_textedit_lines(&te), 1, "an empty text has one line");
    ASSERT_EQ_INT(tui_textedit_insert(&te, "a\r\nb\rc\td", 8), 7, "bytes inserted");
    ASSERT_EQ_STR(buf, "a\nb\nc\td", "CR LF and CR become newlines, tab kept");
    ASSERT_EQ_INT(te.dirty, 1, "an insertion is an edit");
    tui_textedit_set_text(&te, "x\x01y\x1bz\x7f");
    ASSERT_EQ_STR(buf, "xyz", "other controls dropped");
    ASSERT_EQ_INT(te.dirty, 0, "set_text is not an edit");
    tui_textedit_set_text(&te, "\xFF\xEF\xBF\xBD");
    ASSERT_EQ_STR(buf, "\xEF\xBF\xBD\xEF\xBF\xBD", "malformed byte as U+FFFD, a real U+FFFD kept");

    char small[8];
    tui_textedit_init(&te, small, sizeof(small), NULL);
    ASSERT_EQ_INT(tui_textedit_insert(&te, "ab\xE4\xB8\xAD\xE6\x96\x87", 8), 5, "what fits");
    ASSERT_EQ_STR(small, "ab\xE4\xB8\xAD", "cut at a character boundary");
    ASSERT_EQ_INT(te.truncated, 1, "truncated is set");
    tui_textedit_insert(&te, "", 0);
    ASSERT_EQ_INT(te.truncated, 0, "cleared by the next insertion");

    ASSERT_EQ_INT(tui_textedit_insert(NULL, "x", 1), 0, "NULL editor");
    ASSERT_EQ_INT(tui_textedit_insert(&te, NULL, 1), 0, "NULL text");
    tui_textedit_init(&te, NULL, 0, "x");
    ASSERT_EQ_INT(tui_textedit_lines(&te), 0, "no buffer: no lines");
    ASSERT_EQ_INT(send(&te, "a"), 0, "no buffer: nothing handled");
    tui_textedit_set_text(&te, "x");
    tui_textedit_init(NULL, buf, sizeof(buf), NULL);
    tui_textedit_set_text(NULL, "x");
}

static void test_keys_left_to_the_program(void) {
    char buf[64];
    tui_textedit te;
    tui_textedit_init(&te, buf, sizeof(buf), "text");
    ASSERT_EQ_INT(send(&te, "\x1b"), 0, "Esc");
    ASSERT_EQ_INT(send(&te, "\x13"), 0, "Ctrl-S (save)");
    ASSERT_EQ_INT(send(&te, "\x1b[Z"), 0, "Shift-Tab");
    ASSERT_EQ_INT(send(&te, "\x1bx"), 0, "Alt-x");
    ASSERT_EQ_INT(send(&te, "\x1b\r"), 0, "Alt-Enter");
    ASSERT_EQ_INT(send(&te, "\x1b[2~"), 0, "Insert");
    ASSERT_EQ_INT(send(&te, "\x1b[15~"), 0, "F5");
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_RESIZE;
    ASSERT_EQ_INT(tui_textedit_handle(&te, &ev), 0, "not a key");
    ASSERT_EQ_INT(tui_textedit_handle(&te, NULL), 0, "NULL event");
    ASSERT_EQ_STR(buf, "text", "the text is untouched");
    ASSERT_EQ_INT(te.dirty, 0, "and not dirty");
}

static void test_typing_newlines_and_deleting(void) {
    char buf[64];
    tui_textedit te;
    tui_textedit_init(&te, buf, sizeof(buf), NULL);
    ASSERT_EQ_INT(send(&te, "one\rtwo\tx"), 1, "typed");
    ASSERT_EQ_STR(buf, "one\ntwo\tx", "Enter is a newline, Tab a tab");
    ASSERT_CURSOR_AT(&te, 1, 9, "after the tab and x");
    send(&te, "\x1b[H");
    ASSERT_CURSOR_AT(&te, 1, 0, "Home");
    send(&te, "\x7f");
    ASSERT_EQ_STR(buf, "onetwo\tx", "Backspace at a line's start joins the lines");
    ASSERT_CURSOR_AT(&te, 0, 3, "between one and two");
    send(&te, "\r");
    send(&te, "\x1b[A\x1b[F");
    ASSERT_CURSOR_AT(&te, 0, 3, "End of the first line");
    send(&te, "\x1b[3~");
    ASSERT_EQ_STR(buf, "onetwo\tx", "Delete at a line's end joins the lines");
    send(&te, "\x01");
    ASSERT_CURSOR_AT(&te, 0, 0, "Ctrl-A");
    send(&te, "\x05");
    ASSERT_CURSOR_AT(&te, 0, 9, "Ctrl-E");
    send(&te, "\x1b[H\x1b[C\x1b[C\x1b[C\x0b");
    ASSERT_EQ_STR(buf, "one", "Ctrl-K: the rest of the line");
    send(&te, "\rmore\x1b[A\x1b[F\x0b");
    ASSERT_EQ_STR(buf, "onemore", "Ctrl-K at a line's end: the newline");
    send(&te, "\x1b[1;5F");
    ASSERT_EQ_INT(te.cur, strlen(buf), "Ctrl+End: the end of the text");
    send(&te, "\x1b[1;5H");
    ASSERT_EQ_INT(te.cur, 0, "Ctrl+Home: the start of the text");
    send(&te, "\x1b[D\x7f");
    ASSERT_EQ_STR(buf, "onemore", "Left and Backspace at the start: nothing");
    send(&te, "\x1b[1;5F\x1b[C\x1b[3~");
    ASSERT_EQ_STR(buf, "onemore", "Right and Delete at the end: nothing");
    ASSERT_EQ_INT(send(&te, "\x0c"), 0, "another Ctrl key is the program's");

    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_PASTE;
    ev.paste = "\r\npasted\tline";
    ev.paste_len = 13;
    ASSERT_EQ_INT(tui_textedit_handle(&te, &ev), 1, "paste");
    ASSERT_EQ_STR(buf, "onemore\npasted\tline", "pasted as it is");
    ev.paste = NULL;
    ev.paste_len = 0;
    ASSERT_EQ_INT(tui_textedit_handle(&te, &ev), 1, "an empty paste");
}

/* ── What the library adds ───────────────────────────────────────────── */

static void test_columns_and_vertical_movement(void) {
    char buf[128];
    tui_textedit te;
    /* Line 0: a tab then x; line 1: 中文 (four columns); line 2: e + mark. */
    tui_textedit_init(&te, buf, sizeof(buf), "\tx\n\xE4\xB8\xAD\xE6\x96\x87yz\ne\xCC\x81" "f\nlong line here");
    send(&te, "\x1b[C");
    ASSERT_CURSOR_AT(&te, 0, 8, "a tab reaches column 8");
    send(&te, "\x1b[C");
    ASSERT_CURSOR_AT(&te, 0, 9, "after x");
    send(&te, "\x1b[B");
    ASSERT_CURSOR_AT(&te, 1, 6, "Down: the end of the shorter line");
    send(&te, "\x1b[B");
    ASSERT_CURSOR_AT(&te, 2, 2, "Down again: still shorter");
    send(&te, "\x1b[B");
    ASSERT_CURSOR_AT(&te, 3, 9, "the column aimed for came back");
    send(&te, "\x1b[A\x1b[A\x1b[A");
    ASSERT_CURSOR_AT(&te, 0, 9, "and back up");
    send(&te, "\x1b[B\x1b[H\x1b[C");
    ASSERT_CURSOR_AT(&te, 1, 2, "Right over a wide character: two columns");
    send(&te, "\x1b[B");
    ASSERT_CURSOR_AT(&te, 2, 2, "column 2 is after e+mark and f");
    send(&te, "\x1b[D");
    ASSERT_CURSOR_AT(&te, 2, 1, "a combining mark takes no column");
    ASSERT_EQ_INT(te.cur, (size_t)(strstr(buf, "f") - buf), "before f");
    send(&te, "\x1b[D");
    ASSERT_CURSOR_AT(&te, 2, 0, "e and its mark are one character");
    send(&te, "\x1b[C");
    ASSERT_EQ_INT(te.cur, (size_t)(strstr(buf, "f") - buf), "Right steps over e and its mark");
    send(&te, "\x7f");
    ASSERT_EQ_STR(buf, "\tx\n\xE4\xB8\xAD\xE6\x96\x87yz\nf\nlong line here",
                  "Backspace removes e with its mark");
    send(&te, "\x1b[1;5H\x1b[A");
    ASSERT_CURSOR_AT(&te, 0, 0, "Up on the first line");
    send(&te, "\x1b[1;5F\x1b[B");
    ASSERT_CURSOR_AT(&te, 3, 14, "Down on the last line");
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
    return f->scr && f->s ? 0 : -1;
}

static void fx_draw(Fx *f, tui_textedit *te, tui_rect r) {
    tui_canvas all = tui_canvas_of(f->scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_textedit_render(te, tui_canvas_sub(all, r), TUI_STYLE_DEFAULT);
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    if (f->s) pty_close(f->s);
}

static void test_render_and_scroll(void) {
    char buf[512];
    tui_textedit te;
    tui_textedit_init(&te, buf, sizeof(buf),
                      "Title\n\tindented\nwide \xE4\xB8\xAD\xE6\x96\x87 text\n"
                      "line 4\nline 5\nline 6\nline 7");
    Fx f;
    ASSERT(fx_open(&f, 5, 14) == 0, "fixtures");
    tui_rect area = tui_rect_make(1, 1, 3, 12);
    fx_draw(&f, &te, area);
    const char *const expect[] = {
        "", " Title", "         inde", " wide \xE4\xB8\xAD\xE6\x96\x87 te", "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);
    int r, c;
    pty_cursor(f.s, &r, &c);
    ASSERT_EQ_INT(r, 1, "cursor row on the screen");
    ASSERT_EQ_INT(c, 1, "cursor column on the screen");

    /* Down to the last line: the view scrolls with it. */
    send(&te, "\x1b[1;5F");
    fx_draw(&f, &te, area);
    const char *const bottom[] = { "", " line 5", " line 6", " line 7", "", NULL };
    ASSERT_SCREEN_EQ(f.s, bottom);
    ASSERT_EQ_INT(te.top, 4, "scrolled to line 4");
    ASSERT_EQ_INT(te.rows, 3, "the height drawn");

    /* PgUp moves by that height. */
    send(&te, "\x1b[5~");
    ASSERT_CURSOR_AT(&te, 3, 6, "PgUp: three lines up");
    send(&te, "\x1b[6~\x1b[6~");
    ASSERT_CURSOR_AT(&te, 6, 6, "PgDn: clamped at the last line");

    /* A long line: the view scrolls right to keep the cursor. */
    send(&te, "\x1b[1;5H\x1b[B\x1b[F");
    fx_draw(&f, &te, area);
    ASSERT_EQ_INT(te.left, 5, "scrolled right (16 columns in a 12-wide view)");
    ASSERT_EQ_INT(te.top, 1, "scrolled up just to the cursor's line");
    const char *const right[] = { "", "    indented", " \xE4\xB8\xAD\xE6\x96\x87 text", " 4", "", NULL };
    ASSERT_SCREEN_EQ(f.s, right);
    pty_cursor(f.s, &r, &c);
    ASSERT_EQ_INT(r, 1, "cursor on the indented line");
    ASSERT_EQ_INT(c, 12, "after its last character");
    send(&te, "\x1b[H");
    fx_draw(&f, &te, area);
    ASSERT_EQ_INT(te.left, 0, "back to the left edge");

    /* Nothing to draw into. */
    tui_textedit_render(&te, tui_canvas_sub(tui_canvas_of(f.scr), tui_rect_make(9, 9, 1, 1)),
                        TUI_STYLE_DEFAULT);
    tui_textedit_render(NULL, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    tui_textedit_cursor(NULL, &r, NULL);
    ASSERT_EQ_INT(r, 0, "cursor of NULL");
    fx_close(&f);
}

static void test_summary(void) {
    char out[64];
    ASSERT_EQ_INT(tui_textedit_summary("one line", 20, out, sizeof(out)), 8, "one line");
    ASSERT_EQ_STR(out, "one line", "unchanged");
    ASSERT_EQ_INT(tui_textedit_summary("first\nsecond\nthird", 30, out, sizeof(out)), 15,
                  "several lines");
    ASSERT_EQ_STR(out, "first (3 lines)", "the first line and the count");
    ASSERT_EQ_INT(tui_textedit_summary("a long first line\nx", 14, out, sizeof(out)), 14, "cut");
    ASSERT_EQ_STR(out, "a l\xE2\x80\xA6 (2 lines)", "cut with an ellipsis");
    ASSERT_EQ_INT(tui_textedit_summary("abc\nd", 6, out, sizeof(out)), 6, "only the count fits");
    ASSERT_EQ_STR(out, "(2 li\xE2\x80\xA6", "the count, cut");
    ASSERT_EQ_INT(tui_textedit_summary("", 10, out, sizeof(out)), 0, "empty text");
    ASSERT_EQ_STR(out, "", "empty");
    ASSERT_EQ_INT(tui_textedit_summary(NULL, 10, out, sizeof(out)), 0, "NULL text");
    ASSERT_EQ_INT(tui_textedit_summary("x", 0, out, sizeof(out)), 0, "no columns");
    ASSERT_EQ_INT(tui_textedit_summary("x", 5, NULL, 0), -1, "no buffer");
    char tiny[4];
    ASSERT_EQ_INT(tui_textedit_summary("abcdef", 10, tiny, sizeof(tiny)), -1, "head too big");
    char eight[12];
    ASSERT_EQ_INT(tui_textedit_summary("ab\ncd", 40, eight, sizeof(eight)), -1, "tail too big");
    char huge[1500];
    memset(huge, 'h', sizeof(huge) - 1);
    huge[sizeof(huge) - 1] = '\0';
    ASSERT_EQ_INT(tui_textedit_summary(huge, 30, out, sizeof(out)), 30, "a very long first line");
}

static const TestCase tests[] = {
    TEST_CASE(test_init_and_insert),
    TEST_CASE(test_keys_left_to_the_program),
    TEST_CASE(test_typing_newlines_and_deleting),
    TEST_CASE(test_columns_and_vertical_movement),
    TEST_CASE(test_render_and_scroll),
    TEST_CASE(test_summary),
};

int main(int argc, char **argv) {
    return RUN_SUITE("textedit", tests, 100, 60);
}
