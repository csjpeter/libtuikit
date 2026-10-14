/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-14 */
/* Unit tests of tui_statusline: hints that fit, message, wait, position, prompt, colours. */

/*
 * The line is rendered, fed to libptytest's emulator and compared cell by
 * cell: the characters, and the reverse video of the bar, the bold keys and
 * the colour of a message (policies/tui-interface-model.md).
 */

#include "test_helpers.h"
#include "tuikit/statusline.h"
#include "tuikit/text.h"

#include <errno.h>
#include <stdlib.h>

int g_tests_run = 0;
int g_tests_failed = 0;

typedef struct {
    tui_screen *scr;
    tui_buf     buf;
    PtySession *s;
    int         rows, cols;
} Fx;

static int fx_open(Fx *f, int rows, int cols) {
    f->rows = rows;
    f->cols = cols;
    f->scr = tui_screen_new(rows, cols);
    tui_buf_init(&f->buf);
    f->s = pty_open(cols, rows);
    return (f->scr && f->s) ? 0 : -1;
}

static void fx_show(Fx *f, tui_statusline *sl) {
    tui_screen_clear(f->scr, TUI_STYLE_DEFAULT);
    tui_statusline_render(sl, tui_canvas_of(f->scr));
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    if (f->s) pty_close(f->s);
}

static const char *last_row(Fx *f) {
    static char buf[512];
    return pty_row_text(f->s, f->rows - 1, buf, sizeof(buf));
}

static const char *fmt(const tui_statusline *sl, int cols) {
    static char buf[512];
    int n = tui_statusline_format(sl, cols, buf, sizeof(buf));
    if (n < 0) { g_tests_failed++; printf("FAIL: format did not fit\n"); }
    return buf;
}

static tui_event key(uint32_t cp) {
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = TUI_KEY_CHAR;
    ev.cp = cp;
    int n = tui_utf8_encode(cp, ev.utf8);
    ev.utf8[n] = '\0';
    return ev;
}

static tui_event special(tui_key k) {
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = k;
    return ev;
}

static const tui_statusline_hint g_hints[] = {
    { "Enter", "open",   5 },
    { "/",     "filter", 4 },
    { "d",     "delete", 2 },
    { "q",     "quit",   9 },
    { "",      NULL,     7 },   /* nothing to show */
    { "r",     NULL,     1 },   /* a key alone */
};
#define NH 6

/* ── Hints ──────────────────────────────────────────────────────────── */

static void test_hints_full_line(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    ASSERT_EQ_STR(fmt(&sl, 20), " ? help", "no hints: the help hint alone");
    tui_statusline_set_hints(&sl, g_hints, NH);
    ASSERT_EQ_STR(fmt(&sl, 80), " Enter open  / filter  d delete  q quit  r  ? help",
                  "all hints in order, two blanks apart, help last");
    ASSERT_EQ_INT(tui_statusline_hint_count(&sl), NH, "the full set");
    ASSERT_EQ_STR(tui_statusline_hint_at(&sl, 3)->label, "quit", "hint_at");
    ASSERT(tui_statusline_hint_at(&sl, NH) == NULL, "out of range");
    ASSERT(tui_statusline_hint_at(&sl, -1) == NULL, "negative");
    ASSERT_EQ_INT(tui_statusline_hints_fit(&sl, 80), 5, "five of six have something to show");
    tui_statusline_free(&sl);
}

static void test_hints_drop_by_priority(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_hints(&sl, g_hints, NH);
    /* priorities: q 9, Enter 5, / 4, d 2, r 1 */
    ASSERT_EQ_STR(fmt(&sl, 51), " Enter open  / filter  d delete  q quit  r  ? help", "51 columns: all");
    ASSERT_EQ_STR(fmt(&sl, 50), " Enter open  / filter  d delete  q quit  ? help",
                  "50: the lowest priority goes first, whole");
    ASSERT_EQ_STR(fmt(&sl, 41), " Enter open  / filter  q quit  ? help", "41: d goes too");
    tui_statusline_free(&sl);
}

static void test_hints_degrade_below_80(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_hints(&sl, g_hints, NH);
    ASSERT_EQ_INT(tui_statusline_hints_fit(&sl, 38), 3, "38 columns: three hints");
    ASSERT_EQ_STR(fmt(&sl, 38), " Enter open  / filter  q quit  ? help", "exactly");
    ASSERT_EQ_INT(tui_statusline_hints_fit(&sl, 35), 2, "35 columns: two");
    ASSERT_EQ_STR(fmt(&sl, 35), " Enter open  q quit  ? help", "/ goes before Enter");
    ASSERT_EQ_INT(tui_statusline_hints_fit(&sl, 24), 1, "24 columns: only the top one");
    ASSERT_EQ_STR(fmt(&sl, 24), " q quit  ? help", "q is kept longest");
    ASSERT_EQ_INT(tui_statusline_hints_fit(&sl, 8), 0, "none");
    ASSERT_EQ_STR(fmt(&sl, 8), " ? help", "the help hint stays");
    ASSERT_EQ_STR(fmt(&sl, 7), " ? he\xE2\x80\xA6", "help is cut only when it is all that is left");
    ASSERT_EQ_STR(fmt(&sl, 2), "", "no room at all");
    ASSERT_EQ_STR(fmt(&sl, 0), "", "zero columns");
    ASSERT_EQ_INT(tui_statusline_hints_fit(NULL, 80), 0, "NULL line");
    tui_statusline_free(&sl);
}

static void test_help_hint_options(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_hints(&sl, g_hints, 1);
    tui_statusline_set_help(&sl, "F1", NULL);
    ASSERT_EQ_STR(fmt(&sl, 40), " Enter open  F1", "a key without a label");
    tui_statusline_set_help(&sl, NULL, NULL);
    ASSERT_EQ_STR(fmt(&sl, 40), " Enter open", "no help hint");
    ASSERT_EQ_STR(fmt(&sl, 4), "", "and nothing fits");
    tui_statusline_set_help(&sl, "?", "help");
    static tui_statusline_hint many[TUI_STATUSLINE_HINTS_MAX + 1];
    ASSERT_EQ_INT(tui_statusline_set_hints(&sl, many, TUI_STATUSLINE_HINTS_MAX + 1), -1, "too many");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_statusline_hint_count(&sl), 1, "unchanged");
    ASSERT_EQ_INT(tui_statusline_set_hints(&sl, NULL, 0), 0, "cleared");
    ASSERT_EQ_INT(tui_statusline_hint_count(&sl), 0, "none");
    ASSERT_EQ_INT(tui_statusline_set_hints(NULL, g_hints, 1), -1, "NULL line");
    tui_statusline_free(&sl);
}

/* ── Message ────────────────────────────────────────────────────────── */

static void test_message_replaces_hints_and_is_cut(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_hints(&sl, g_hints, NH);
    tui_statusline_set_message(&sl, "Saved 3 messages", TUI_STYLE_OK);
    ASSERT_EQ_STR(fmt(&sl, 80), " Saved 3 messages", "the message replaces the hints");
    ASSERT_EQ_STR(tui_statusline_message(&sl), "Saved 3 messages", "readable");
    ASSERT_EQ_STR(fmt(&sl, 10), " Saved 3\xE2\x80\xA6", "cut with an ellipsis, a blank kept at the right edge");
    tui_statusline_set_message(&sl, "\xE4\xBD\xA0\xE5\xA5\xBD\xE4\xB8\x96\xE7\x95\x8C", TUI_STYLE_ERROR);
    ASSERT_EQ_STR(fmt(&sl, 7), " \xE4\xBD\xA0\xE5\xA5\xBD\xE2\x80\xA6", "a wide character is not cut by halves");
    tui_statusline_set_message(&sl, "a\tb\nc\x1b[31md", TUI_STYLE_ERROR);
    ASSERT_EQ_STR(tui_statusline_message(&sl), "a b c [31md", "controls become spaces");
    tui_statusline_clear_message(&sl);
    ASSERT_EQ_STR(fmt(&sl, 80), " Enter open  / filter  d delete  q quit  r  ? help", "hints come back");
    tui_statusline_set_message(&sl, NULL, TUI_STYLE_OK);
    ASSERT_EQ_STR(tui_statusline_message(&sl), "", "NULL clears");
    tui_statusline_free(&sl);
}

static void test_message_limit_is_a_character_boundary(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    char big[400];
    memset(big, 'x', sizeof(big));
    big[254] = '\0';
    strcat(big, "\xC3\xA9\xC3\xA9");   /* é at bytes 254-255: would be cut inside */
    tui_statusline_set_message(&sl, big, TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT((int)strlen(tui_statusline_message(&sl)), 254, "the half character is not kept");
    ASSERT_EQ_INT(tui_str_width(tui_statusline_message(&sl)), 254, "valid UTF-8");
    tui_statusline_free(&sl);
}

static void test_key_clears_message(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_message(&sl, "Failed: no route", TUI_STYLE_ERROR);
    tui_event rz;
    memset(&rz, 0, sizeof(rz));
    rz.type = TUI_EV_RESIZE;
    rz.rows = 24;
    rz.cols = 80;
    ASSERT_EQ_INT(tui_statusline_handle(&sl, &rz), TUI_STATUSLINE_IGNORED, "a resize is not a key");
    ASSERT_EQ_STR(tui_statusline_message(&sl), "Failed: no route", "and keeps the message");
    tui_event k = key('j');
    ASSERT_EQ_INT(tui_statusline_handle(&sl, &k), TUI_STATUSLINE_CHANGED, "a key clears it");
    ASSERT_EQ_STR(tui_statusline_message(&sl), "", "cleared");
    ASSERT_EQ_INT(tui_statusline_handle(&sl, &k), TUI_STATUSLINE_IGNORED, "nothing left to clear: the program's");
    tui_statusline_set_message(&sl, "x", TUI_STYLE_OK);
    tui_event paste;
    memset(&paste, 0, sizeof(paste));
    paste.type = TUI_EV_PASTE;
    paste.paste = "abc";
    paste.paste_len = 3;
    ASSERT_EQ_INT(tui_statusline_handle(&sl, &paste), TUI_STATUSLINE_CHANGED, "a paste clears it too");
    ASSERT_EQ_INT(tui_statusline_handle(NULL, &k), TUI_STATUSLINE_IGNORED, "NULL line");
    ASSERT_EQ_INT(tui_statusline_handle(&sl, NULL), TUI_STATUSLINE_IGNORED, "NULL event");
    tui_statusline_free(&sl);
}

/* ── Position, wait ─────────────────────────────────────────────────── */

static void test_position_is_always_kept(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_hints(&sl, g_hints, NH);
    tui_statusline_set_position_index(&sl, 3, 120);
    ASSERT_EQ_STR(fmt(&sl, 80),
                  " Enter open  / filter  d delete  q quit  r  ? help                      [3/120]",
                  "right-aligned, a blank before the edge");
    ASSERT_EQ_STR(fmt(&sl, 30), " q quit  ? help       [3/120]",
                  "hints are dropped to make room for it");
    tui_statusline_set_message(&sl, "a long message that has to be cut short", TUI_STYLE_DEFAULT);
    ASSERT_EQ_STR(fmt(&sl, 24), " a long messag\xE2\x80\xA6 [3/120]", "the message is cut, the position stays");
    tui_statusline_clear_message(&sl);
    tui_statusline_set_position(&sl, "page 2/7");
    ASSERT_EQ_STR(fmt(&sl, 30), " q quit  ? help      page 2/7", "free text");
    tui_statusline_set_position(&sl, "a very long position text indeed");
    ASSERT_EQ_STR(fmt(&sl, 12), " a very lo\xE2\x80\xA6", "wider than the line: cut itself, nothing else");
    ASSERT_EQ_STR(fmt(&sl, 2), "", "no room");
    tui_statusline_set_position_index(&sl, 1, 0);
    ASSERT_EQ_STR(fmt(&sl, 20), " q quit  ? help", "total below 1 clears it");
    tui_statusline_set_position(&sl, NULL);
    tui_statusline_free(&sl);
}

static void test_wait_text(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_hints(&sl, g_hints, NH);
    tui_statusline_set_wait(&sl, "Syncing INBOX ...", 0, 0);
    ASSERT_EQ_STR(fmt(&sl, 60), " Syncing INBOX ...", "the wait text replaces the hints");
    tui_statusline_set_wait(&sl, "Syncing INBOX ...", 12, 340);
    ASSERT_EQ_STR(fmt(&sl, 60), " Syncing INBOX ... 12/340", "with the progress");
    ASSERT_EQ_STR(fmt(&sl, 20), " Syncing IN\xE2\x80\xA6 12/340", "the text gives way, the numbers stay");
    ASSERT_EQ_STR(fmt(&sl, 8), " Synci\xE2\x80\xA6", "no room for the numbers: dropped");
    tui_statusline_set_message(&sl, "Mail from X", TUI_STYLE_DEFAULT);
    ASSERT_EQ_STR(fmt(&sl, 60), " Mail from X", "a message goes before the wait text");
    tui_event k = key('x');
    tui_statusline_handle(&sl, &k);
    ASSERT_EQ_STR(fmt(&sl, 60), " Syncing INBOX ... 12/340", "a key does not end the wait");
    tui_statusline_clear_wait(&sl);
    ASSERT_EQ_STR(fmt(&sl, 28), " Enter open  q quit  ? help", "hints again");
    tui_statusline_set_wait(&sl, "w", 0, 0);
    tui_statusline_set_wait(&sl, "", 0, 0);
    ASSERT_EQ_INT(tui_statusline_hints_fit(&sl, 80), 5, "an empty text clears it");
    tui_statusline_free(&sl);
}

/* ── Prompt ─────────────────────────────────────────────────────────── */

static void test_prompt(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_hints(&sl, g_hints, NH);
    tui_statusline_set_position_index(&sl, 2, 9);
    tui_statusline_begin_prompt(&sl, "/", "ab");
    ASSERT_EQ_INT(tui_statusline_prompt_active(&sl), 1, "open");
    ASSERT_EQ_STR(fmt(&sl, 30), " /ab                    [2/9]", "prompt and text, position kept");
    tui_event c = key('c');
    ASSERT_EQ_INT(tui_statusline_handle(&sl, &c), TUI_STATUSLINE_CHANGED, "typed");
    ASSERT_EQ_STR(tui_statusline_prompt_text(&sl), "abc", "text");
    tui_event f5 = special(TUI_KEY_F5);
    ASSERT_EQ_INT(tui_statusline_handle(&sl, &f5), TUI_STATUSLINE_IGNORED, "not an editing key");
    tui_event en = special(TUI_KEY_ENTER);
    ASSERT_EQ_INT(tui_statusline_handle(&sl, &en), TUI_STATUSLINE_ACCEPT, "Enter");
    ASSERT_EQ_INT(tui_statusline_prompt_active(&sl), 0, "closed");
    ASSERT_EQ_STR(tui_statusline_prompt_text(&sl), "abc", "text kept after Enter");
    ASSERT_EQ_STR(fmt(&sl, 20), " ? help       [2/9]", "hints are back");

    tui_statusline_begin_prompt(&sl, "filter: ", NULL);
    ASSERT_EQ_STR(tui_statusline_prompt_text(&sl), "", "a new prompt starts empty");
    tui_statusline_set_message(&sl, "msg", TUI_STYLE_OK);
    ASSERT_EQ_STR(fmt(&sl, 30), " filter:                [2/9]", "the prompt goes before a message");
    tui_event esc = special(TUI_KEY_ESC);
    ASSERT_EQ_INT(tui_statusline_handle(&sl, &esc), TUI_STATUSLINE_CANCEL, "Esc");
    ASSERT_EQ_INT(tui_statusline_prompt_active(&sl), 0, "closed");
    ASSERT_EQ_STR(tui_statusline_message(&sl), "msg", "the message was not touched by the prompt");

    tui_statusline_begin_prompt(&sl, NULL, "x");
    tui_event cd = key('d');
    cd.mods = TUI_MOD_CTRL;
    tui_event bs = special(TUI_KEY_BACKSPACE);
    tui_statusline_handle(&sl, &bs);
    ASSERT_EQ_INT(tui_statusline_handle(&sl, &cd), TUI_STATUSLINE_CANCEL, "Ctrl-D on an empty line leaves");
    ASSERT_EQ_INT(tui_statusline_prompt_active(&sl), 0, "closed");
    tui_statusline_begin_prompt(&sl, "/", NULL);
    tui_statusline_free(&sl);
    ASSERT_EQ_INT(tui_statusline_prompt_active(NULL), 0, "NULL line");
    ASSERT_EQ_STR(tui_statusline_prompt_text(NULL), "", "NULL line text");
}

/* ── The picture ────────────────────────────────────────────────────── */

static void test_render_bar_and_styles(void) {
    Fx f;
    ASSERT(fx_open(&f, 5, 40) == 0, "fixtures");
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_hints(&sl, g_hints, 2);
    fx_show(&f, &sl);
    ASSERT_EQ_STR(last_row(&f), " Enter open  / filter  ? help", "text of the last row");
    for (int c = 0; c < 40; c++)
        ASSERT_EQ_INT(pty_cell_attr(f.s, 4, c) & PTY_ATTR_REVERSE, PTY_ATTR_REVERSE, "the whole row is reverse video");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 4, 1) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "a key is bold");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 4, 7) & PTY_ATTR_BOLD, 0, "a label is not");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 3, 1) & PTY_ATTR_REVERSE, 0, "the row above is the terminal's own");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 3, 1), PTY_COLOR_DEFAULT, "default background elsewhere");

    tui_statusline_set_message(&sl, "Could not save", TUI_STYLE_ERROR);
    fx_show(&f, &sl);
    ASSERT_EQ_INT(strncmp(last_row(&f), " Could not save", 15), 0, "the message");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 4, 1) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "error: bold");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 4, 1) & PTY_ATTR_REVERSE, PTY_ATTR_REVERSE, "on the bar");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 4, 1), 31, "red");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 4, 30), 31, "the bar carries the colour to the edge");

    tui_statusline_set_message(&sl, "Saved", TUI_STYLE_OK);
    fx_show(&f, &sl);
    ASSERT_EQ_INT(pty_cell_fg(f.s, 4, 1), 32, "ok: green");
    tui_statusline_clear_message(&sl);

    tui_statusline_set_wait(&sl, "Syncing ...", 1, 2);
    fx_show(&f, &sl);
    ASSERT_EQ_INT(strncmp(last_row(&f), " Syncing ... 1/2", 16), 0, "the wait text");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 4, 1) & PTY_ATTR_ITALIC, PTY_ATTR_ITALIC, "progress: italic");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 4, 1), 33, "yellow");
    tui_statusline_clear_wait(&sl);

    tui_statusline_set_position(&sl, "[3/120]");
    fx_show(&f, &sl);
    ASSERT_EQ_STR(pty_cell_text(f.s, 4, 32), "[", "the position at the right, a blank before the edge");
    ASSERT_EQ_STR(pty_cell_text(f.s, 4, 38), "]", "ends at column 38 of 40");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 4, 39) & PTY_ATTR_REVERSE, PTY_ATTR_REVERSE, "bar to the edge");
    tui_statusline_free(&sl);
    fx_close(&f);
}

static void test_render_prompt_cursor_and_resize(void) {
    Fx f;
    ASSERT(fx_open(&f, 4, 30) == 0, "fixtures");
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_position(&sl, "[1/2]");
    tui_statusline_begin_prompt(&sl, "/", "abc");
    fx_show(&f, &sl);
    ASSERT_EQ_INT(strncmp(last_row(&f), " /abc", 5), 0, "prompt and text");
    int r = -1, c = -1;
    ASSERT_EQ_INT(pty_cursor(f.s, &r, &c), 1, "the cursor is shown");
    ASSERT_EQ_INT(r, 3, "on the status row");
    ASSERT_EQ_INT(c, 5, "after the text: lead 1 + '/' + 'abc'");
    ASSERT_EQ_STR(pty_cell_text(f.s, 3, 24), "[", "position kept");
    for (int i = 0; i < 40; i++) { tui_event ch = key('z'); tui_statusline_handle(&sl, &ch); }
    fx_show(&f, &sl);
    pty_cursor(f.s, &r, &c);
    ASSERT(c < 23, "the text scrolls, the cursor stays left of the position");
    ASSERT_EQ_STR(pty_cell_text(f.s, 3, 24), "[", "position still there");
    tui_statusline_free(&sl);
    fx_close(&f);

    /* A resize: the same model at another width, nothing else to do. */
    ASSERT(fx_open(&f, 6, 100) == 0, "fixtures");
    tui_statusline_init(&sl);
    tui_statusline_set_hints(&sl, g_hints, NH);
    fx_show(&f, &sl);
    ASSERT_EQ_STR(last_row(&f), " Enter open  / filter  d delete  q quit  r  ? help", "wide");
    tui_screen_resize(f.scr, 6, 24);
    pty_resize(f.s, 24, 6);
    fx_show(&f, &sl);
    ASSERT_EQ_STR(last_row(&f), " q quit  ? help", "narrow: hints dropped, nothing wrapped");
    tui_statusline_free(&sl);
    fx_close(&f);
}

static void test_render_edge_cases(void) {
    Fx f;
    ASSERT(fx_open(&f, 3, 20) == 0, "fixtures");
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_render(NULL, tui_canvas_of(f.scr));
    tui_canvas none = tui_canvas_sub(tui_canvas_of(f.scr), tui_rect_make(0, 0, 0, 0));
    tui_statusline_render(&sl, none);
    /* A canvas that is not the screen: the last row of it. */
    tui_canvas top = tui_canvas_sub(tui_canvas_of(f.scr), tui_rect_make(0, 0, 2, 20));
    tui_statusline_set_message(&sl, "in a pane", TUI_STYLE_DEFAULT);
    tui_screen_clear(f.scr, TUI_STYLE_DEFAULT);
    tui_statusline_render(&sl, top);
    tui_buf_reset(&f.buf);
    tui_screen_render(f.scr, &f.buf);
    pty_feed(f.s, f.buf.data, f.buf.len);
    char b[64];
    ASSERT_EQ_STR(pty_row_text(f.s, 1, b, sizeof(b)), " in a pane", "the last row of the canvas");
    ASSERT_EQ_STR(pty_row_text(f.s, 2, b, sizeof(b)), "", "nothing below it");
    /* A prompt on a line so narrow that the editor has no room. */
    tui_statusline_begin_prompt(&sl, "/", "q");
    tui_canvas thin = tui_canvas_sub(tui_canvas_of(f.scr), tui_rect_make(2, 0, 1, 2));
    tui_statusline_render(&sl, thin);
    tui_statusline_free(&sl);
    fx_close(&f);
}

static void test_format_limits(void) {
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_hints(&sl, g_hints, NH);
    tui_statusline_set_position(&sl, "[1/9]");
    char small[8];
    ASSERT_EQ_INT(tui_statusline_format(&sl, 80, small, sizeof(small)), -1, "too small");
    ASSERT_EQ_STR(small, " Enter ", "cut and terminated");
    char one[1];
    ASSERT_EQ_INT(tui_statusline_format(&sl, 80, one, sizeof(one)), -1, "room for the NUL only");
    ASSERT_EQ_STR(one, "", "terminated");
    ASSERT_EQ_INT(tui_statusline_format(&sl, 80, NULL, 10), -1, "NULL out");
    ASSERT_EQ_INT(tui_statusline_format(&sl, 80, small, 0), -1, "zero size");
    ASSERT_EQ_INT(tui_statusline_format(NULL, 80, small, sizeof(small)), -1, "NULL line");
    /* The cut never leaves half a character. */
    tui_statusline_set_message(&sl, "\xC3\xA9\xC3\xA9\xC3\xA9", TUI_STYLE_DEFAULT);
    char t[5];
    ASSERT_EQ_INT(tui_statusline_format(&sl, 80, t, sizeof(t)), -1, "cut");
    ASSERT_EQ_STR(t, " \xC3\xA9", "whole characters only");
    ASSERT_EQ_INT(tui_statusline_format(&sl, 80, t, sizeof(t)), -1, "again");
    tui_statusline_init(NULL);
    tui_statusline_free(NULL);
    tui_statusline_set_help(NULL, "?", "help");
    tui_statusline_set_message(NULL, "x", TUI_STYLE_DEFAULT);
    tui_statusline_clear_message(NULL);
    tui_statusline_set_wait(NULL, "x", 0, 0);
    tui_statusline_clear_wait(NULL);
    tui_statusline_set_position(NULL, "x");
    tui_statusline_set_position_index(NULL, 1, 2);
    tui_statusline_begin_prompt(NULL, "/", NULL);
    ASSERT_EQ_INT(tui_statusline_hint_count(NULL), 0, "NULL count");
    ASSERT(tui_statusline_hint_at(NULL, 0) == NULL, "NULL hint");
    ASSERT_EQ_STR(tui_statusline_message(NULL), "", "NULL message");
    tui_statusline_free(&sl);
}

static const TestCase tests[] = {
    TEST_CASE(test_hints_full_line),
    TEST_CASE(test_hints_drop_by_priority),
    TEST_CASE(test_hints_degrade_below_80),
    TEST_CASE(test_help_hint_options),
    TEST_CASE(test_message_replaces_hints_and_is_cut),
    TEST_CASE(test_message_limit_is_a_character_boundary),
    TEST_CASE(test_key_clears_message),
    TEST_CASE(test_position_is_always_kept),
    TEST_CASE(test_wait_text),
    TEST_CASE(test_prompt),
    TEST_CASE(test_render_bar_and_styles),
    TEST_CASE(test_render_prompt_cursor_and_resize),
    TEST_CASE(test_render_edge_cases),
    TEST_CASE(test_format_limits),
};

int main(int argc, char **argv) {
    return RUN_SUITE("statusline", tests, 140, 60);
}
