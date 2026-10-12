/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Unit tests of tui_tty over a pseudo-terminal pair. */

/*
 * Unit tests of tui_tty over a real pseudo-terminal pair from openpty(3):
 * the library works on the slave, the test plays the terminal on the
 * master.  One row per email-cli capability (libtuikit(7), "email-cli
 * equivalents") is covered here.
 */

#include "test_helpers.h"
#include "tty_fixture.h"
#include "tuikit/tty.h"

#include <errno.h>
#include <termios.h>

int g_tests_run = 0;
int g_tests_failed = 0;

static void test_is_tty(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    int fds[2];
    ASSERT_EQ_INT(pipe(fds), 0, "pipe");
    ASSERT_EQ_INT(tui_tty_is(p.slave), 1, "slave is a terminal");
    ASSERT_EQ_INT(tui_tty_is(fds[0]), 0, "pipe is not a terminal");
    ASSERT_EQ_INT(tui_tty_is(-1), 0, "bad descriptor is not a terminal");
    close(fds[0]);
    close(fds[1]);
    tpair_close(&p);
}

static void test_size_and_fallbacks(void) {
    TPair p;
    ASSERT(tpair_open(&p, 30, 100) == 0, "openpty");
    ASSERT_EQ_INT(tui_tty_rows(p.slave), 30, "rows");
    ASSERT_EQ_INT(tui_tty_cols(p.slave), 100, "cols");
    ASSERT_EQ_INT(tpair_resize(&p, 0, 0), 0, "set size 0x0");
    ASSERT_EQ_INT(tui_tty_rows(p.slave), 0, "unknown rows: 0 (email-cli)");
    ASSERT_EQ_INT(tui_tty_cols(p.slave), 80, "unknown cols: 80 (email-cli)");
    int fds[2];
    ASSERT_EQ_INT(pipe(fds), 0, "pipe");
    ASSERT_EQ_INT(tui_tty_rows(fds[1]), 0, "pipe rows");
    ASSERT_EQ_INT(tui_tty_cols(fds[1]), 80, "pipe cols");
    close(fds[0]);
    close(fds[1]);
    tpair_close(&p);
}

static void test_raw_enter_exit(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    struct termios before, raw, after;
    ASSERT_EQ_INT(tcgetattr(p.slave, &before), 0, "tcgetattr before");
    ASSERT((before.c_lflag & ICANON) && (before.c_lflag & ECHO), "starts cooked");

    tui_tty_raw *st = tui_tty_raw_enter(p.slave, 0);
    ASSERT(st != NULL, "raw_enter");
    ASSERT_EQ_INT(tcgetattr(p.slave, &raw), 0, "tcgetattr raw");
    ASSERT_EQ_INT(raw.c_lflag & (ICANON | ECHO | ISIG | IEXTEN), 0, "ICANON ECHO ISIG IEXTEN off");
    ASSERT_EQ_INT(raw.c_iflag & (IXON | IXOFF | ICRNL | INLCR | IGNCR), 0,
                  "IXON IXOFF ICRNL INLCR IGNCR off (Ctrl-S reaches the program)");
    ASSERT_EQ_INT(raw.c_cc[VMIN], 1, "VMIN");
    ASSERT_EQ_INT(raw.c_cc[VTIME], 0, "VTIME");
    ASSERT_EQ_INT(raw.c_oflag, before.c_oflag, "output processing untouched");

    tui_tty_raw_exit(&st);
    ASSERT(st == NULL, "exit clears the pointer");
    ASSERT_EQ_INT(tcgetattr(p.slave, &after), 0, "tcgetattr after");
    ASSERT_EQ_INT(after.c_lflag, before.c_lflag, "lflag restored");
    ASSERT_EQ_INT(after.c_iflag, before.c_iflag, "iflag restored");
    ASSERT_EQ_INT(after.c_cc[VMIN], before.c_cc[VMIN], "VMIN restored");
    tui_tty_raw_exit(&st);       /* NULL-safe */
    tui_tty_raw_exit(NULL);
    tpair_close(&p);
}

static void test_raw_keep_flags(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_tty_raw *st = tui_tty_raw_enter(p.slave, TUI_TERM_KEEP_ISIG | TUI_TERM_KEEP_IXON);
    ASSERT(st != NULL, "raw_enter");
    struct termios raw;
    tcgetattr(p.slave, &raw);
    ASSERT(raw.c_lflag & ISIG, "ISIG kept");
    ASSERT(raw.c_iflag & IXON, "IXON kept");
    ASSERT_EQ_INT(raw.c_lflag & (ICANON | ECHO), 0, "still raw");
    tui_tty_raw_exit(&st);
    tpair_close(&p);
}

static void test_raw_not_a_tty(void) {
    int fds[2];
    ASSERT_EQ_INT(pipe(fds), 0, "pipe");
    errno = 0;
    ASSERT(tui_tty_raw_enter(fds[0], 0) == NULL, "raw mode on a pipe fails");
    ASSERT_EQ_INT(errno, ENOTTY, "errno");
    close(fds[0]);
    close(fds[1]);
}

static int raii_scope(int fd) {
    TUI_RAII_RAW tui_tty_raw *st = tui_tty_raw_enter(fd, 0);
    if (!st) return -1;
    struct termios t;
    tcgetattr(fd, &t);
    return (t.c_lflag & ICANON) ? -1 : 0;
}

static void test_raii(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    ASSERT_EQ_INT(raii_scope(p.slave), 0, "raw inside the scope");
    struct termios t;
    tcgetattr(p.slave, &t);
    ASSERT(t.c_lflag & ICANON, "restored when the scope ended (RAII_TERM_RAW)");
    tpair_close(&p);
}

static void test_read_key(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    tui_tty_raw *st = tui_tty_raw_enter(p.slave, 0);
    ASSERT(st != NULL, "raw_enter");
    tui_event ev;
    char name[64];

    tpair_type(&p, "\x1b[1;5A");
    ASSERT_EQ_INT(tui_tty_read_key(p.slave, &ev), 1, "read Ctrl+Up");
    ASSERT_EQ_STR(tui_event_name(&ev, name, sizeof(name)), "Ctrl+Up", "Ctrl+Up");

    tpair_type(&p, "\x1b[Z\t");
    ASSERT_EQ_INT(tui_tty_read_key(p.slave, &ev), 1, "read Shift+Tab");
    ASSERT_EQ_STR(tui_event_name(&ev, name, sizeof(name)), "Shift+Tab", "Shift+Tab (email-cli TERM_KEY_SHIFT_TAB)");
    ASSERT_EQ_INT(tui_tty_read_key(p.slave, &ev), 1, "read Tab");
    ASSERT_EQ_STR(tui_event_name(&ev, name, sizeof(name)), "Tab", "Tab (email-cli TERM_KEY_TAB)");

    /* Only the key is consumed: what follows stays for the next reader. */
    tpair_type(&p, "\xC5\x91" "b");
    ASSERT_EQ_INT(tui_tty_read_key(p.slave, &ev), 1, "read ő");
    ASSERT_EQ_STR(ev.utf8, "\xC5\x91", "utf8 of ő (email-cli terminal_last_utf8)");
    char c = 0;
    ASSERT_EQ_INT(read(p.slave, &c, 1), 1, "next byte still there");
    ASSERT_EQ_INT(c, 'b', "next byte is b");

    /* A lone ESC is reported after the ESC delay. */
    tpair_type(&p, "\x1b");
    int64_t t0 = test_now_ms();
    ASSERT_EQ_INT(tui_tty_read_key(p.slave, &ev), 1, "read Esc");
    int64_t dt = test_now_ms() - t0;
    ASSERT_EQ_INT(ev.key, TUI_KEY_ESC, "Esc");
    ASSERT(dt >= TUI_ESC_DELAY_DEFAULT - 5, "Esc waited for the ESC delay");
    ASSERT(dt < 2000, "Esc did not hang");

    /* An unfinished sequence is dropped after the delay; the key typed
     * later is the one returned. */
    tpair_type(&p, "\x1b[1;");
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        usleep(300 * 1000);
        tpair_type(&p, "k");
        _exit(0);
    }
    ASSERT_EQ_INT(tui_tty_read_key(p.slave, &ev), 1, "a key");
    ASSERT(TUI_EV_IS_CHAR(&ev, 'k'), "k, after the dropped sequence");
    waitpid(pid, NULL, 0);

    tui_tty_raw_exit(&st);
    tpair_close_master(&p);
    ASSERT_EQ_INT(tui_tty_read_key(p.slave, &ev), 0, "end of input after hang-up");
    ASSERT_EQ_INT(ev.type, TUI_EV_HANGUP, "hang-up event");
    ASSERT_EQ_INT(tui_tty_read_key(p.slave, NULL), -1, "NULL event");
    tpair_close(&p);
}

static void test_read_key_from_pipe(void) {
    int fds[2];
    ASSERT_EQ_INT(pipe(fds), 0, "pipe");
    ASSERT_EQ_INT(write(fds[1], "x", 1), 1, "write");
    close(fds[1]);
    tui_event ev;
    ASSERT_EQ_INT(tui_tty_read_key(fds[0], &ev), 1, "key from a pipe");
    ASSERT(TUI_EV_IS_CHAR(&ev, 'x'), "x");
    ASSERT_EQ_INT(tui_tty_read_key(fds[0], &ev), 0, "end of the pipe");
    close(fds[0]);
    ASSERT_EQ_INT(tui_tty_read_key(fds[0], &ev), -1, "closed descriptor");
}

/* The password prompt, in a child so that the test can wait for echo to
 * be off before it types: typed earlier, the line discipline would echo. */
static void test_read_password_tty(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    int res[2];
    ASSERT_EQ_INT(pipe(res), 0, "result pipe");
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        close(res[0]);
        char buf[8];
        int n = tui_tty_read_password(p.slave, p.slave, "Password", buf, sizeof(buf));
        struct termios t;
        tcgetattr(p.slave, &t);
        char out[64];
        int len = snprintf(out, sizeof(out), "%d|%s|%d", n, buf, (t.c_lflag & ECHO) ? 1 : 0);
        ssize_t w = write(res[1], out, (size_t)len);
        (void)w;
        exit(0);    /* exit, not _exit: coverage data is written */
    }
    close(res[1]);
    char seen[256];
    ASSERT(tpair_read_until(&p, "Password: ", seen, sizeof(seen), 3000) == 0,
           "prompt \"Password: \" written to the terminal");
    /* Echo is off before the prompt appears: no waiting needed. */
    struct termios t;
    tcgetattr(p.slave, &t);
    ASSERT_EQ_INT(t.c_lflag & ECHO, 0, "echo already off when the prompt is visible");
    tpair_type(&p, "secret-and-long\r\n");
    char out[64] = {0};
    ssize_t n = read(res[0], out, sizeof(out) - 1);
    ASSERT(n > 0, "child reported");
    ASSERT_EQ_STR(out, "7|secret-|1", "password truncated to the buffer, echo restored");
    ASSERT(tpair_read_until(&p, "\n", seen, sizeof(seen), 3000) == 0, "newline after the password");
    ASSERT(strstr(seen, "secret") == NULL, "the password was not echoed");
    int st = 0;
    waitpid(pid, &st, 0);
    close(res[0]);
    tpair_close(&p);
}

static void test_read_password_not_a_tty(void) {
    int in[2], out[2];
    ASSERT_EQ_INT(pipe(in), 0, "input pipe");
    ASSERT_EQ_INT(pipe(out), 0, "output pipe");
    ASSERT_EQ_INT(write(in[1], "pw\r\nnext\n", 9), 9, "write");
    char buf[16];
    ASSERT_EQ_INT(tui_tty_read_password(in[0], out[1], "Password", buf, sizeof(buf)), 2, "length");
    ASSERT_EQ_STR(buf, "pw", "CR LF removed");
    ASSERT_EQ_INT(tui_tty_read_password(in[0], out[1], "Password", buf, sizeof(buf)), 4, "second line");
    ASSERT_EQ_STR(buf, "next", "second line");
    close(in[1]);
    ASSERT_EQ_INT(tui_tty_read_password(in[0], out[1], "Password", buf, sizeof(buf)), -1, "end of input");
    close(out[1]);
    char c;
    ASSERT_EQ_INT(read(out[0], &c, 1), 0, "no prompt when input is not a terminal (email-cli)");
    ASSERT_EQ_INT(tui_tty_read_password(in[0], -1, NULL, NULL, 4), -1, "NULL buffer");
    ASSERT_EQ_INT(tui_tty_read_password(in[0], -1, NULL, buf, 0), -1, "zero size");
    close(in[0]);
    close(out[0]);

    /* A last line without a newline still counts. */
    ASSERT_EQ_INT(pipe(in), 0, "input pipe");
    ASSERT_EQ_INT(write(in[1], "tail", 4), 4, "write");
    close(in[1]);
    ASSERT_EQ_INT(tui_tty_read_password(in[0], -1, NULL, buf, sizeof(buf)), 4, "unterminated line");
    ASSERT_EQ_STR(buf, "tail", "unterminated line content");
    close(in[0]);
}

/* One masked prompt on a terminal pair: @p ahead is typed before the
 * prompt, @p typed once the terminal is in raw mode.  @p screen receives
 * everything the terminal showed, prompt included. */
static void masked(int *rc_out, const char *ahead, const char *typed, char *pw, size_t size,
                  char *screen, size_t screen_size) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    if (ahead) tpair_type(&p, ahead);
    pid_t typist = tpair_type_when_raw(&p, typed);
    ASSERT(typist > 0, "typist");
    int rc = tui_tty_read_password_masked(p.slave, p.slave, "Password", pw, size);
    *rc_out = rc;
    int st = 0;
    waitpid(typist, &st, 0);
    ASSERT(WIFEXITED(st) && WEXITSTATUS(st) == 0, "the typist saw raw mode");
    screen[0] = '\0';
    size_t n = tpair_read_all(&p, screen, screen_size - 1, 150);
    screen[n] = '\0';
    tpair_close(&p);
}

/* Everything the field does on a terminal, as email-cli's masked field
 * does it (EMAIL-9). */
static void test_read_password_masked(void) {
    char pw[32], screen[256];
    int rc;

    masked(&rc, NULL, "secret\r", pw, sizeof(pw), screen, sizeof(screen));
    ASSERT_EQ_INT(rc, 6, "length");
    ASSERT_EQ_STR(pw, "secret", "password");
    ASSERT_EQ_STR(screen, "Password: ******\r\n", "one * per character, newline after");

    /* One mask per character, not per byte. */
    masked(&rc, NULL, "\xc3\xa9x\xe2\x82\xac\r", pw, sizeof(pw), screen, sizeof(screen));
    ASSERT_EQ_INT(rc, 6, "length in bytes");
    ASSERT_EQ_STR(pw, "\xc3\xa9x\xe2\x82\xac", "UTF-8 kept");
    ASSERT_EQ_STR(screen, "Password: ***\r\n", "three characters, three masks");

    /* Backspace removes the last character and its mask. */
    masked(&rc, NULL, "ab\x7f" "c\r", pw, sizeof(pw), screen, sizeof(screen));
    ASSERT_EQ_INT(rc, 2, "after Backspace");
    ASSERT_EQ_STR(pw, "ac", "Backspace removed b");
    ASSERT_EQ_STR(screen, "Password: **\b \b*\r\n", "its mask erased");
    masked(&rc, NULL, "\xc3\xa9\x7fz\r", pw, sizeof(pw), screen, sizeof(screen));
    ASSERT_EQ_INT(rc, 1, "a whole multi-byte character is removed");
    ASSERT_EQ_STR(pw, "z", "Backspace over UTF-8");
    masked(&rc, NULL, "\x7f" "a\r", pw, sizeof(pw), screen, sizeof(screen));
    ASSERT_EQ_STR(pw, "a", "Backspace on an empty field");
    ASSERT_EQ_STR(screen, "Password: *\r\n", "nothing erased on an empty field");

    /* Ctrl-U clears the field. */
    masked(&rc, NULL, "abc\x15" "d\r", pw, sizeof(pw), screen, sizeof(screen));
    ASSERT_EQ_INT(rc, 1, "after Ctrl-U");
    ASSERT_EQ_STR(pw, "d", "Ctrl-U cleared the field");
    ASSERT_EQ_STR(screen, "Password: ***\b \b\b \b\b \b*\r\n", "all masks erased");

    /* Escape sequences, Ctrl and Alt combinations are not part of it. */
    masked(&rc, NULL, "a\x1b[Ab\x1bOHc\x01\x1b" "xd\r", pw, sizeof(pw), screen, sizeof(screen));
    ASSERT_EQ_STR(pw, "abcd", "escape sequences and Ctrl/Alt keys dropped");
    ASSERT_EQ_STR(screen, "Password: ****\r\n", "and not shown");
    masked(&rc, NULL, "a\x13" "b\x11" "c\r", pw, sizeof(pw), screen, sizeof(screen));
    ASSERT_EQ_STR(pw, "abc", "Ctrl-S and Ctrl-Q are keys, not flow control");

    /* A full field rings the bell and drops the character. */
    char small[4];
    masked(&rc, NULL, "abcde\r", small, sizeof(small), screen, sizeof(screen));
    ASSERT_EQ_INT(rc, 3, "size - 1 bytes");
    ASSERT_EQ_STR(small, "abc", "truncated");
    ASSERT_EQ_STR(screen, "Password: ***\a\a\r\n", "a bell for each dropped character");

    /* What was typed before the prompt is not part of the password. */
    masked(&rc, "old\n", "x\r", pw, sizeof(pw), screen, sizeof(screen));
    ASSERT_EQ_INT(rc, 1, "type-ahead discarded");
    ASSERT_EQ_STR(pw, "x", "only what was typed at the prompt");

    /* Ctrl-C and Ctrl-D end it with the buffer wiped. */
    memset(pw, 'z', sizeof(pw));
    masked(&rc, NULL, "ab\x03", pw, sizeof(pw), screen, sizeof(screen));
    ASSERT_EQ_INT(rc, -1, "Ctrl-C");
    ASSERT(memcmp(pw, "\0\0\0\0\0\0\0\0", 8) == 0, "buffer wiped");
    ASSERT_EQ_STR(screen, "Password: **^C\r\n", "^C shown");
    masked(&rc, NULL, "ab\x04", pw, sizeof(pw), screen, sizeof(screen));
    ASSERT_EQ_INT(rc, -1, "Ctrl-D");
    ASSERT_EQ_STR(pw, "", "buffer wiped");
}

/* While it reads: no echo, no canonical mode, no signal keys, no flow
 * control, no CR to LF; afterwards the mode is as it was. */
static void test_read_password_masked_mode(void) {
    TPair p;
    ASSERT(tpair_open(&p, 24, 80) == 0, "openpty");
    struct termios before;
    tcgetattr(p.slave, &before);
    int res[2];
    ASSERT_EQ_INT(pipe(res), 0, "result pipe");
    pid_t pid = fork();
    ASSERT(pid >= 0, "fork");
    if (pid == 0) {
        close(res[0]);
        char buf[16];
        int n = tui_tty_read_password_masked(p.slave, p.slave, "Password", buf, sizeof(buf));
        char out[32];
        int len = snprintf(out, sizeof(out), "%d|%s", n, buf);
        ssize_t w = write(res[1], out, (size_t)len);
        (void)w;
        exit(0);    /* exit, not _exit: coverage data is written */
    }
    close(res[1]);
    char seen[128];
    ASSERT(tpair_read_until(&p, "Password: ", seen, sizeof(seen), 3000) == 0, "prompt");
    struct termios t;
    tcgetattr(p.slave, &t);
    ASSERT_EQ_INT(t.c_lflag & (ECHO | ICANON | ISIG | IEXTEN), 0, "echo, canonical, signals, extensions off");
    ASSERT_EQ_INT(t.c_iflag & (IXON | ICRNL), 0, "flow control and CR translation off");
    tpair_type(&p, "pw\r");
    char out[32] = {0};
    ASSERT(read(res[0], out, sizeof(out) - 1) > 0, "child reported");
    ASSERT_EQ_STR(out, "2|pw", "result");
    int st = 0;
    waitpid(pid, &st, 0);
    close(res[0]);
    tcgetattr(p.slave, &t);
    ASSERT_EQ_INT(t.c_lflag & (ECHO | ICANON | ISIG | IEXTEN),
                  before.c_lflag & (ECHO | ICANON | ISIG | IEXTEN), "lflag restored");
    ASSERT_EQ_INT(t.c_iflag & (IXON | ICRNL), before.c_iflag & (IXON | ICRNL), "iflag restored");
    tpair_close(&p);
}

static void test_read_password_masked_not_a_tty(void) {
    int in[2], out[2];
    ASSERT_EQ_INT(pipe(in), 0, "input pipe");
    ASSERT_EQ_INT(pipe(out), 0, "output pipe");
    ASSERT_EQ_INT(write(in[1], "pw\r\nnext\n", 9), 9, "write");
    char buf[16];
    ASSERT_EQ_INT(tui_tty_read_password_masked(in[0], out[1], "Password", buf, sizeof(buf)), 2, "length");
    ASSERT_EQ_STR(buf, "pw", "CR LF removed");
    ASSERT_EQ_INT(tui_tty_read_password_masked(in[0], out[1], "Password", buf, sizeof(buf)), 4, "second line");
    close(in[1]);
    ASSERT_EQ_INT(tui_tty_read_password_masked(in[0], out[1], "Password", buf, sizeof(buf)), -1, "end of input");
    close(out[1]);
    char c;
    ASSERT_EQ_INT(read(out[0], &c, 1), 0, "no prompt and no mask when input is not a terminal");
    ASSERT_EQ_INT(tui_tty_read_password_masked(in[0], -1, NULL, NULL, 4), -1, "NULL buffer");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_tty_read_password_masked(in[0], -1, NULL, buf, 0), -1, "zero size");
    close(in[0]);
    close(out[0]);
}

static const TestCase tests[] = {
    TEST_CASE(test_is_tty),
    TEST_CASE(test_size_and_fallbacks),
    TEST_CASE(test_raw_enter_exit),
    TEST_CASE(test_raw_keep_flags),
    TEST_CASE(test_raw_not_a_tty),
    TEST_CASE(test_raii),
    TEST_CASE(test_read_key),
    TEST_CASE(test_read_key_from_pipe),
    TEST_CASE(test_read_password_tty),
    TEST_CASE(test_read_password_not_a_tty),
    TEST_CASE(test_read_password_masked),
    TEST_CASE(test_read_password_masked_mode),
    TEST_CASE(test_read_password_masked_not_a_tty),
};

int main(int argc, char **argv) {
    return RUN_SUITE("tty", tests, 85, 60);
}
