/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Unit tests of the failure paths: allocation and system call faults. */

/*
 * The failure paths: what each module does when an allocation or a system
 * call fails.  Failures are injected by the linker (tests/common/faults.h);
 * every case checks the documented outcome -- the error return, errno,
 * and that nothing is left half-done (the terminal mode, the screen state).
 */

#include "test_helpers.h"
#include "faults.h"
#include "tty_fixture.h"
#include "tuikit/tuikit.h"

#include <errno.h>
#include <fcntl.h>
#include <termios.h>
#include <wchar.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static int same_mode(const struct termios *a, const struct termios *b) {
    return a->c_lflag == b->c_lflag && a->c_iflag == b->c_iflag &&
           a->c_cc[VMIN] == b->c_cc[VMIN];
}

/* ── tui_buf ─────────────────────────────────────────────────────────── */

static void test_buf_failures(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    tui_buf b;
    tui_buf_init(&b);
    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    errno = 0;
    ASSERT_EQ_INT(tui_buf_puts(&b, "x"), -1, "append: allocation failure");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    ASSERT_EQ_INT(b.failed, 1, "failed is set");
    fault_reset();
    tui_buf_reset(&b);
    ASSERT_EQ_INT(tui_buf_append(&b, "x", (size_t)-1), -1, "a size that cannot be added");
    ASSERT_EQ_INT(b.failed, 1, "failed is set");
    tui_buf_reset(&b);
    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_buf_printf(&b, "%d", 42), -1, "printf: allocation failure");
    fault_reset();
    tui_buf_reset(&b);
    /* An encoding error in the format: a surrogate as a wide character. */
    ASSERT_EQ_INT(tui_buf_printf(&b, "%lc", (wint_t)0xD800), -1, "printf: encoding error");
    ASSERT_EQ_INT(b.failed, 1, "failed is set");
    tui_buf_free(&b);
}

/* ── tui_screen ──────────────────────────────────────────────────────── */

static void test_screen_failures(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    fault_arm(F_CALLOC, 0, 1, ENOMEM);
    ASSERT(tui_screen_new(2, 2) == NULL, "new: the screen itself");
    fault_reset();
    fault_arm(F_MALLOC, 1, 1, ENOMEM);
    errno = 0;
    ASSERT(tui_screen_new(2, 2) == NULL, "new: the second grid");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    fault_reset();
    tui_screen *s = tui_screen_new(2, 4);
    ASSERT(s != NULL, "new");
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_screen_resize(s, 3, 3), -1, "resize: allocation failure");
    fault_reset();
    ASSERT_EQ_INT(tui_screen_rows(s), 2, "size unchanged");
    /* The buffer fails in the middle of a frame. */
    tui_screen_put(s, 0, 0, 0, "abcd", tui_style_attrs(TUI_ATTR_BOLD));
    tui_buf b;
    tui_buf_init(&b);
    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    errno = 0;
    ASSERT_EQ_INT(tui_screen_render(s, &b), -1, "render: the buffer failed");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    fault_reset();
    tui_buf_reset(&b);
    ASSERT(tui_screen_render(s, &b) > 0, "next render");
    ASSERT(strstr(b.data, "\033[2J") != NULL, "the next frame is a full one");
    tui_buf_free(&b);
    tui_screen_free(s);
}

/* ── tui_textview, tui_dialog, tui_lineedit, the decoder ─────────────── */

static void test_textview_failures(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    tui_textview tv;
    tui_textview_init(&tv);
    tui_textview_set_text(&tv, "old");
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_textview_set_text(&tv, "new"), -1, "set_text: allocation failure");
    fault_reset();
    ASSERT_EQ_STR(tv.text, "old", "the old text stays");
    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_textview_append(&tv, "more", 4), -1, "append: allocation failure");
    fault_reset();
    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    errno = 0;
    ASSERT_EQ_INT(tui_textview_wrap(&tv, 10), -1, "wrap: line index allocation");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    fault_reset();
    fault_arm(F_REALLOC, 1, 1, ENOMEM);
    ASSERT_EQ_INT(tui_textview_wrap(&tv, 10), -1, "wrap: the second index array");
    fault_reset();
    tui_screen *s = tui_screen_new(2, 10);
    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    tui_textview_render(&tv, tui_canvas_of(s), TUI_STYLE_DEFAULT);   /* draws nothing, survives */
    fault_reset();
    ASSERT_EQ_INT(tui_textview_wrap(&tv, 10), 1, "works again");
    tui_screen_free(s);
    tui_textview_free(&tv);
}

static void test_dialog_and_decoder_failures(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    tui_dialog d;
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    errno = 0;
    ASSERT_EQ_INT(tui_dialog_message(&d, "t", "x"), -1, "dialog: allocation failure");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    fault_reset();

    static const char *const items[] = { "a", "b" };
    fault_arm(F_CALLOC, 0, 1, ENOMEM);
    errno = 0;
    ASSERT_EQ_INT(tui_dialog_checklist(&d, "t", items, 2, NULL, 0), -1, "checklist: the set");
    ASSERT_EQ_INT(errno, ENOMEM, "errno");
    fault_reset();

    tui_decoder dec;
    tui_decoder_init(&dec);
    tui_event ev;
    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    const char *in = "\033[200~abc\033[201~";
    tui_decoder_feed(&dec, in, strlen(in));
    ASSERT_EQ_INT(tui_decoder_next(&dec, &ev), 1, "paste event");
    fault_reset();
    ASSERT_EQ_INT(ev.paste_len, 0, "the paste is cut where memory ran out");
    ASSERT_EQ_INT(dec.paste_truncated, 1, "and says so");
    tui_decoder_free(&dec);
}

static void test_lineedit_failures(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    tui_history h;
    tui_history_init(&h, 4);
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_history_add(&h, "a"), -1, "history: the copy");
    fault_reset();
    fault_arm(F_CALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_history_add(&h, "a"), -1, "history: the item array");
    fault_reset();
    ASSERT_EQ_INT(tui_history_count(&h), 0, "nothing added");
    tui_history_add(&h, "older");

    char buf[32];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof(buf), "one two");
    le.history = &h;
    tui_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = TUI_KEY_CHAR;
    ev.cp = 'w';
    ev.mods = TUI_MOD_CTRL;
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    tui_lineedit_handle(&le, &ev);
    fault_reset();
    ASSERT_EQ_STR(buf, "one ", "the word is removed even without a kill buffer");
    ev.cp = 'y';
    tui_lineedit_handle(&le, &ev);
    ASSERT_EQ_STR(buf, "one ", "nothing to yank");
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = TUI_KEY_UP;
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    tui_lineedit_handle(&le, &ev);
    fault_reset();
    ASSERT_EQ_STR(buf, "older", "history browsing without a saved draft");
    ev.key = TUI_KEY_DOWN;
    tui_lineedit_handle(&le, &ev);
    ASSERT_EQ_STR(buf, "", "back to an empty draft");
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_PASTE;
    ev.paste = "pasted";
    ev.paste_len = 6;
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_lineedit_handle(&le, &ev), TUI_EDIT_IGNORED, "paste without memory");
    fault_reset();
    tui_lineedit_free(&le);
    tui_history_free(&h);
}

/* tui_lineedit_run(): a failed poll, read or write ends it with -1. */
static void test_lineedit_run_failures(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    char buf[16];
    tui_lineedit le;
    int p[2], null_fd = open("/dev/null", O_WRONLY);
    ASSERT(null_fd >= 0, "/dev/null");
    static const struct { fault_fn fn; int skip; const char *what; } cases[] = {
        { F_POLL, 0, "poll fails" },
        { F_READ, 0, "read fails" },
        { F_WRITE, 0, "drawing fails" },
        { F_WRITE, 1, "putting the cursor back fails" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        ASSERT_EQ_INT(pipe(p), 0, "pipe");
        ASSERT_EQ_INT(write(p[1], "abc\r", 4), 4, "keys");
        tui_lineedit_init(&le, buf, sizeof(buf), NULL);
        fault_arm(cases[i].fn, cases[i].skip, 1, EBADF);
        ASSERT_EQ_INT(tui_lineedit_run(&le, p[0], null_fd, 1, 1, ""), -1, cases[i].what);
        fault_reset();
        tui_lineedit_free(&le);
        close(p[0]);
        close(p[1]);
    }
    /* EINTR and EAGAIN are retried, not errors. */
    ASSERT_EQ_INT(pipe(p), 0, "pipe");
    ASSERT_EQ_INT(write(p[1], "ok\r", 3), 3, "keys");
    tui_lineedit_init(&le, buf, sizeof(buf), NULL);
    fault_arm(F_POLL, 0, 1, EINTR);
    fault_arm(F_READ, 0, 1, EAGAIN);
    ASSERT_EQ_INT(tui_lineedit_run(&le, p[0], null_fd, 1, 1, ""), 1, "retried");
    fault_reset();
    ASSERT_EQ_STR(buf, "ok", "line read");
    close(p[0]);
    close(p[1]);
    /* A line wider than the terminal: drawn from its scrolled part, a wide
     * character cut by the left edge as a blank (/dev/null: 80 columns). */
    ASSERT_EQ_INT(pipe(p), 0, "pipe");
    char keys[200];
    size_t k = 0;
    keys[k++] = 'x';
    for (int i = 0; i < 50; i++) { memcpy(keys + k, "\xE4\xB8\xAD", 3); k += 3; }
    keys[k++] = '\r';
    ASSERT_EQ_INT(write(p[1], keys, k), (ssize_t)k, "keys");
    char big[256];
    tui_lineedit_init(&le, big, sizeof(big), "e\xCC\x81");
    ASSERT_EQ_INT(tui_lineedit_run(&le, p[0], null_fd, 1, 1, "> "), 1, "long line");
    ASSERT_EQ_INT(le.scroll > 0, 1, "scrolled");
    tui_lineedit_free(&le);
    close(p[0]);
    close(p[1]);
    close(null_fd);
}

/* tui_readline(): failures on a terminal. */
static void test_readline_failures(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    TPair t;
    ASSERT(tpair_open(&t, 4, 40) == 0, "openpty");
    tui_readline_opts o;
    tui_readline_opts_init(&o);
    o.in_fd = o.out_fd = t.slave;
    char buf[16];
    /* Raw mode refused: the plain path reads the line. */
    tpair_type(&t, "plain\n");
    fault_arm(F_TCSETATTR, 0, 1, EIO);
    ASSERT_EQ_INT(tui_readline("> ", buf, sizeof(buf), &o), 5, "plain fallback");
    fault_reset();
    ASSERT_EQ_STR(buf, "plain", "line");
    /* Drawing fails: the end, with the mode restored. */
    fault_arm(F_WRITE, 0, 1, EIO);
    ASSERT_EQ_INT(tui_readline("> ", buf, sizeof(buf), &o), TUI_READLINE_EOF, "write fails");
    fault_reset();
    struct termios m;
    tcgetattr(t.slave, &m);
    ASSERT(m.c_lflag & ICANON, "mode restored");
    tpair_read_all(&t, buf, sizeof(buf), 20);
    tpair_close(&t);

    /* The plain path, interrupted by a signal with the flag set. */
    int p[2];
    ASSERT_EQ_INT(pipe(p), 0, "pipe");
    volatile sig_atomic_t flag = 1;
    o.in_fd = p[0];
    o.out_fd = -1;
    o.interrupt = &flag;
    fault_arm(F_READ, 0, 1, EINTR);
    ASSERT_EQ_INT(tui_readline(NULL, buf, sizeof(buf), &o), TUI_READLINE_INTERRUPT, "interrupted");
    fault_reset();
    flag = 0;
    ASSERT_EQ_INT(write(p[1], "z\n", 2), 2, "line");
    fault_arm(F_READ, 0, 1, EINTR);
    ASSERT_EQ_INT(tui_readline(NULL, buf, sizeof(buf), &o), 1, "EINTR without the flag: retried");
    fault_reset();
    close(p[0]);
    close(p[1]);
}

/* ── tui_tty ─────────────────────────────────────────────────────────── */

static void test_tty_failures(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    TPair t;
    ASSERT(tpair_open(&t, 4, 40) == 0, "openpty");
    struct termios before, m;
    tcgetattr(t.slave, &before);

    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT(tui_tty_raw_enter(t.slave, 0) == NULL, "raw: allocation");
    fault_reset();
    fault_arm(F_TCGETATTR, 0, 1, EIO);
    ASSERT(tui_tty_raw_enter(t.slave, 0) == NULL, "raw: tcgetattr");
    fault_reset();
    fault_arm(F_TCSETATTR, 0, 1, EIO);
    errno = 0;
    ASSERT(tui_tty_raw_enter(t.slave, 0) == NULL, "raw: tcsetattr");
    ASSERT_EQ_INT(errno, EIO, "errno");
    fault_reset();
    fault_arm(F_TCGETATTR, 1, 1, EIO);
    ASSERT(tui_tty_raw_enter(t.slave, 0) == NULL, "raw: reading the mode back");
    fault_reset();
    fault_tcgetattr_lies(1, 1);
    errno = 0;
    ASSERT(tui_tty_raw_enter(t.slave, 0) == NULL, "raw: the terminal ignored the change");
    ASSERT_EQ_INT(errno, EIO, "errno EIO");
    fault_reset();
    tcgetattr(t.slave, &m);
    ASSERT(same_mode(&before, &m), "every failure left the mode as it was");
    fault_arm(F_TCSETATTR, 0, 1, EINTR);
    tui_tty_raw *raw = tui_tty_raw_enter(t.slave, 0);
    fault_reset();
    ASSERT(raw != NULL, "raw: EINTR retried");

    /* read_key: an interrupted wait and read are retried. */
    tui_event ev;
    tpair_type(&t, "\033");
    fault_arm(F_POLL, 0, 1, EINTR);
    fault_arm(F_READ, 0, 1, EINTR);
    ASSERT_EQ_INT(tui_tty_read_key(t.slave, &ev), 1, "read_key after EINTRs");
    ASSERT_EQ_INT(ev.key, TUI_KEY_ESC, "ESC");
    fault_reset();
    tpair_type(&t, "\033");
    fault_arm(F_POLL, 0, 1, EBADF);
    ASSERT_EQ_INT(tui_tty_read_key(t.slave, &ev), -1, "read_key: poll error");
    fault_reset();
    fault_arm(F_READ, 0, 1, EBADF);
    ASSERT_EQ_INT(tui_tty_read_key(t.slave, &ev), -1, "read_key: read error");
    fault_reset();
    /* A paste through read_key arrives without text. */
    tcflush(t.slave, TCIFLUSH);
    tpair_type(&t, "\033[200~xy\033[201~");
    ASSERT_EQ_INT(tui_tty_read_key(t.slave, &ev), 1, "paste");
    ASSERT_EQ_INT(ev.type, TUI_EV_PASTE, "type");
    ASSERT_EQ_INT(ev.paste_len, 0, "no text");
    tui_tty_raw_exit(&raw);

    /* read_password: the mode change and the prompt can fail. */
    char pw[16];
    fault_arm(F_TCGETATTR, 0, 1, EIO);
    ASSERT_EQ_INT(tui_tty_read_password(t.slave, t.slave, "P", pw, sizeof(pw)), -1, "tcgetattr");
    fault_reset();
    fault_arm(F_TCSETATTR, 0, 1, EIO);
    ASSERT_EQ_INT(tui_tty_read_password(t.slave, t.slave, "P", pw, sizeof(pw)), -1, "tcsetattr");
    fault_reset();
    fault_arm(F_WRITE, 0, 1, EIO);
    ASSERT_EQ_INT(tui_tty_read_password(t.slave, t.slave, "P", pw, sizeof(pw)), -1, "prompt");
    fault_reset();
    tcgetattr(t.slave, &m);
    ASSERT(m.c_lflag & ECHO, "echo back on after every failure");
    struct sigaction now;
    sigaction(SIGINT, NULL, &now);
    ASSERT(now.sa_handler == SIG_DFL, "no handler left behind");
    tcflush(t.slave, TCIFLUSH);
    tpair_type(&t, "pw\n");
    fault_arm(F_READ, 0, 1, EINTR);
    ASSERT_EQ_INT(tui_tty_read_password(t.slave, t.slave, NULL, pw, sizeof(pw)), 2, "EINTR retried");
    fault_reset();
    tpair_type(&t, "pw\n");               /* typed first: typing writes too */
    fault_arm(F_WRITE, 0, 1, EIO);
    ASSERT_EQ_INT(tui_tty_read_password(t.slave, t.slave, NULL, pw, sizeof(pw)), -1,
                  "the final newline cannot be written");
    fault_reset();

    /* The masked field: the same failures, and one for each echo it writes. */
    fault_arm(F_TCGETATTR, 0, 1, EIO);
    ASSERT_EQ_INT(tui_tty_read_password_masked(t.slave, t.slave, "P", pw, sizeof(pw)), -1, "masked: tcgetattr");
    fault_reset();
    fault_arm(F_TCSETATTR, 0, 1, EIO);
    ASSERT_EQ_INT(tui_tty_read_password_masked(t.slave, t.slave, "P", pw, sizeof(pw)), -1, "masked: tcsetattr");
    fault_reset();
    fault_arm(F_WRITE, 0, 1, EIO);
    ASSERT_EQ_INT(tui_tty_read_password_masked(t.slave, t.slave, "P", pw, sizeof(pw)), -1, "masked: prompt");
    fault_reset();
    tcgetattr(t.slave, &m);
    ASSERT(m.c_lflag & ECHO, "masked: echo back on after every failure");
    ASSERT(m.c_lflag & ICANON, "masked: canonical mode back on");
    sigaction(SIGINT, NULL, &now);
    ASSERT(now.sa_handler == SIG_DFL, "masked: no handler left behind");
    {
        pid_t typist = tpair_type_when_raw(&t, "a");
        fault_arm(F_READ, 0, 1, EBADF);
        ASSERT_EQ_INT(tui_tty_read_password_masked(t.slave, t.slave, NULL, pw, sizeof(pw)), -1,
                      "masked: read error");
        ASSERT_EQ_INT(errno, EBADF, "masked: the error of read");
        fault_reset();
        int st = 0;
        waitpid(typist, &st, 0);
        tcflush(t.slave, TCIFLUSH);
    }
    static const struct { const char *keys; int skip; const char *what; } echo[] = {
        { "a",      0, "the mask of a character" },
        { "a\x7f",  1, "the erasure by Backspace" },
        { "ab\x15", 2, "the erasure by Ctrl-U" },
    };
    for (size_t i = 0; i < sizeof(echo) / sizeof(echo[0]); i++) {
        pid_t typist = tpair_type_when_raw(&t, echo[i].keys);
        fault_arm(F_WRITE, echo[i].skip, 1, EIO);
        memset(pw, 'x', sizeof(pw));
        ASSERT_EQ_INT(tui_tty_read_password_masked(t.slave, t.slave, NULL, pw, sizeof(pw)), -1,
                      echo[i].what);
        ASSERT_EQ_INT(errno, EIO, "the error of write");
        ASSERT_EQ_INT(pw[0], 0, "the buffer is wiped");
        fault_reset();
        int st = 0;
        waitpid(typist, &st, 0);
        tcflush(t.slave, TCIFLUSH);
    }
    tcgetattr(t.slave, &m);
    ASSERT(m.c_lflag & ECHO, "masked: mode restored after the echo failures");
    /* The terminal is gone: end of input. */
    {
        TPair gone;
        ASSERT(tpair_open(&gone, 4, 40) == 0, "openpty");
        tpair_close_master(&gone);
        ASSERT_EQ_INT(tui_tty_read_password_masked(gone.slave, gone.slave, NULL, pw, sizeof(pw)), -1,
                      "masked: hang-up");
        tpair_close(&gone);
    }
    tpair_close(&t);
}

/* ── tui_term ────────────────────────────────────────────────────────── */

static tui_term *open_on(TPair *t, unsigned flags) {
    tui_term_opts o;
    tui_term_opts_init(&o);
    o.in_fd = o.out_fd = t->slave;
    o.flags |= flags;
    return tui_term_open(&o);
}

static void test_term_open_failures(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    TPair t;
    ASSERT(tpair_open(&t, 4, 40) == 0, "openpty");
    struct termios before, m;
    tcgetattr(t.slave, &before);
    static const struct { fault_fn fn; int skip; int err; const char *what; } cases[] = {
        { F_CALLOC, 0, ENOMEM, "the session" },
        { F_TCGETATTR, 0, EIO, "reading the mode" },
        { F_PIPE, 0, EMFILE, "the wake-up pipe" },
        { F_FCNTL, 0, EBADF, "pipe flags" },
        { F_FCNTL, 3, EBADF, "the second end's flags" },
        { F_TCSETATTR, 0, EIO, "entering raw mode" },
        { F_WRITE, 0, EIO, "the entry sequences" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        fault_arm(cases[i].fn, cases[i].skip, 1, cases[i].err);
        errno = 0;
        tui_term *term = open_on(&t, 0);
        fault_reset();
        ASSERT(term == NULL, cases[i].what);
        ASSERT_EQ_INT(errno, cases[i].err, cases[i].what);
        tcgetattr(t.slave, &m);
        ASSERT(same_mode(&before, &m), "the terminal is left as it was");
        struct sigaction now;
        sigaction(SIGTERM, NULL, &now);
        ASSERT(now.sa_handler == SIG_DFL, "no handler installed");
    }
    tui_term *term = open_on(&t, 0);
    ASSERT(term != NULL, "a later open works: nothing stayed busy");
    tui_term_close(term);
    char drain[256];
    tpair_read_all(&t, drain, sizeof(drain), 20);
    tpair_close(&t);
}

static void test_term_runtime_failures(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    TPair t;
    ASSERT(tpair_open(&t, 4, 40) == 0, "openpty");
    tui_term *term = open_on(&t, 0);
    ASSERT(term != NULL, "open");
    tui_event ev;
    char drain[512];

    fault_arm(F_POLL, 0, 1, EBADF);
    errno = 0;
    ASSERT_EQ_INT(tui_term_wait(term, 100, NULL, 0, &ev), -1, "wait: poll error");
    ASSERT_EQ_INT(errno, EBADF, "errno");
    fault_reset();
    fault_arm(F_POLL, 0, 1, EINTR);
    ASSERT_EQ_INT(tui_term_wait(term, 30, NULL, 0, &ev), 0, "wait: EINTR retried to the timeout");
    fault_reset();
    tpair_type(&t, "k");
    fault_arm(F_READ, 0, 1, EAGAIN);
    ASSERT_EQ_INT(tui_term_wait(term, 1000, NULL, 0, &ev), 1, "wait: EAGAIN retried");
    ASSERT(TUI_EV_IS_CHAR(&ev, 'k'), "the key");
    fault_reset();
    tpair_type(&t, "k");
    fault_arm(F_READ, 0, 1, EBADF);
    ASSERT_EQ_INT(tui_term_wait(term, 1000, NULL, 0, &ev), -1, "wait: read error");
    fault_reset();
    ASSERT_EQ_INT(tui_term_wait(term, 1000, NULL, 0, &ev), 1, "the key is still there");
    tpair_type(&t, "k");
    fault_arm(F_READ, 0, 1, EIO);
    ASSERT_EQ_INT(tui_term_wait(term, 1000, NULL, 0, &ev), 1, "wait: EIO");
    ASSERT_EQ_INT(ev.type, TUI_EV_HANGUP, "is a hang-up");
    fault_reset();
    tui_term_close(term);
    tpair_read_all(&t, drain, sizeof(drain), 20);

    /* More extra descriptors than the built-in array, without memory. */
    term = open_on(&t, 0);
    struct pollfd many[20];
    for (int i = 0; i < 20; i++) { many[i].fd = -1; many[i].events = POLLIN; }
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_term_wait(term, 0, many, 20, &ev), -1, "wait: no memory for the array");
    fault_reset();

    /* resume: the mode cannot be set again. */
    ASSERT_EQ_INT(tui_term_suspend(term), 0, "suspend");
    fault_arm(F_TCSETATTR, 0, 1, EIO);
    ASSERT_EQ_INT(tui_term_resume(term), -1, "resume fails");
    fault_reset();
    struct termios m;
    tcgetattr(t.slave, &m);
    ASSERT(m.c_lflag & ICANON, "and the terminal stays usable");
    ASSERT_EQ_INT(tui_term_resume(term), 0, "a later resume works");

    /* Leaving: the sequences or the mode cannot be written. */
    fault_arm(F_WRITE, 0, 1, EIO);
    ASSERT_EQ_INT(tui_term_suspend(term), -1, "suspend reports the failed write");
    fault_reset();
    ASSERT_EQ_INT(tui_term_resume(term), 0, "resume");
    fault_arm(F_TCSETATTR, 0, 1, EIO);
    ASSERT_EQ_INT(tui_term_close(term), -1, "close reports the failed mode");
    fault_reset();
    tpair_read_all(&t, drain, sizeof(drain), 20);

    /* Without signal handling. */
    term = open_on(&t, TUI_TERM_NO_SIGNALS);
    ASSERT(term != NULL, "open");
    fault_arm(F_TCSETATTR, 0, 1, EINTR);
    ASSERT_EQ_INT(tui_term_close(term), 0, "close: EINTR retried");
    fault_reset();

    /* A closed input descriptor is a hang-up, not a spin. */
    int fd = dup(t.slave);
    tui_term_opts o;
    tui_term_opts_init(&o);
    o.in_fd = o.out_fd = fd;
    o.flags = TUI_TERM_NO_SIGNALS;
    term = tui_term_open(&o);
    ASSERT(term != NULL, "open on a duplicate");
    close(fd);
    ASSERT_EQ_INT(tui_term_wait(term, 1000, NULL, 0, &ev), 1, "an event");
    ASSERT_EQ_INT(ev.type, TUI_EV_HANGUP, "POLLNVAL is a hang-up");
    tui_term_close(term);
    tpair_read_all(&t, drain, sizeof(drain), 20);
    tpair_close(&t);
}

/* tui_term_open(NULL): standard input and output, when they are a terminal. */
static void test_term_default_options(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    TPair t;
    ASSERT(tpair_open(&t, 4, 40) == 0, "openpty");
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        dup2(t.slave, STDIN_FILENO);
        dup2(t.slave, STDOUT_FILENO);
        tui_term *term = tui_term_open(NULL);
        int ok = term != NULL;
        tui_term_close(term);
        exit(ok ? 0 : 1);
    }
    int st = 0;
    waitpid(pid, &st, 0);
    ASSERT(WIFEXITED(st) && WEXITSTATUS(st) == 0, "opened on stdin/stdout with no options");
    char drain[256];
    tpair_read_all(&t, drain, sizeof(drain), 20);
    tpair_close(&t);
}

/* SIGINT at the password prompt: the terminal gets its echo back before
 * the process dies of the signal. */
static void test_password_sigint(void) {
    fault_reset();          /* a failed assertion elsewhere left nothing armed */
    TPair t;
    ASSERT(tpair_open(&t, 4, 40) == 0, "openpty");
    struct termios before, after;
    tcgetattr(t.slave, &before);
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        char pw[16];
        tui_tty_read_password(t.slave, t.slave, "Password", pw, sizeof(pw));
        exit(3);
    }
    char seen[128];
    ASSERT(tpair_read_until(&t, "Password: ", seen, sizeof(seen), 3000) == 0, "prompt");
    tpair_type(&t, "hun");
    kill(pid, SIGINT);
    int st = 0;
    waitpid(pid, &st, 0);
    ASSERT(WIFSIGNALED(st) && WTERMSIG(st) == SIGINT, "died of SIGINT");
    tcgetattr(t.slave, &after);
    ASSERT(after.c_lflag & ECHO, "echo restored by the handler");
    ASSERT(same_mode(&before, &after), "the whole mode restored");
    tpair_close(&t);
}

static const TestCase tests[] = {
    TEST_CASE(test_buf_failures),
    TEST_CASE(test_screen_failures),
    TEST_CASE(test_textview_failures),
    TEST_CASE(test_dialog_and_decoder_failures),
    TEST_CASE(test_lineedit_failures),
    TEST_CASE(test_lineedit_run_failures),
    TEST_CASE(test_readline_failures),
    TEST_CASE(test_tty_failures),
    TEST_CASE(test_term_open_failures),
    TEST_CASE(test_term_runtime_failures),
    TEST_CASE(test_term_default_options),
    TEST_CASE(test_password_sigint),
};

int main(int argc, char **argv) {
    return RUN_SUITE("faults", tests, 150, 60);
}
