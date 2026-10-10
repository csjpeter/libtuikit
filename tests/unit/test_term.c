/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Unit tests of tui_term over a pseudo-terminal pair. */

/*
 * Unit tests of tui_term over a real pseudo-terminal pair.  The session
 * works on the slave; the test reads the master to see exactly which bytes
 * were written, and reads the slave's termios to see the mode.
 *
 * The signal paths run in a forked child, since they end or stop the
 * process; the parent asserts the child's fate and the terminal it left.
 */

#include "test_helpers.h"
#include "tty_fixture.h"
#include "tuikit/term.h"

#include <errno.h>
#include <sys/resource.h>
#include <termios.h>

int g_tests_run = 0;
int g_tests_failed = 0;

#define ENTER_DEFAULT "\033[?1049h\033[?25l\033[?7l"
#define LEAVE_DEFAULT "\033[0m\033[?7h\033[?25h\033[?1049l"

static tui_term *open_on(TPair *p, unsigned extra_flags, int esc_delay) {
    tui_term_opts o;
    tui_term_opts_init(&o);
    o.in_fd = p->slave;
    o.out_fd = p->slave;
    o.flags |= extra_flags;
    o.esc_delay_ms = esc_delay;
    return tui_term_open(&o);
}

static int same_mode(const struct termios *a, const struct termios *b) {
    return a->c_lflag == b->c_lflag && a->c_iflag == b->c_iflag &&
           a->c_oflag == b->c_oflag && a->c_cc[VMIN] == b->c_cc[VMIN] &&
           a->c_cc[VTIME] == b->c_cc[VTIME];
}

static void test_opts_defaults(void) {
    tui_term_opts o;
    memset(&o, 0x5A, sizeof(o));
    tui_term_opts_init(&o);
    ASSERT_EQ_INT(o.in_fd, STDIN_FILENO, "in_fd");
    ASSERT_EQ_INT(o.out_fd, STDOUT_FILENO, "out_fd");
    ASSERT_EQ_INT(o.flags, TUI_TERM_ALTSCREEN | TUI_TERM_HIDE_CURSOR | TUI_TERM_NOWRAP, "flags");
    ASSERT_EQ_INT(o.esc_delay_ms, 100, "ESC delay: email-cli's 100 ms");
    tui_term_opts_init(NULL);
}

static void test_open_close_bytes_and_mode(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    struct termios before, during, after;
    tcgetattr(p.slave, &before);

    tui_term *t = open_on(&p, 0, 0);
    ASSERT(t != NULL, "open");
    char out[256];
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_BYTES(out, strlen(out), ENTER_DEFAULT, "enter: alternate screen, hide cursor, no wrap");
    tcgetattr(p.slave, &during);
    ASSERT_EQ_INT(during.c_lflag & (ICANON | ECHO | ISIG | IEXTEN), 0, "raw lflag");
    ASSERT_EQ_INT(during.c_iflag & (IXON | ICRNL), 0, "no flow control, no CR translation");
    ASSERT_EQ_INT(tui_term_fd(t), p.slave, "tui_term_fd");

    ASSERT_EQ_INT(tui_term_close(t), 0, "close");
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_BYTES(out, strlen(out), LEAVE_DEFAULT, "leave: reset, wrap, cursor, main screen");
    tcgetattr(p.slave, &after);
    ASSERT(same_mode(&before, &after), "mode restored exactly");
    ASSERT_EQ_INT(tui_term_close(NULL), 0, "close NULL");
    ASSERT_EQ_INT(tui_term_fd(NULL), -1, "fd of NULL");
    tpair_close(&p);
}

static void test_flags_select_sequences(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_term_opts o;
    tui_term_opts_init(&o);
    o.in_fd = o.out_fd = p.slave;
    o.flags = TUI_TERM_PASTE;
    tui_term *t = tui_term_open(&o);
    ASSERT(t != NULL, "open");
    char out[256];
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_BYTES(out, strlen(out), "\033[?2004h", "only bracketed paste");
    tui_term_close(t);
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_BYTES(out, strlen(out), "\033[0m\033[?2004l", "only its inverse");

    o.flags = TUI_TERM_KEEP_ISIG | TUI_TERM_KEEP_IXON;
    t = tui_term_open(&o);
    ASSERT(t != NULL, "open keeping ISIG and IXON");
    struct termios m;
    tcgetattr(p.slave, &m);
    ASSERT((m.c_lflag & ISIG) && (m.c_iflag & IXON), "ISIG and IXON kept");
    ASSERT_EQ_INT(m.c_lflag & ICANON, 0, "still raw");
    tui_term_close(t);
    tpair_close(&p);
}

static void test_open_errors(void) {
    int fds[2];
    ASSERT_EQ_INT(pipe(fds), 0, "pipe");
    tui_term_opts o;
    tui_term_opts_init(&o);
    o.in_fd = fds[0];
    o.out_fd = fds[1];
    errno = 0;
    ASSERT(tui_term_open(&o) == NULL, "open on a pipe fails");
    ASSERT_EQ_INT(errno, ENOTTY, "errno ENOTTY (the program picks its non-TTY fallback)");
    close(fds[0]);
    close(fds[1]);

    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_term *a = open_on(&p, 0, 0);
    ASSERT(a != NULL, "first session");
    errno = 0;
    tui_term *b = open_on(&p, 0, 0);
    ASSERT(b == NULL, "a second session with signal handling is refused");
    ASSERT_EQ_INT(errno, EBUSY, "errno EBUSY");
    tui_term *c = open_on(&p, TUI_TERM_NO_SIGNALS, 0);
    ASSERT(c != NULL, "a session without signal handling is allowed");
    tui_term_close(c);
    tui_term_close(a);
    tui_term *d = open_on(&p, 0, 0);
    ASSERT(d != NULL, "after close, a new session may take the signals");
    tui_term_close(d);
    tpair_close(&p);
}

static void test_size(void) {
    TPair p;
    ASSERT(tpair_open(&p, 33, 111) == 0, "openpty");
    tui_term *t = open_on(&p, 0, 0);
    ASSERT(t != NULL, "open");
    int rows = 0, cols = 0;
    ASSERT_EQ_INT(tui_term_size(t, &rows, &cols), 0, "size");
    ASSERT_EQ_INT(rows, 33, "rows");
    ASSERT_EQ_INT(cols, 111, "cols");
    tpair_resize(&p, 0, 0);
    errno = 0;
    ASSERT_EQ_INT(tui_term_size(t, &rows, &cols), -1, "unknown size is an error, not 80x24");
    ASSERT_EQ_INT(errno, ENOTTY, "errno");
    ASSERT_EQ_INT(tui_term_size(NULL, &rows, &cols), -1, "NULL session");
    tui_term_close(t);
    tpair_close(&p);
}

static void test_write_and_flush(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_term *t = open_on(&p, TUI_TERM_NO_SIGNALS, 0);
    ASSERT(t != NULL, "open");
    char out[256];
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_EQ_INT(tui_term_write(t, "abc", 3), 0, "write");
    tui_buf b;
    tui_buf_init(&b);
    tui_buf_puts(&b, "\033[2;3Hxy");
    ASSERT_EQ_INT(tui_term_flush_buf(t, &b), 0, "flush");
    ASSERT_EQ_INT(b.len, 0, "buffer emptied");
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_BYTES(out, strlen(out), "abc\033[2;3Hxy", "written bytes, unchanged");
    ASSERT_EQ_INT(tui_term_write(NULL, "x", 1), -1, "write NULL session");
    ASSERT_EQ_INT(tui_term_write(t, NULL, 1), -1, "write NULL bytes");
    ASSERT_EQ_INT(tui_term_flush_buf(t, NULL), -1, "flush NULL buffer");
    tui_buf_free(&b);
    tui_term_close(t);
    tpair_close(&p);
}

static void test_wait_keys_and_timeout(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_term *t = open_on(&p, 0, 0);
    ASSERT(t != NULL, "open");
    tui_event ev;
    char name[64];

    int64_t t0 = test_now_ms();
    ASSERT_EQ_INT(tui_term_wait(t, 60, NULL, 0, &ev), 0, "timeout with no input");
    ASSERT(test_now_ms() - t0 >= 55, "waited the timeout");
    ASSERT_EQ_INT(tui_term_wait(t, 0, NULL, 0, &ev), 0, "zero timeout polls");

    tpair_type(&p, "q\x1b[1;5A\x13");
    ASSERT_EQ_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "q");
    ASSERT(TUI_EV_IS_CHAR(&ev, 'q'), "q decoded");
    ASSERT_EQ_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "Ctrl+Up");
    ASSERT_EQ_STR(tui_event_name(&ev, name, sizeof(name)), "Ctrl+Up", "Ctrl+Up decoded");
    ASSERT_EQ_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "Ctrl+S");
    ASSERT(TUI_EV_IS_CTRL(&ev, 's'), "Ctrl+S reaches the program (issue-manager BUG-208)");

    /* A lone ESC: after the ESC delay, not before, not never. */
    tpair_type(&p, "\x1b");
    t0 = test_now_ms();
    ASSERT_EQ_INT(tui_term_wait(t, -1, NULL, 0, &ev), 1, "Esc");
    int64_t dt = test_now_ms() - t0;
    ASSERT_EQ_INT(ev.key, TUI_KEY_ESC, "Esc decoded");
    ASSERT(dt >= 95 && dt < 1000, "Esc reported after the 100 ms ESC delay");

    /* The rest of a sequence arriving within the delay completes it. */
    tpair_type(&p, "\x1b[");
    ASSERT_EQ_INT(tui_term_wait(t, 20, NULL, 0, &ev), 0, "half a sequence is no event yet");
    tpair_type(&p, "B");
    ASSERT_EQ_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "completed");
    ASSERT_EQ_INT(ev.key, TUI_KEY_DOWN, "Down, not Esc");

    /* An unfinished sequence that never completes is dropped after the
     * delay, and the wait goes on to its own timeout. */
    tpair_type(&p, "\x1b[1;");
    t0 = test_now_ms();
    ASSERT_EQ_INT(tui_term_wait(t, 300, NULL, 0, &ev), 0, "dropped, then the timeout");
    ASSERT(test_now_ms() - t0 >= 290, "waited the whole timeout");
    tpair_type(&p, "z");
    ASSERT_EQ_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "the next key");
    ASSERT(TUI_EV_IS_CHAR(&ev, 'z'), "z, nothing of the dropped sequence");

    ASSERT_EQ_INT(tui_term_wait(NULL, 0, NULL, 0, &ev), -1, "NULL session");
    ASSERT_EQ_INT(tui_term_wait(t, 0, NULL, 0, NULL), -1, "NULL event");
    ASSERT_EQ_INT(tui_term_wait(t, 0, NULL, 2, &ev), -1, "extra count without array");
    tui_term_close(t);
    tpair_close(&p);
}

static void test_custom_esc_delay(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_term *t = open_on(&p, 0, 25);
    ASSERT(t != NULL, "open");
    tui_event ev;
    tpair_type(&p, "\x1b");
    int64_t t0 = test_now_ms();
    ASSERT_EQ_INT(tui_term_wait(t, -1, NULL, 0, &ev), 1, "Esc");
    int64_t dt = test_now_ms() - t0;
    ASSERT(dt >= 20 && dt < 95, "Esc after the configured 25 ms");
    tui_term_close(t);
    tpair_close(&p);
}

static void test_resize_events(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_term *t = open_on(&p, 0, 0);
    ASSERT(t != NULL, "open");
    tui_event ev;
    tpair_resize(&p, 30, 100);
    raise(SIGWINCH);
    raise(SIGWINCH);
    ASSERT_EQ_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "resize event");
    ASSERT_EQ_INT(ev.type, TUI_EV_RESIZE, "type");
    ASSERT_EQ_INT(ev.rows, 30, "new rows");
    ASSERT_EQ_INT(ev.cols, 100, "new cols");
    ASSERT_EQ_INT(tui_term_wait(t, 50, NULL, 0, &ev), 0, "two signals, one event");

    /* issue-manager BUG-240: a resize that arrives while the program is
     * drawing -- not waiting -- must still wake the next wait. */
    tpair_resize(&p, 40, 120);
    raise(SIGWINCH);
    usleep(20 * 1000);                     /* "drawing" */
    ASSERT_EQ_INT(tui_term_wait(t, 0, NULL, 0, &ev), 1, "resize delivered to a later wait");
    ASSERT_EQ_INT(ev.type, TUI_EV_RESIZE, "type");
    ASSERT_EQ_INT(ev.rows, 40, "rows");

    /* Resize while blocked in the wait. */
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        usleep(50 * 1000);
        tpair_resize(&p, 20, 60);
        kill(getppid(), SIGWINCH);
        _exit(0);
    }
    ASSERT_EQ_INT(tui_term_wait(t, 3000, NULL, 0, &ev), 1, "woken in the wait");
    ASSERT_EQ_INT(ev.type, TUI_EV_RESIZE, "type");
    ASSERT_EQ_INT(ev.cols, 60, "cols");
    waitpid(pid, NULL, 0);

    /* A size that cannot be read is reported as an error, not as 0x0. */
    tpair_resize(&p, 0, 0);
    raise(SIGWINCH);
    errno = 0;
    ASSERT_EQ_INT(tui_term_wait(t, 1000, NULL, 0, &ev), -1, "unreadable size");
    ASSERT_EQ_INT(errno, ENOTTY, "errno");
    tui_term_close(t);
    tpair_close(&p);
}

/* A SIGWINCH handler the program had before is still called. */
static volatile sig_atomic_t g_own_winch;
static void own_winch(int sig) { (void)sig; g_own_winch++; }

static void test_resize_chains_previous_handler(void) {
    struct sigaction sa, old;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = own_winch;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGWINCH, &sa, &old);
    g_own_winch = 0;
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_term *t = open_on(&p, 0, 0);
    ASSERT(t != NULL, "open");
    raise(SIGWINCH);
    tui_event ev;
    ASSERT_EQ_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "resize event");
    ASSERT_EQ_INT(g_own_winch, 1, "the program's own handler ran too");
    tui_term_close(t);
    struct sigaction now;
    sigaction(SIGWINCH, NULL, &now);
    ASSERT(now.sa_handler == own_winch, "the program's handler is back after close");
    sigaction(SIGWINCH, &old, NULL);
    tpair_close(&p);
}

static void test_extra_fds(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_term *t = open_on(&p, 0, 0);
    ASSERT(t != NULL, "open");
    int a[2], b[2];
    ASSERT_EQ_INT(pipe(a), 0, "pipe a");
    ASSERT_EQ_INT(pipe(b), 0, "pipe b");
    struct pollfd extra[2] = { { .fd = a[0], .events = POLLIN }, { .fd = b[0], .events = POLLIN } };
    tui_event ev;
    ASSERT_EQ_INT(tui_term_wait(t, 30, extra, 2, &ev), 0, "nothing ready");
    ASSERT_EQ_INT(write(b[1], "x", 1), 1, "write b");
    ASSERT_EQ_INT(tui_term_wait(t, 1000, extra, 2, &ev), 1, "event");
    ASSERT_EQ_INT(ev.type, TUI_EV_FD, "type");
    ASSERT_EQ_INT(ev.fd_index, 1, "index of b");
    ASSERT(extra[1].revents & POLLIN, "revents filled in");
    /* Keys and an extra fd both ready: the key is not lost. */
    tpair_type(&p, "k");
    usleep(10 * 1000);
    int got_key = 0, got_fd = 0;
    for (int i = 0; i < 2; i++) {
        ASSERT_EQ_INT(tui_term_wait(t, 1000, extra, 2, &ev), 1, "event");
        if (ev.type == TUI_EV_KEY && ev.cp == 'k') got_key = 1;
        if (ev.type == TUI_EV_FD) got_fd = 1;
    }
    ASSERT(got_key && got_fd, "both the key and the fd reported");
    /* Many extra fds: beyond the built-in array. */
    struct pollfd many[20];
    int fds[20][2];
    for (int i = 0; i < 20; i++) {
        ASSERT_EQ_INT(pipe(fds[i]), 0, "pipe");
        many[i].fd = fds[i][0];
        many[i].events = POLLIN;
    }
    ASSERT_EQ_INT(write(fds[17][1], "y", 1), 1, "write 17");
    ASSERT_EQ_INT(tui_term_wait(t, 1000, many, 20, &ev), 1, "event among 20");
    ASSERT_EQ_INT(ev.fd_index, 17, "index 17");
    for (int i = 0; i < 20; i++) { close(fds[i][0]); close(fds[i][1]); }
    close(a[0]); close(a[1]); close(b[0]); close(b[1]);
    tui_term_close(t);
    tpair_close(&p);
}

static void test_paste(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_term *t = open_on(&p, TUI_TERM_PASTE, 0);
    ASSERT(t != NULL, "open");
    tui_event ev;
    tpair_type(&p, "\x1b[200~line 1\rline 2\x1b[201~");
    ASSERT_EQ_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "paste");
    ASSERT_EQ_INT(ev.type, TUI_EV_PASTE, "type");
    ASSERT_EQ_INT(ev.paste_len, 13, "length");
    ASSERT(memcmp(ev.paste, "line 1\rline 2", 13) == 0, "content, CR kept as text");
    tui_term_close(t);
    tpair_close(&p);
}

static void test_hangup(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_term *t = open_on(&p, 0, 0);
    ASSERT(t != NULL, "open");
    tpair_close_master(&p);
    tui_event ev;
    int64_t t0 = test_now_ms();
    ASSERT_EQ_INT(tui_term_wait(t, 2000, NULL, 0, &ev), 1, "hang-up reported");
    ASSERT_EQ_INT(ev.type, TUI_EV_HANGUP, "type");
    ASSERT(test_now_ms() - t0 < 1000, "at once, not at the timeout");
    ASSERT_EQ_INT(tui_term_wait(t, 2000, NULL, 0, &ev), 1, "reported again");
    ASSERT_EQ_INT(ev.type, TUI_EV_HANGUP, "still hang-up, no spinning on reads");
    tui_term_close(t);
    tpair_close(&p);
}

static void test_suspend_resume(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    struct termios before, m;
    tcgetattr(p.slave, &before);
    tui_term *t = open_on(&p, 0, 0);
    ASSERT(t != NULL, "open");
    char out[256];
    tpair_read_all(&p, out, sizeof(out), 50);

    ASSERT_EQ_INT(tui_term_suspend(t), 0, "suspend");
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_BYTES(out, strlen(out), LEAVE_DEFAULT, "suspend leaves the screen");
    tcgetattr(p.slave, &m);
    ASSERT(same_mode(&before, &m), "suspended: original mode");
    ASSERT_EQ_INT(tui_term_suspend(t), 0, "suspend twice");
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_EQ_INT(strlen(out), 0, "second suspend writes nothing");

    ASSERT_EQ_INT(tui_term_resume(t), 0, "resume");
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_BYTES(out, strlen(out), ENTER_DEFAULT, "resume enters again");
    tcgetattr(p.slave, &m);
    ASSERT_EQ_INT(m.c_lflag & ICANON, 0, "raw again");
    tui_event ev;
    ASSERT_EQ_INT(tui_term_wait(t, 0, NULL, 0, &ev), 1, "an event after resume");
    ASSERT_EQ_INT(ev.type, TUI_EV_RESIZE, "a resize: repaint everything");
    ASSERT_EQ_INT(tui_term_resume(t), 0, "resume when not suspended");

    /* Closing a suspended session leaves the terminal as it is. */
    ASSERT_EQ_INT(tui_term_suspend(t), 0, "suspend");
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_EQ_INT(tui_term_close(t), 0, "close while suspended");
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_EQ_INT(strlen(out), 0, "nothing written");
    tcgetattr(p.slave, &m);
    ASSERT(same_mode(&before, &m), "original mode");

    /* The same without signal handling. */
    t = open_on(&p, TUI_TERM_NO_SIGNALS, 0);
    ASSERT(t != NULL, "open without signals");
    ASSERT_EQ_INT(tui_term_suspend(t), 0, "suspend");
    tcgetattr(p.slave, &m);
    ASSERT(same_mode(&before, &m), "suspended: original mode");
    ASSERT_EQ_INT(tui_term_resume(t), 0, "resume");
    tcgetattr(p.slave, &m);
    ASSERT_EQ_INT(m.c_lflag & ICANON, 0, "raw again");
    ASSERT_EQ_INT(tui_term_suspend(t), 0, "suspend");
    ASSERT_EQ_INT(tui_term_close(t), 0, "close while suspended");
    tcgetattr(p.slave, &m);
    ASSERT(same_mode(&before, &m), "original mode");
    ASSERT_EQ_INT(tui_term_suspend(NULL), -1, "suspend NULL");
    ASSERT_EQ_INT(tui_term_resume(NULL), -1, "resume NULL");
    tpair_close(&p);
}

/* ── Signals, in a child ─────────────────────────────────────────────── */

typedef enum { END_SIGNAL, END_EXIT_NO_CLOSE } Ending;

/* Child: open a session, then die by @p sig (or exit without closing).
 * Parent: the child's fate, the bytes it wrote, the mode it left. */
static void check_restored_after(int sig, Ending how) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    struct termios before, after;
    tcgetattr(p.slave, &before);
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        struct rlimit no_core = { 0, 0 };
        setrlimit(RLIMIT_CORE, &no_core);          /* SIGQUIT: no core file */
        tui_term *t = open_on(&p, 0, 0);
        if (!t) _exit(90);
        if (how == END_EXIT_NO_CLOSE) exit(7);
        raise(sig);
        _exit(91);                                 /* not reached */
    }
    int st = 0;
    waitpid(pid, &st, 0);
    char what[96];
    if (how == END_SIGNAL) {
        snprintf(what, sizeof(what), "child killed by signal %d (status 0x%x)", sig, st);
        ASSERT(WIFSIGNALED(st) && WTERMSIG(st) == sig, what);
    } else {
        snprintf(what, sizeof(what), "child exited 7 (status 0x%x)", st);
        ASSERT(WIFEXITED(st) && WEXITSTATUS(st) == 7, what);
    }
    char out[256];
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_BYTES(out, strlen(out), ENTER_DEFAULT LEAVE_DEFAULT, "entered, then left");
    tcgetattr(p.slave, &after);
    ASSERT(same_mode(&before, &after), "terminal mode restored");
    tpair_close(&p);
}

static void test_sigterm_restores(void) { check_restored_after(SIGTERM, END_SIGNAL); }
static void test_sighup_restores(void)  { check_restored_after(SIGHUP, END_SIGNAL); }
static void test_sigint_restores(void)  { check_restored_after(SIGINT, END_SIGNAL); }
static void test_sigquit_restores(void) { check_restored_after(SIGQUIT, END_SIGNAL); }
static void test_exit_restores(void)    { check_restored_after(0, END_EXIT_NO_CLOSE); }

/* mamind keeps Ctrl-C as SIGINT with its own handler setting a flag: the
 * library must leave such a handler alone, and the terminal raw. */
static volatile sig_atomic_t g_interrupted;
static void on_interrupt(int sig) { (void)sig; g_interrupted = 1; }

static void test_program_handler_kept(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = on_interrupt;
        sigemptyset(&sa.sa_mask);
        sigaction(SIGINT, &sa, NULL);
        signal(SIGHUP, SIG_IGN);                   /* nohup */
        tui_term *t = open_on(&p, TUI_TERM_KEEP_ISIG, 0);
        if (!t) _exit(90);
        raise(SIGINT);
        raise(SIGHUP);
        struct termios m;
        tcgetattr(p.slave, &m);
        int ok = g_interrupted && !(m.c_lflag & ICANON);
        struct sigaction now;
        sigaction(SIGHUP, NULL, &now);
        ok = ok && now.sa_handler == SIG_IGN;
        tui_term_close(t);
        exit(ok ? 0 : 1);   /* exit, not _exit: coverage data is written */
    }
    int st = 0;
    waitpid(pid, &st, 0);
    ASSERT(WIFEXITED(st), "child survived SIGINT (its handler) and SIGHUP (ignored)");
    ASSERT_EQ_INT(WEXITSTATUS(st), 0, "handler ran, terminal stayed raw, SIG_IGN kept");
    tpair_close(&p);
}

/* Ctrl-Z: SIGTSTP gives the terminal back and stops; SIGCONT takes it
 * over again and reports a resize so the program repaints. */
static void test_sigtstp_and_continue(void) {
    /* Measured: under Valgrind, raise(SIGTSTP) with the default action does
     * not stop the process at all, with or without libtuikit.  The stop
     * cannot be observed there; the test says so instead of failing on the
     * tool or passing in silence.  Natively and under ASAN it runs whole. */
    if (getenv("TUIKIT_TEST_UNDER_VALGRIND")) {
        printf("  NOTE: SIGTSTP stops are not emulated by Valgrind; "
               "test_sigtstp_and_continue not checked in this run\n");
        return;
    }
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    struct termios before, m;
    tcgetattr(p.slave, &before);
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        /* A process group of its own whose parent is in another group of
         * the same session: not orphaned, so SIGTSTP really stops it. */
        setpgid(0, 0);
        tui_term *t = open_on(&p, 0, 0);
        if (!t) _exit(90);
        raise(SIGTSTP);
        tui_event ev;
        int rc = tui_term_wait(t, 2000, NULL, 0, &ev);
        struct termios r;
        tcgetattr(p.slave, &r);
        int ok = rc == 1 && ev.type == TUI_EV_RESIZE && !(r.c_lflag & ICANON);
        tui_term_close(t);
        exit(ok ? 0 : 1);   /* exit, not _exit: coverage data is written */
    }
    int st = 0;
    waitpid(pid, &st, WUNTRACED);
    char what[128];
    snprintf(what, sizeof(what), "child stopped by SIGTSTP (status 0x%x: %s %d)", st,
             WIFEXITED(st) ? "exited" : WIFSIGNALED(st) ? "killed by" : "stopped by",
             WIFEXITED(st) ? WEXITSTATUS(st) : WIFSIGNALED(st) ? WTERMSIG(st) : WSTOPSIG(st));
    ASSERT(WIFSTOPPED(st), what);
    char out[256];
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_BYTES(out, strlen(out), ENTER_DEFAULT LEAVE_DEFAULT, "screen left before stopping");
    tcgetattr(p.slave, &m);
    ASSERT(same_mode(&before, &m), "terminal usable by the shell while stopped");
    kill(pid, SIGCONT);
    waitpid(pid, &st, 0);
    ASSERT(WIFEXITED(st), "child exited");
    ASSERT_EQ_INT(WEXITSTATUS(st), 0, "raw again and a resize event after SIGCONT");
    tpair_read_all(&p, out, sizeof(out), 50);
    ASSERT_BYTES(out, strlen(out), ENTER_DEFAULT LEAVE_DEFAULT, "entered again, then closed");
    tpair_close(&p);
}

/* SIGSTOP from outside, and a shell that resets the terminal meanwhile:
 * on SIGCONT the session takes the terminal over again. */
static void test_sigstop_then_cont(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    struct termios before;
    tcgetattr(p.slave, &before);
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        tui_term *t = open_on(&p, 0, 0);
        if (!t) _exit(90);
        tui_event ev;
        int rc = tui_term_wait(t, 5000, NULL, 0, &ev);
        struct termios r;
        tcgetattr(p.slave, &r);
        int ok = rc == 1 && ev.type == TUI_EV_RESIZE && !(r.c_lflag & ICANON);
        tui_term_close(t);
        exit(ok ? 0 : 1);   /* exit, not _exit: coverage data is written */
    }
    char out[256];
    ASSERT(tpair_read_until(&p, ENTER_DEFAULT, out, sizeof(out), 3000) == 0, "child entered");
    usleep(30 * 1000);
    kill(pid, SIGSTOP);
    int st = 0;
    waitpid(pid, &st, WUNTRACED);
    ASSERT(WIFSTOPPED(st), "child stopped");
    tcsetattr(p.slave, TCSANOW, &before);           /* the "shell" resets it */
    kill(pid, SIGCONT);
    waitpid(pid, &st, 0);
    ASSERT(WIFEXITED(st), "child exited");
    ASSERT_EQ_INT(WEXITSTATUS(st), 0, "raw again and a resize event after SIGCONT");
    tpair_close(&p);
}

static const TestCase tests[] = {
    TEST_CASE(test_opts_defaults),
    TEST_CASE(test_open_close_bytes_and_mode),
    TEST_CASE(test_flags_select_sequences),
    TEST_CASE(test_open_errors),
    TEST_CASE(test_size),
    TEST_CASE(test_write_and_flush),
    TEST_CASE(test_wait_keys_and_timeout),
    TEST_CASE(test_custom_esc_delay),
    TEST_CASE(test_resize_events),
    TEST_CASE(test_resize_chains_previous_handler),
    TEST_CASE(test_extra_fds),
    TEST_CASE(test_paste),
    TEST_CASE(test_hangup),
    TEST_CASE(test_suspend_resume),
    TEST_CASE(test_sigterm_restores),
    TEST_CASE(test_sighup_restores),
    TEST_CASE(test_sigint_restores),
    TEST_CASE(test_sigquit_restores),
    TEST_CASE(test_exit_restores),
    TEST_CASE(test_program_handler_kept),
    TEST_CASE(test_sigtstp_and_continue),
    TEST_CASE(test_sigstop_then_cont),
};

int main(int argc, char **argv) {
    return RUN_SUITE("term", tests, 200, 90);
}
