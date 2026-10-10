/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Regression tests carried over from the projects libtuikit replaces. */

/*
 * Regression tests carried over from the projects libtuikit replaces, so
 * that "email-cli is the reference" and "tg-cli loses nothing" are checked
 * rather than claimed:
 *
 *   - email-cli tests/unit/test_input_line.c: all 34 cases, run through
 *     tui_lineedit_init() / tui_lineedit_run() with the keys on a pipe, as
 *     the original ran input_line_run() on a redirected stdin;
 *   - tg-cli tests/unit/test_readline.c: the history API and the non-TTY
 *     path, through tui_history_* and tui_readline().
 *
 * One deliberate difference is spelled out where it occurs (case 9).
 */

#include "test_helpers.h"
#include "tty_fixture.h"
#include "tuikit/lineedit.h"
#include "tuikit/term.h"
#include "tuikit/tty.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/time.h>

int g_tests_run = 0;
int g_tests_failed = 0;

/* Runs tui_lineedit_run() with @p keys on a pipe and the drawing sent to
 * /dev/null, as email-cli's run_with_keys() did. */
static int run_with_keys(tui_lineedit *le, int trow, const char *prompt,
                         const char *keys, size_t keylen) {
    int p[2];
    if (pipe(p) != 0) return -99;
    if (write(p[1], keys, keylen) != (ssize_t)keylen) {
        close(p[0]);
        close(p[1]);
        return -99;
    }
    close(p[1]);
    int null_fd = open("/dev/null", O_WRONLY);
    if (null_fd < 0) {
        close(p[0]);
        return -99;
    }
    int res = tui_lineedit_run(le, p[0], null_fd, trow, 3, prompt);
    close(p[0]);
    close(null_fd);
    return res;
}

/* email-cli cases 1-7: initialisation. */
static void test_email_init(void) {
    char buf[64];
    tui_lineedit il;
    tui_lineedit_init(&il, buf, sizeof(buf), NULL);
    ASSERT(il.buf == buf, "1. buf pointer set");
    ASSERT_EQ_INT(il.size, sizeof(buf), "1. bufsz set");
    ASSERT_EQ_INT(il.len, 0, "1. len 0 for NULL initial");
    ASSERT_EQ_INT(il.cur, 0, "1. cur 0 for NULL initial");
    ASSERT_EQ_INT(buf[0], 0, "1. buf NUL-terminated");
    tui_lineedit_init(&il, buf, sizeof(buf), "");
    ASSERT_EQ_INT(il.len, 0, "2. len 0 for empty initial");
    ASSERT_EQ_INT(il.cur, 0, "2. cur 0 for empty initial");
    tui_lineedit_init(&il, buf, sizeof(buf), "hello");
    ASSERT_EQ_INT(il.len, 5, "3. len 5 for 'hello'");
    ASSERT_EQ_INT(il.cur, 5, "3. cur at end after init");
    ASSERT_EQ_STR(buf, "hello", "3. buf contains 'hello'");
    char small[4];
    tui_lineedit_init(&il, small, sizeof(small), "toolong");
    ASSERT_EQ_INT(il.len, 3, "4. len truncated to bufsz-1");
    ASSERT_EQ_INT(small[3], 0, "4. buf NUL-terminated after truncation");
    tui_lineedit_init(&il, buf, sizeof(buf), "x");
    ASSERT(il.tab_fn == NULL, "5. tab_fn NULL after init");
    ASSERT(il.shift_tab_fn == NULL, "5. shift_tab_fn NULL after init");
    ASSERT(il.render_below == NULL, "5. render_below NULL after init");
    ASSERT_EQ_INT(il.trow, 0, "6. trow 0 after init");
    tui_lineedit_init(&il, buf, sizeof(buf), "h\xC3\xA9llo");
    ASSERT_EQ_INT(il.len, 6, "7. len accounts for UTF-8 bytes");
    ASSERT_EQ_INT(il.cur, 6, "7. cur at byte end for UTF-8 text");
    ASSERT_EQ_STR(buf, "h\xC3\xA9llo", "7. UTF-8 content preserved");
}

typedef struct {
    int         n;          /* email-cli's case number */
    const char *initial;    /* NULL: empty */
    size_t      bufsz;
    const char *keys;
    size_t      keylen;
    int         result;
    const char *expect;     /* NULL: not checked */
} RunCase;

/* email-cli cases 8-34: input_line_run() with injected keys. */
static const RunCase g_run_cases[] = {
    {  8, "hello", 64, "\r", 1, 1, "hello" },
    { 10, "test", 64, "\x03", 1, 0, NULL },
    { 11, NULL, 64, "\r", 1, 1, "" },
    { 12, NULL, 64, "hi\r", 3, 1, "hi" },
    { 13, "ab", 64, "\x7f\r", 2, 1, "a" },
    { 14, NULL, 64, "\x7f\r", 2, 1, "" },
    { 15, "ab", 64, "\033[D\x7f\r", 5, 1, "b" },
    { 16, "hi", 64, "\033[C\r", 4, 1, "hi" },
    { 17, "bc", 64, "\033[Ha\r", 5, 1, "abc" },
    { 18, "hi", 64, "\033[H\033[F!\r", 8, 1, "hi!" },
    { 19, "abc", 64, "\033[H\033[3~\r", 8, 1, "bc" },
    { 20, "hi", 64, "\033[3~\r", 5, 1, "hi" },
    { 21, "abc", 4, "d\r", 2, 1, "abc" },
    { 22, "\xC3\xA9", 64, "\r", 1, 1, "\xC3\xA9" },
    { 23, "\xE4\xB8\xAD", 64, "\r", 1, 1, "\xE4\xB8\xAD" },
    { 24, "\xF0\x9F\x98\x80", 64, "\r", 1, 1, "\xF0\x9F\x98\x80" },
    { 25, "\xC3\xA9", 64, "\033[D\r", 4, 1, "\xC3\xA9" },
    { 26, "\xE4\xB8\xAD" "x", 64, "\033[H\033[3~\r", 8, 1, "x" },
    { 27, "hi", 64, "\033[Z\r", 4, 1, "hi" },
    { 28, "bc", 64, "\033[1~a\r", 6, 1, "abc" },
    { 29, "hi", 64, "\033[1x\r", 5, 1, "hi" },
    { 30, "hi", 64, "\033[H\033[4~!\r", 9, 1, "hi!" },
    { 31, "bc", 64, "\033[7~a\r", 6, 1, "abc" },
    { 32, "hi", 64, "\033[H\033[8~!\r", 9, 1, "hi!" },
    { 33, "hi", 64, "\033[9x\r", 5, 1, "hi" },
    { 34, "hi", 64, "\t\r", 2, 1, "hi" },
};

static void test_email_run_cases(void) {
    for (size_t i = 0; i < sizeof(g_run_cases) / sizeof(g_run_cases[0]); i++) {
        const RunCase *c = &g_run_cases[i];
        char buf[64];
        tui_lineedit il;
        tui_lineedit_init(&il, buf, c->bufsz, c->initial);
        int res = run_with_keys(&il, 5, "> ", c->keys, c->keylen);
        char what[64];
        snprintf(what, sizeof(what), "email-cli case %d: result", c->n);
        ASSERT_EQ_INT(res, c->result, what);
        if (c->expect) {
            snprintf(what, sizeof(what), "email-cli case %d: buffer", c->n);
            ASSERT_EQ_STR(buf, c->expect, what);
        }
        ASSERT_EQ_INT(il.trow, 5, "trow set by run");
        tui_lineedit_free(&il);
    }
}

/* email-cli case 9.  The original sent "\033x": its decoder took ESC
 * followed by anything but '[' as a bare ESC and consumed the second byte.
 * libtuikit decodes ESC x arriving together as Alt+x (xterm's
 * metaSendsEscape), which the line editor ignores; a bare ESC is an ESC
 * after the ESC delay -- or at once when the input ends behind it. */
static void test_email_case_9_escape(void) {
    char buf[64];
    tui_lineedit il;
    tui_lineedit_init(&il, buf, sizeof(buf), "hello");
    ASSERT_EQ_INT(run_with_keys(&il, 5, "> ", "\033", 1), 0, "9. a bare ESC returns 0");
    ASSERT_EQ_STR(buf, "hello", "9. buffer unchanged after ESC");
    tui_lineedit_init(&il, buf, sizeof(buf), "hello");
    ASSERT_EQ_INT(run_with_keys(&il, 5, "> ", "\033x", 2), -1,
                  "9'. ESC x is Alt+x: ignored, then the input ends (-1)");
    ASSERT_EQ_STR(buf, "hello", "9'. Alt+x inserts nothing");
    tui_lineedit_free(&il);
}

/* ── tg-cli tests/unit/test_readline.c ───────────────────────────────── */

static void test_tg_history(void) {
    tui_history h;
    tui_history_init(&h, 256);
    ASSERT_EQ_INT(tui_history_count(&h), 0, "history_init: count 0");
    tui_history_init(NULL, 256);                     /* must not crash */
    tui_history_add(&h, "hello");
    tui_history_add(&h, "world");
    ASSERT_EQ_INT(tui_history_count(&h), 2, "two adds");
    tui_history_add(&h, "");
    ASSERT_EQ_INT(tui_history_count(&h), 2, "empty not added");
    tui_history_add(&h, NULL);
    ASSERT_EQ_INT(tui_history_count(&h), 2, "NULL not added");
    tui_history_free(&h);
    tui_history_init(&h, 256);
    tui_history_add(&h, "alpha");
    tui_history_add(&h, "alpha");
    ASSERT_EQ_INT(tui_history_count(&h), 1, "consecutive duplicate not added");
    tui_history_add(&h, "beta");
    tui_history_add(&h, "alpha");
    ASSERT_EQ_INT(tui_history_count(&h), 3, "non-consecutive duplicate added");
    tui_history_free(&h);
    tui_history_init(&h, 256);
    for (int i = 0; i < 256 + 10; i++) {
        char b[16];
        snprintf(b, sizeof(b), "entry %d", i);
        tui_history_add(&h, b);
    }
    ASSERT_EQ_INT(tui_history_count(&h), 256, "wrap: never above the maximum (RL_HISTORY_MAX)");
    ASSERT_EQ_STR(tui_history_get(&h, 0), "entry 10", "wrap: the oldest went first");
    tui_history_free(&h);
}

static int readline_from(const char *input, size_t n, char *buf, size_t size) {
    int p[2];
    if (pipe(p) != 0) return -99;
    if (n && write(p[1], input, n) != (ssize_t)n) return -99;
    close(p[1]);
    int null_fd = open("/dev/null", O_WRONLY);
    tui_readline_opts o;
    tui_readline_opts_init(&o);
    o.in_fd = p[0];
    o.out_fd = null_fd;
    int rc = tui_readline(NULL, buf, size, &o);
    close(p[0]);
    close(null_fd);
    return rc;
}

static void test_tg_readline_non_tty(void) {
    char buf[64];
    ASSERT(tui_readline(">> ", NULL, 64, NULL) < 0, "NULL buffer");
    ASSERT(tui_readline(">> ", buf, 0, NULL) < 0, "zero size");
    ASSERT(readline_from("hello pipe\n", 11, buf, sizeof(buf)) >= 0, "pipe input");
    ASSERT_EQ_STR(buf, "hello pipe", "line read");
    ASSERT(readline_from("test\r\n", 6, buf, sizeof(buf)) >= 0, "CR LF");
    ASSERT_EQ_STR(buf, "test", "CR and LF stripped");
    ASSERT(readline_from("", 0, buf, sizeof(buf)) < 0, "EOF returns a negative value");
    ASSERT_EQ_INT(readline_from("", 0, buf, sizeof(buf)), TUI_READLINE_EOF, "EOF is TUI_READLINE_EOF");
}

/* ── Additions made while carrying the tests over ────────────────────── */

/* tg-cli's terminal_wait_key(): and the interruption it could not report. */
static void on_alarm(int sig) { (void)sig; }

static void test_tty_wait(void) {
    int p[2];
    ASSERT_EQ_INT(pipe(p), 0, "pipe");
    ASSERT_EQ_INT(tui_tty_wait(p[0], 30), 0, "timeout with no input");
    ASSERT_EQ_INT(write(p[1], "x", 1), 1, "write");
    ASSERT_EQ_INT(tui_tty_wait(p[0], 1000), 1, "input ready");
    ASSERT_EQ_INT(tui_tty_wait(p[0], -1), 1, "no limit, input ready");
    char c;
    ASSERT_EQ_INT(read(p[0], &c, 1), 1, "read");
    close(p[1]);
    ASSERT_EQ_INT(tui_tty_wait(p[0], 1000), 1, "end of input: a read will not block");
    close(p[0]);
    errno = 0;
    ASSERT_EQ_INT(tui_tty_wait(p[0], 0), -1, "closed descriptor");
    ASSERT_EQ_INT(errno, EBADF, "errno EBADF");

    /* A signal during the wait is EINTR, not an error that looks like a
     * timeout (survey defect 6, TG-5). */
    ASSERT_EQ_INT(pipe(p), 0, "pipe");
    struct sigaction sa, old;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = on_alarm;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGALRM, &sa, &old);
    struct itimerval it;
    memset(&it, 0, sizeof(it));
    it.it_value.tv_usec = 50 * 1000;
    setitimer(ITIMER_REAL, &it, NULL);
    errno = 0;
    int r = tui_tty_wait(p[0], 3000);
    int e = errno;
    sigaction(SIGALRM, &old, NULL);
    alarm(60);                                   /* the suite's own alarm back */
    ASSERT_EQ_INT(r, -1, "interrupted wait");
    ASSERT_EQ_INT(e, EINTR, "errno EINTR");
    close(p[0]);
    close(p[1]);
}

/* A lone ESC followed by the end of input is still an ESC, on every path. */
static void test_escape_then_end_of_input(void) {
    int p[2];
    ASSERT_EQ_INT(pipe(p), 0, "pipe");
    ASSERT_EQ_INT(write(p[1], "\033", 1), 1, "write ESC");
    close(p[1]);
    tui_event ev;
    ASSERT_EQ_INT(tui_tty_read_key(p[0], &ev), 1, "read_key: the ESC");
    ASSERT_EQ_INT(ev.key, TUI_KEY_ESC, "ESC");
    ASSERT_EQ_INT(tui_tty_read_key(p[0], &ev), 0, "then the end");
    close(p[0]);

    TPair t;
    ASSERT(tpair_open(&t, 24, 80) == 0, "openpty");
    tui_term_opts o;
    tui_term_opts_init(&o);
    o.in_fd = o.out_fd = t.slave;
    o.flags = TUI_TERM_NO_SIGNALS;
    tui_term *term = tui_term_open(&o);
    ASSERT(term != NULL, "open");
    tpair_type(&t, "\033");
    /* Read the ESC in (it is then held for its delay); closing the master
     * first would let the kernel discard it with the rest of the input. */
    ASSERT_EQ_INT(tui_term_wait(term, 20, NULL, 0, &ev), 0, "ESC held, no event yet");
    tpair_close_master(&t);
    ASSERT_EQ_INT(tui_term_wait(term, 2000, NULL, 0, &ev), 1, "term_wait: an event");
    ASSERT_EQ_INT(ev.type, TUI_EV_KEY, "the ESC comes before the hang-up");
    ASSERT_EQ_INT(ev.key, TUI_KEY_ESC, "ESC");
    ASSERT_EQ_INT(tui_term_wait(term, 2000, NULL, 0, &ev), 1, "then");
    ASSERT_EQ_INT(ev.type, TUI_EV_HANGUP, "the hang-up");
    tui_term_close(term);
    tpair_close(&t);
}

static const TestCase tests[] = {
    TEST_CASE(test_email_init),
    TEST_CASE(test_email_run_cases),
    TEST_CASE(test_email_case_9_escape),
    TEST_CASE(test_tg_history),
    TEST_CASE(test_tg_readline_non_tty),
    TEST_CASE(test_tty_wait),
    TEST_CASE(test_escape_then_end_of_input),
};

int main(int argc, char **argv) {
    return RUN_SUITE("compat", tests, 130, 60);
}
