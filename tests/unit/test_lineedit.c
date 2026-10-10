/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Unit tests of tui_lineedit, tui_history and tui_readline. */

/*
 * Unit tests of tui_lineedit, tui_history, tui_lineedit_run() and
 * tui_readline().  The editing model is driven with events; the two loops
 * are driven through a real pseudo-terminal pair with the keys typed in
 * advance, and what they wrote is fed to libptytest's emulator.
 */

#include "test_helpers.h"
#include "tty_fixture.h"
#include "tuikit/lineedit.h"
#include "tuikit/tty.h"

#include <errno.h>
#include <termios.h>

int g_tests_run = 0;
int g_tests_failed = 0;

/* Feeds @p keys through a decoder into the editor; returns the result of
 * the last event. */
static tui_edit_result type(tui_lineedit *le, const char *keys) {
    tui_decoder d;
    tui_decoder_init(&d);
    tui_decoder_feed(&d, keys, strlen(keys));
    tui_event ev;
    tui_edit_result r = TUI_EDIT_IGNORED;
    while (tui_decoder_next(&d, &ev)) r = tui_lineedit_handle(le, &ev);
    if (tui_decoder_timeout(&d, &ev)) r = tui_lineedit_handle(le, &ev);
    tui_decoder_free(&d);
    return r;
}

static void test_history(void) {
    tui_history h;
    tui_history_init(&h, 3);
    ASSERT_EQ_INT(tui_history_count(&h), 0, "empty");
    ASSERT_EQ_INT(tui_history_add(&h, "one"), 0, "add");
    ASSERT_EQ_INT(tui_history_add(&h, "one"), 0, "duplicate of the last");
    ASSERT_EQ_INT(tui_history_add(&h, ""), 0, "empty line");
    ASSERT_EQ_INT(tui_history_count(&h), 1, "duplicate and empty not added");
    tui_history_add(&h, "two");
    tui_history_add(&h, "three");
    tui_history_add(&h, "four");
    ASSERT_EQ_INT(tui_history_count(&h), 3, "capped");
    ASSERT_EQ_STR(tui_history_get(&h, 0), "two", "oldest dropped");
    ASSERT_EQ_STR(tui_history_get(&h, 2), "four", "newest last");
    ASSERT(tui_history_get(&h, 3) == NULL, "out of range");
    ASSERT(tui_history_get(&h, -1) == NULL, "negative");
    ASSERT_EQ_INT(tui_history_add(&h, NULL), -1, "NULL line");
    ASSERT_EQ_INT(tui_history_add(NULL, "x"), -1, "NULL history");
    ASSERT_EQ_INT(tui_history_count(NULL), 0, "count of NULL");
    tui_history_free(&h);
    ASSERT_EQ_INT(tui_history_count(&h), 0, "freed");
    tui_history_init(&h, 0);
    ASSERT_EQ_INT(h.max, 256, "default capacity");
    tui_history_free(&h);
    tui_history_init(NULL, 1);
    tui_history_free(NULL);
}

static void test_insert_and_move(void) {
    char buf[64];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), "h\xC3\xA9llo");     /* héllo */
    ASSERT_EQ_STR(buf, "h\xC3\xA9llo", "initial text");
    ASSERT_EQ_INT(le.cur, le.len, "cursor at the end (email-cli)");
    ASSERT_EQ_INT(tui_lineedit_cursor_col(&le), 5, "cursor column");
    type(&le, "\x1b[D\x1b[D\x1b[D\x1b[D");                        /* 4 x Left */
    ASSERT_EQ_INT(le.cur, 1, "after h");
    type(&le, "\xC5\x91");                                         /* ő */
    ASSERT_EQ_STR(buf, "h\xC5\x91\xC3\xA9llo", "ő inserted as its bytes (email-cli fix)");
    type(&le, "\x7f\x7f");
    ASSERT_EQ_STR(buf, "\xC3\xA9llo", "Backspace removes whole characters");
    type(&le, "\x1b[3~");
    ASSERT_EQ_STR(buf, "llo", "Delete removes é whole");
    type(&le, "\x1b[F");
    ASSERT_EQ_INT(le.cur, 3, "End");
    type(&le, "\x01");
    ASSERT_EQ_INT(le.cur, 0, "Ctrl-A");
    type(&le, "\x05");
    ASSERT_EQ_INT(le.cur, 3, "Ctrl-E");
    type(&le, "\x02\x02");
    ASSERT_EQ_INT(le.cur, 1, "Ctrl-B twice");
    type(&le, "\x06");
    ASSERT_EQ_INT(le.cur, 2, "Ctrl-F");
    type(&le, "\x1b[H");
    ASSERT_EQ_INT(le.cur, 0, "Home");
    type(&le, "\x1b[D");
    ASSERT_EQ_INT(le.cur, 0, "Left at the start");
    type(&le, "\x1b[F\x1b[C");
    ASSERT_EQ_INT(le.cur, 3, "Right at the end");
    tui_lineedit_free(&le);
}

static void test_combining_moves_with_base(void) {
    char buf[64];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), "ae\xCC\x81z");      /* a é(e+mark) z */
    ASSERT_EQ_INT(tui_lineedit_cursor_col(&le), 3, "three columns");
    type(&le, "\x1b[D\x1b[D");
    ASSERT_EQ_INT(le.cur, 1, "Left skips the mark with its base");
    type(&le, "\x1b[C");
    ASSERT_EQ_INT(le.cur, 4, "Right steps over e and its mark");
    type(&le, "\x7f");
    ASSERT_EQ_STR(buf, "az", "Backspace removes e and its mark");
    tui_lineedit_free(&le);
}

static void test_words_and_kill_yank(void) {
    char buf[64];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), "one two  three");
    type(&le, "\x1b[1;5D");
    ASSERT_EQ_INT(le.cur, 9, "Ctrl+Left: start of three");
    type(&le, "\x1b" "b");
    ASSERT_EQ_INT(le.cur, 4, "Alt-b: start of two");
    type(&le, "\x1b[1;5C");
    ASSERT_EQ_INT(le.cur, 7, "Ctrl+Right: end of two");
    type(&le, "\x1b" "f");
    ASSERT_EQ_INT(le.cur, 14, "Alt-f: end of three");
    type(&le, "\x17");
    ASSERT_EQ_STR(buf, "one two  ", "Ctrl-W kills the word before");
    type(&le, "\x19");
    ASSERT_EQ_STR(buf, "one two  three", "Ctrl-Y yanks it back");
    type(&le, "\x1b[H\x1b[C\x1b[C\x1b[C");
    type(&le, "\x0b");
    ASSERT_EQ_STR(buf, "one", "Ctrl-K kills to the end");
    type(&le, "\x1b[D");
    type(&le, "\x15");
    ASSERT_EQ_STR(buf, "e", "Ctrl-U kills to the start");
    ASSERT_EQ_INT(le.cur, 0, "cursor at the start");
    type(&le, "\x19");
    ASSERT_EQ_STR(buf, "one", "Ctrl-Y yanks the last kill");
    type(&le, "\x1b\x7f");
    ASSERT_EQ_STR(buf, "e", "Alt-Backspace kills the word before");
    type(&le, "\x08");
    ASSERT_EQ_STR(buf, "e", "Ctrl-H at the start: nothing");
    tui_lineedit_free(&le);
}

static void test_results(void) {
    char buf[16];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), NULL);
    ASSERT_EQ_INT(type(&le, "\x04"), TUI_EDIT_EOF, "Ctrl-D on an empty line");
    ASSERT_EQ_INT(type(&le, "ab\x01\x04"), TUI_EDIT_CHANGED, "Ctrl-D deletes forward");
    ASSERT_EQ_STR(buf, "b", "deleted");
    ASSERT_EQ_INT(type(&le, "\r"), TUI_EDIT_ACCEPT, "Enter");
    ASSERT_EQ_INT(type(&le, "\x03"), TUI_EDIT_CANCEL, "Ctrl-C");
    ASSERT_EQ_INT(type(&le, "\x1b"), TUI_EDIT_CANCEL, "Esc");
    ASSERT_EQ_INT(type(&le, "\x1b[5~"), TUI_EDIT_IGNORED, "PgUp is the program's");
    ASSERT_EQ_INT(type(&le, "\x1b[A"), TUI_EDIT_IGNORED, "Up without history");
    ASSERT_EQ_INT(type(&le, "\t"), TUI_EDIT_IGNORED, "Tab without a hook");
    ASSERT_EQ_INT(type(&le, "\x1b[Z"), TUI_EDIT_IGNORED, "Shift-Tab without a hook");
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_RESIZE;
    ASSERT_EQ_INT(tui_lineedit_handle(&le, &ev), TUI_EDIT_IGNORED, "not a key");
    ASSERT_EQ_INT(tui_lineedit_handle(NULL, &ev), TUI_EDIT_IGNORED, "NULL editor");
    tui_lineedit_free(&le);
}

static void test_capacity_and_controls(void) {
    char buf[6];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), "abcdefgh");
    ASSERT_EQ_STR(buf, "abcde", "initial text truncated to the buffer");
    type(&le, "x");
    ASSERT_EQ_STR(buf, "abcde", "full: nothing inserted");
    type(&le, "\x7f\x7f\x7f");
    type(&le, "\xE4\xB8\xAD\xE4\xB8\xAD");     /* two 3-byte characters, room for one */
    ASSERT_EQ_STR(buf, "ab\xE4\xB8\xAD", "never half a character");
    tui_lineedit_set_text(&le, "a\tb\nc\x1b");
    ASSERT_EQ_STR(buf, "a b c", "tab and newline become spaces, ESC dropped");
    tui_lineedit_set_text(&le, "\xFF");
    ASSERT_EQ_STR(buf, "\xEF\xBF\xBD", "malformed byte stored as U+FFFD");
    ASSERT_EQ_INT(tui_lineedit_insert(&le, NULL), 0, "insert NULL");
    tui_lineedit_free(&le);

    /* Paste: inserted as text, newlines as spaces. */
    char big[64];
    tui_lineedit_init(&le, big, sizeof(big), NULL);
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_PASTE;
    ev.paste = "two\nlines";
    ev.paste_len = 9;
    ASSERT_EQ_INT(tui_lineedit_handle(&le, &ev), TUI_EDIT_CHANGED, "paste");
    ASSERT_EQ_STR(big, "two lines", "pasted");
    tui_lineedit_free(&le);
    tui_lineedit_init(&le, NULL, 0, "x");
    ASSERT_EQ_INT(le.size, 0, "no buffer");
    ASSERT_EQ_INT(type(&le, "a"), TUI_EDIT_IGNORED, "no buffer: nothing to edit");
    tui_lineedit_init(NULL, big, 4, NULL);
}

/* A character the terminal reported with Shift (CSI u) is text too. */
static void test_shifted_character_and_scroll_back(void) {
    char buf[64];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), NULL);
    ASSERT_EQ_INT(type(&le, "\x1b[65;2u"), TUI_EDIT_CHANGED, "Shift+A");
    ASSERT_EQ_STR(buf, "A", "inserted");
    /* A long line scrolled to its end, then shortened: the field fills
     * again from the left rather than showing empty columns. */
    tui_lineedit_set_text(&le, "abcdefghijklmnopqrstuvwxyz");
    tui_screen *scr = tui_screen_new(1, 10);
    tui_canvas row = tui_canvas_of(scr);
    tui_lineedit_render(&le, row, "> ", TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(le.scroll, 19, "scrolled to show the end");
    type(&le, "\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f\x7f");
    tui_lineedit_render(&le, row, "> ", TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(le.scroll, 9, "scrolled back as the text shrank");
    type(&le, "\x01");
    tui_lineedit_render(&le, row, "> ", TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(le.scroll, 0, "Home: from the start");
    tui_screen_free(scr);
    tui_lineedit_free(&le);
}

static void test_history_browsing(void) {
    tui_history h;
    tui_history_init(&h, 10);
    tui_history_add(&h, "first");
    tui_history_add(&h, "second");
    char buf[64];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), "draft");
    le.history = &h;
    type(&le, "\x1b[A");
    ASSERT_EQ_STR(buf, "second", "Up: newest");
    type(&le, "\x1b[A");
    ASSERT_EQ_STR(buf, "first", "Up: older");
    type(&le, "\x1b[A");
    ASSERT_EQ_STR(buf, "first", "Up at the oldest");
    type(&le, "\x1b[B");
    ASSERT_EQ_STR(buf, "second", "Down");
    type(&le, "\x1b[B");
    ASSERT_EQ_STR(buf, "draft", "Down past the newest: the draft is back");
    type(&le, "\x1b[B");
    ASSERT_EQ_STR(buf, "draft", "Down with nothing newer");
    tui_lineedit_free(&le);
    tui_history_free(&h);
}

/* email-cli's completion hooks. */
static void complete_fwd(tui_lineedit *le) { tui_lineedit_set_text(le, "forward"); }
static void complete_back(tui_lineedit *le) { tui_lineedit_set_text(le, "back"); }

static void test_hooks(void) {
    char buf[32];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), "f");
    le.tab_fn = complete_fwd;
    le.shift_tab_fn = complete_back;
    ASSERT_EQ_INT(type(&le, "\t"), TUI_EDIT_CHANGED, "Tab");
    ASSERT_EQ_STR(buf, "forward", "tab_fn ran");
    ASSERT_EQ_INT(type(&le, "\x1b[Z"), TUI_EDIT_CHANGED, "Shift-Tab");
    ASSERT_EQ_STR(buf, "back", "shift_tab_fn ran");
    tui_lineedit_free(&le);
}

static void test_render_scrolls(void) {
    tui_screen *scr = tui_screen_new(2, 12);
    tui_buf b;
    tui_buf_init(&b);
    PtySession *s = pty_open(12, 2);
    ASSERT(scr && s, "fixtures");
    char buf[64];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), "abcdefghijklmnop");
    tui_canvas row = tui_canvas_sub(tui_canvas_of(scr), tui_rect_make(1, 0, 1, 12));
    int col = tui_lineedit_render(&le, row, "> ", TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(col, 11, "cursor at the right edge");
    tui_screen_render(scr, &b);
    pty_feed(s, b.data, b.len);
    const char *const expect[] = { "", "> hijklmnop", NULL };
    ASSERT_SCREEN_EQ(s, expect);
    int r, c;
    pty_cursor(s, &r, &c);
    ASSERT_EQ_INT(r, 1, "cursor row");
    ASSERT_EQ_INT(c, 11, "cursor column after the text");

    type(&le, "\x01");
    col = tui_lineedit_render(&le, row, "> ", TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(col, 2, "Home: cursor after the prompt");
    tui_buf_reset(&b);
    tui_screen_render(scr, &b);
    pty_feed(s, b.data, b.len);
    const char *const expect2[] = { "", "> abcdefghij", NULL };
    ASSERT_SCREEN_EQ(s, expect2);

    /* Wide text scrolled so a wide character is cut by the left edge. */
    tui_lineedit_set_text(&le, "x\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD");
    col = tui_lineedit_render(&le, row, "> ", TUI_STYLE_DEFAULT);
    tui_buf_reset(&b);
    tui_screen_render(scr, &b);
    pty_feed(s, b.data, b.len);
    const char *const expect3[] = { "", ">  \xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD", NULL };
    ASSERT_SCREEN_EQ(s, expect3);
    ASSERT_EQ_INT(col, 11, "cursor after the last character");
    ASSERT_EQ_INT(tui_lineedit_render(NULL, row, "", TUI_STYLE_DEFAULT), 0, "NULL editor");
    ASSERT_EQ_INT(tui_lineedit_cursor_col(NULL), 0, "cursor of NULL");
    tui_lineedit_free(&le);
    pty_close(s);
    tui_screen_free(scr);
    tui_buf_free(&b);
}

/* ── tui_lineedit_run(): email-cli's input_line_run() ────────────────── */

static void below_hook(const tui_lineedit *le) {
    int *fd = le->user;
    const char *s = "\033[4;1Hcompletion list";
    ssize_t w = write(*fd, s, strlen(s));
    (void)w;
}

static void test_run(void) {
    TPair p;
    ASSERT(tpair_open(&p, 5, 30) == 0, "openpty");
    tui_tty_raw *raw = tui_tty_raw_enter(p.slave, 0);
    ASSERT(raw != NULL, "raw mode");
    PtySession *s = pty_open(30, 5);
    ASSERT(s != NULL, "emulator");

    char buf[32];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), "abc");
    le.render_below = below_hook;
    le.user = &p.slave;
    tpair_type(&p, "\x1b[D\x7fX\r");
    ASSERT_EQ_INT(tui_lineedit_run(&le, p.slave, p.slave, 3, 3, "To: "), 1, "Enter returns 1");
    ASSERT_EQ_STR(buf, "aXc", "edited text");
    ASSERT_EQ_INT(le.trow, 3, "trow set");
    char out[8192];
    size_t n = tpair_read_all(&p, out, sizeof(out), 50);
    pty_feed(s, out, n);
    /* The completion row is cleared on the way out, as in email-cli. */
    const char *const expect[] = { "", "", "  To: aXc", "", "", NULL };
    ASSERT_SCREEN_EQ(s, expect);
    ASSERT(strstr(out, "completion list") != NULL, "render_below was called");
    ASSERT(strstr(out, "\033[?25l") != NULL, "cursor hidden at the end");

    tpair_type(&p, "zz\x1b");
    ASSERT_EQ_INT(tui_lineedit_run(&le, p.slave, p.slave, 1, 1, NULL), 0, "Esc returns 0");
    tpair_type(&p, "\x03");
    ASSERT_EQ_INT(tui_lineedit_run(&le, p.slave, p.slave, 0, 0, ""), 0, "Ctrl-C returns 0");
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_EQ_INT(tui_lineedit_run(NULL, p.slave, p.slave, 1, 1, ""), -1, "NULL editor");
    tui_lineedit_free(&le);
    tui_tty_raw_exit(&raw);

    /* The input ends: -1, not a spin. */
    tui_lineedit_init(&le, buf, sizeof(buf), NULL);
    tpair_close_master(&p);
    ASSERT_EQ_INT(tui_lineedit_run(&le, p.slave, p.slave, 1, 1, ""), -1, "hang-up");
    tui_lineedit_free(&le);
    pty_close(s);
    tpair_close(&p);
}

/* ── tui_readline(): tg-cli's rl_readline(), mamind's line_editor_read() ── */

static void test_readline_tty(void) {
    TPair p;
    ASSERT(tpair_open(&p, 3, 40) == 0, "openpty");
    struct termios before, after;
    tcgetattr(p.slave, &before);
    tui_history h;
    tui_history_init(&h, 10);
    tui_history_add(&h, "earlier");
    tui_readline_opts o;
    tui_readline_opts_init(&o);
    ASSERT_EQ_INT(o.in_fd, 0, "default input");
    ASSERT_EQ_INT(o.out_fd, 1, "default output");
    o.in_fd = o.out_fd = p.slave;
    o.history = &h;
    char buf[64];
    /* recall, edit, Alt+x (ignored), Enter */
    pid_t typer = tpair_type_when_raw(&p, "\x1b[A!\x1b" "x\r");
    ASSERT(typer > 0, "typist");
    ASSERT_EQ_INT(tui_readline(">> ", buf, sizeof(buf), &o), 8, "length");
    int st = 0;
    waitpid(typer, &st, 0);
    ASSERT(WIFEXITED(st) && WEXITSTATUS(st) == 0, "typed once raw mode was on");
    ASSERT_EQ_STR(buf, "earlier!", "recalled and edited; Alt+x is no text");
    ASSERT_EQ_INT(tui_history_count(&h), 2, "added to the history");
    tcgetattr(p.slave, &after);
    ASSERT(after.c_lflag & ICANON, "mode restored");
    char out[4096];
    size_t n = tpair_read_all(&p, out, sizeof(out), 50);
    PtySession *s = pty_open(40, 3);
    ASSERT(s != NULL, "emulator");
    pty_feed(s, out, n);
    const char *const expect[] = { ">> earlier!", "", "", NULL };
    ASSERT_SCREEN_EQ(s, expect);
    int r, c;
    pty_cursor(s, &r, &c);
    ASSERT_EQ_INT(r, 1, "on the next line after Enter");
    ASSERT_EQ_INT(c, 0, "at its start");

    typer = tpair_type_when_raw(&p, "abc\x03");
    ASSERT_EQ_INT(tui_readline("> ", buf, sizeof(buf), &o), TUI_READLINE_CANCEL, "Ctrl-C");
    waitpid(typer, NULL, 0);
    ASSERT_EQ_STR(buf, "", "cancelled line is empty");
    typer = tpair_type_when_raw(&p, "\x04");
    ASSERT_EQ_INT(tui_readline("> ", buf, sizeof(buf), &o), TUI_READLINE_EOF, "Ctrl-D on empty");
    waitpid(typer, NULL, 0);
    volatile sig_atomic_t flag = 1;
    o.interrupt = &flag;
    ASSERT_EQ_INT(tui_readline("> ", buf, sizeof(buf), &o), TUI_READLINE_INTERRUPT,
                  "interrupt flag (mamind)");
    o.interrupt = NULL;
    o.timeout_sec = 1;
    int64_t t0 = test_now_ms();
    ASSERT_EQ_INT(tui_readline("> ", buf, sizeof(buf), &o), TUI_READLINE_TIMEOUT, "idle timeout");
    int64_t dt = test_now_ms() - t0;
    ASSERT(dt >= 950 && dt < 3000, "timed out after about a second");
    o.timeout_sec = 0;
    tcgetattr(p.slave, &after);
    ASSERT(after.c_lflag & ICANON, "mode restored after every ending");
    ASSERT_EQ_INT(tui_readline("> ", NULL, 4, &o), TUI_READLINE_EOF, "NULL buffer");
    tpair_read_all(&p, out, sizeof(out), 50);
    tpair_close_master(&p);
    ASSERT_EQ_INT(tui_readline("> ", buf, sizeof(buf), &o), TUI_READLINE_EOF, "hang-up");
    pty_close(s);
    tui_history_free(&h);
    tpair_close(&p);
}

/* SIGINT during the wait, with the signal keys kept (mamind). */
static volatile sig_atomic_t g_int;
static void on_int(int sig) { (void)sig; g_int = 1; }

static void test_readline_sigint(void) {
    TPair p;
    ASSERT(tpair_open(&p, 3, 40) == 0, "openpty");
    struct sigaction sa, old;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_int;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, &old);
    g_int = 0;
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        usleep(150 * 1000);
        kill(getppid(), SIGINT);
        _exit(0);
    }
    tui_readline_opts o;
    tui_readline_opts_init(&o);
    o.in_fd = o.out_fd = p.slave;
    o.interrupt = &g_int;
    o.flags = TUI_TERM_KEEP_ISIG;
    char buf[16];
    int64_t t0 = test_now_ms();
    ASSERT_EQ_INT(tui_readline("> ", buf, sizeof(buf), &o), TUI_READLINE_INTERRUPT, "interrupted");
    ASSERT(test_now_ms() - t0 < 1000, "promptly");
    waitpid(pid, NULL, 0);
    sigaction(SIGINT, &old, NULL);
    tpair_close(&p);
}

static void test_readline_not_a_tty(void) {
    int in[2], out[2];
    ASSERT_EQ_INT(pipe(in), 0, "pipe");
    ASSERT_EQ_INT(pipe(out), 0, "pipe");
    ASSERT_EQ_INT(write(in[1], "hello\r\nworld", 12), 12, "write");
    close(in[1]);
    tui_readline_opts o;
    tui_readline_opts_init(&o);
    o.in_fd = in[0];
    o.out_fd = out[1];
    char buf[4];
    ASSERT_EQ_INT(tui_readline("p> ", buf, sizeof(buf), &o), 3, "truncated length");
    ASSERT_EQ_STR(buf, "hel", "truncated to the buffer, CR dropped");
    char big[16];
    ASSERT_EQ_INT(tui_readline("p> ", big, sizeof(big), &o), 5, "last line without newline");
    ASSERT_EQ_STR(big, "world", "second line");
    ASSERT_EQ_INT(tui_readline("p> ", big, sizeof(big), &o), TUI_READLINE_EOF, "end of input");
    close(out[1]);
    char prompts[64] = {0};
    ssize_t n = read(out[0], prompts, sizeof(prompts) - 1);
    ASSERT_EQ_INT(n, 9, "three prompts written");
    ASSERT_EQ_STR(prompts, "p> p> p> ", "the prompt each time (tg-cli, mamind)");
    close(in[0]);
    close(out[0]);
}

static const TestCase tests[] = {
    TEST_CASE(test_history),
    TEST_CASE(test_insert_and_move),
    TEST_CASE(test_combining_moves_with_base),
    TEST_CASE(test_words_and_kill_yank),
    TEST_CASE(test_results),
    TEST_CASE(test_capacity_and_controls),
    TEST_CASE(test_shifted_character_and_scroll_back),
    TEST_CASE(test_history_browsing),
    TEST_CASE(test_hooks),
    TEST_CASE(test_render_scrolls),
    TEST_CASE(test_run),
    TEST_CASE(test_readline_tty),
    TEST_CASE(test_readline_sigint),
    TEST_CASE(test_readline_not_a_tty),
};

int main(int argc, char **argv) {
    return RUN_SUITE("lineedit", tests, 130, 60);
}
