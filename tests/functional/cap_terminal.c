/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-16 */
/* Capability tests: tui_tty, tui_term, tui_lineedit_run and tui_readline on a pty. */

#include "cap_common.h"

#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <termios.h>

static int master, slave;

static void open_pty(int rows, int cols) {
    struct winsize ws = { (unsigned short)rows, (unsigned short)cols, 0, 0 };
    if (openpty(&master, &slave, NULL, NULL, &ws) != 0) {
        perror("openpty");
        exit(1);
    }
    fcntl(master, F_SETFL, fcntl(master, F_GETFL) | O_NONBLOCK);
}

static void close_pty(void) {
    close(master);
    close(slave);
}

/* Everything the program wrote to the terminal so far. */
static size_t drain(char *out, size_t cap) {
    size_t n = 0;
    ssize_t r;
    usleep(20000);
    while (n + 1 < cap && (r = read(master, out + n, cap - 1 - n)) > 0)
        n += (size_t)r;
    out[n] = '\0';
    return n;
}

/* A helper process that types @p bytes on the master after @p delay_ms,
 * as a user would. */
static pid_t typist(const char *bytes, int delay_ms) {
    pid_t p = fork();
    if (p == 0) {
        usleep((useconds_t)delay_ms * 1000);
        ssize_t w = write(master, bytes, strlen(bytes));
        (void)w;
        usleep(50000);
        _exit(0);
    }
    return p;
}
static void join(pid_t p) { int st; waitpid(p, &st, 0); }

static void cap_tty_size_and_mode(void) {
    open_pty(30, 100);
    CHECK_INT(tui_tty_is(slave), 1, "pty is a terminal");
    int pp[2];
    CHECK_INT(pipe(pp), 0, "pipe");
    CHECK_INT(tui_tty_is(pp[0]), 0, "pipe is not");
    CHECK_INT(tui_tty_cols(slave), 100, "cols");
    CHECK_INT(tui_tty_rows(slave), 30, "rows");
    CHECK_INT(tui_tty_cols(pp[0]), 80, "cols default");
    CHECK_INT(tui_tty_rows(pp[0]), 0, "rows unknown");
    close(pp[0]); close(pp[1]);

    struct termios before, during, after;
    tcgetattr(slave, &before);
    tui_tty_raw *raw = tui_tty_raw_enter(slave, 0);
    CHECK(raw != NULL, "raw_enter");
    tcgetattr(slave, &during);
    CHECK((during.c_lflag & (ICANON | ECHO | ISIG)) == 0, "raw: no canon, echo, signals");
    tui_tty_raw_exit(&raw);
    CHECK(raw == NULL, "raw_exit clears the pointer");
    tcgetattr(slave, &after);
    CHECK(after.c_lflag == before.c_lflag && after.c_iflag == before.c_iflag, "mode restored");
    tui_tty_raw_exit(&raw);
    raw = tui_tty_raw_enter(slave, TUI_TERM_KEEP_ISIG);
    tcgetattr(slave, &during);
    CHECK((during.c_lflag & ISIG) != 0, "KEEP_ISIG keeps the signal keys");
    tui_tty_raw_exit(&raw);
    int pp2[2];
    CHECK_INT(pipe(pp2), 0, "pipe");
    CHECK(tui_tty_raw_enter(pp2[0], 0) == NULL, "raw_enter on a non-terminal fails");
    CHECK_INT(errno, ENOTTY, "ENOTTY");
    close(pp2[0]); close(pp2[1]);
    close_pty();
}

static void cap_tty_input(void) {
    open_pty(24, 80);
    tui_tty_raw *raw = tui_tty_raw_enter(slave, 0);
    CHECK_INT(tui_tty_wait(slave, 0), 0, "wait times out");
    CHECK_INT(write(master, "x", 1), 1, "type x");
    CHECK_INT(tui_tty_wait(slave, 1000), 1, "wait sees input");
    tui_event ev;
    CHECK_INT(tui_tty_read_key(slave, &ev), 1, "read_key");
    CHECK(TUI_EV_IS_CHAR(&ev, 'x'), "the key typed");
    CHECK_INT(write(master, "\x1b[A", 3), 3, "type Up");
    CHECK_INT(tui_tty_read_key(slave, &ev), 1, "read_key Up");
    CHECK_INT(ev.key, TUI_KEY_UP, "Up");
    CHECK_INT(write(master, "\x1b", 1), 1, "type lone Esc");
    CHECK_INT(tui_tty_read_key(slave, &ev), 1, "read_key Esc after the delay");
    CHECK_INT(ev.key, TUI_KEY_ESC, "Esc");
    tui_tty_raw_exit(&raw);
    close_pty();
}

static void cap_tty_password(void) {
    char pw[32], out[256];

    open_pty(24, 80);
    pid_t t = typist("secret\n", 150);
    int n = tui_tty_read_password(slave, slave, "Password", pw, sizeof pw);
    join(t);
    CHECK_INT(n, 6, "password length");
    CHECK_STR(pw, "secret", "password");
    drain(out, sizeof out);
    CHECK_HAS(out, "Password: ", "prompt written");
    CHECK(strstr(out, "secret") == NULL, "no echo");
    close_pty();

    int p[2];
    CHECK_INT(pipe(p), 0, "pipe");
    CHECK_INT(write(p[1], "piped\n", 6), 6, "pipe input");
    n = tui_tty_read_password(p[0], 1, "Password", pw, sizeof pw);
    CHECK_INT(n, 5, "from a pipe");
    CHECK_STR(pw, "piped", "no prompt, plain line");
    close(p[0]);
    close(p[1]);

    open_pty(24, 80);
    t = typist("ab\x7f" "c\r", 150);
    n = tui_tty_read_password_masked(slave, slave, "Pass", pw, sizeof pw);
    join(t);
    CHECK_INT(n, 2, "masked length");
    CHECK_STR(pw, "ac", "Backspace removed a character");
    drain(out, sizeof out);
    CHECK_HAS(out, "Pass: ", "masked prompt");
    CHECK_HAS(out, "*", "stars shown");
    CHECK(strstr(out, "ac") == NULL, "text not shown");
    close_pty();

    open_pty(24, 80);
    t = typist("ab\x03", 150);
    n = tui_tty_read_password_masked(slave, slave, "Pass", pw, sizeof pw);
    join(t);
    CHECK_INT(n, -1, "Ctrl-C ends the prompt");
    CHECK_INT(pw[0], 0, "buffer wiped");
    drain(out, sizeof out);
    CHECK_HAS(out, "^C", "^C shown");
    close_pty();
}

static void cap_term(void) {
    open_pty(24, 80);
    tui_term_opts o;
    tui_term_opts_init(&o);
    CHECK_INT(o.in_fd, STDIN_FILENO, "default in_fd");
    CHECK_INT(o.out_fd, STDOUT_FILENO, "default out_fd");
    CHECK(o.flags & TUI_TERM_ALTSCREEN, "default altscreen");
    CHECK_INT(o.esc_delay_ms, TUI_ESC_DELAY_DEFAULT, "default ESC delay");
    o.in_fd = slave;
    o.out_fd = slave;
    o.flags |= TUI_TERM_PASTE;

    tui_term *t = tui_term_open(&o);
    CHECK(t != NULL, "open");
    CHECK_INT(tui_term_fd(t), slave, "term_fd");
    char out[1024];
    drain(out, sizeof out);
    CHECK_HAS(out, "\x1b[?1049h", "alternate screen entered");
    CHECK_HAS(out, "\x1b[?2004h", "bracketed paste on");

    int rows = 0, cols = 0;
    CHECK_INT(tui_term_size(t, &rows, &cols), 0, "size");
    CHECK_INT(rows, 24, "size rows");
    CHECK_INT(cols, 80, "size cols");

    CHECK(tui_term_open(&o) == NULL, "a second session is refused");
    CHECK_INT(errno, EBUSY, "EBUSY");

    CHECK_INT(tui_term_write(t, "hello", 5), 0, "write");
    tui_buf b;
    tui_buf_init(&b);
    tui_buf_puts(&b, "world");
    CHECK_INT(tui_term_flush_buf(t, &b), 0, "flush_buf");
    CHECK_INT(b.len, 0, "flush_buf empties");
    drain(out, sizeof out);
    CHECK_HAS(out, "helloworld", "written to the terminal");
    tui_buf_free(&b);

    tui_event ev;
    CHECK_INT(tui_term_wait(t, 0, NULL, 0, &ev), 0, "wait times out");
    CHECK_INT(write(master, "q", 1), 1, "type");
    CHECK_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "wait event");
    CHECK(TUI_EV_IS_CHAR(&ev, 'q'), "key q");
    CHECK_INT(write(master, "\x1b[200~pasted\x1b[201~", 18), 18, "paste");
    CHECK_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "wait paste");
    CHECK_INT(ev.type, TUI_EV_PASTE, "paste event");
    CHECK_INT(ev.paste_len, 6, "paste length");

    int pp[2];
    CHECK_INT(pipe(pp), 0, "pipe");
    struct pollfd pf = { pp[0], POLLIN, 0 };
    CHECK_INT(write(pp[1], "z", 1), 1, "write the extra fd");
    CHECK_INT(tui_term_wait(t, 1000, &pf, 1, &ev), 1, "wait extra fd");
    CHECK_INT(ev.type, TUI_EV_FD, "fd event");
    CHECK_INT(ev.fd_index, 0, "fd index");
    close(pp[0]); close(pp[1]);

    struct winsize ws = { 40, 120, 0, 0 };
    ioctl(master, TIOCSWINSZ, &ws);
    kill(getpid(), SIGWINCH);
    CHECK_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "resize wakes the wait");
    CHECK_INT(ev.type, TUI_EV_RESIZE, "resize event");
    CHECK_INT(ev.rows, 40, "new rows");
    CHECK_INT(ev.cols, 120, "new cols");

    drain(out, sizeof out);
    CHECK_INT(tui_term_suspend(t), 0, "suspend");
    size_t n = drain(out, sizeof out);
    CHECK(n > 0, "suspend restores the terminal");
    CHECK_HAS(out, "\x1b[?1049l", "alternate screen left");
    CHECK_INT(tui_term_resume(t), 0, "resume");
    drain(out, sizeof out);
    CHECK_HAS(out, "\x1b[?1049h", "alternate screen again");
    CHECK_INT(tui_term_wait(t, 1000, NULL, 0, &ev), 1, "wait after resume");
    CHECK_INT(ev.type, TUI_EV_RESIZE, "resume reports a resize");

    CHECK_INT(tui_term_close(t), 0, "close");
    drain(out, sizeof out);
    CHECK_HAS(out, "\x1b[?1049l", "screen given back");
    CHECK_INT(tui_term_close(NULL), 0, "close NULL is safe");

    int pp3[2];
    CHECK_INT(pipe(pp3), 0, "pipe");
    o.in_fd = pp3[0];
    CHECK(tui_term_open(&o) == NULL, "open on a non-terminal fails");
    CHECK_INT(errno, ENOTTY, "ENOTTY");
    close(pp3[0]); close(pp3[1]);
    close_pty();
}

static void cap_lineedit_run(void) {
    open_pty(24, 80);
    tui_tty_raw *raw = tui_tty_raw_enter(slave, 0);
    char buf[32], out[1024];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof buf, "ab");
    pid_t t = typist("cd\r", 150);
    int r = tui_lineedit_run(&le, slave, slave, 3, 1, "> ");
    join(t);
    CHECK_INT(r, 1, "Enter");
    CHECK_STR(buf, "abcd", "text edited");
    drain(out, sizeof out);
    CHECK_HAS(out, "> abcd", "drawn on the row");
    CHECK_HAS(out, "\x1b[3;", "on terminal row 3");

    tui_lineedit_set_text(&le, "");
    t = typist("\x03", 150);
    r = tui_lineedit_run(&le, slave, slave, 1, 1, "> ");
    join(t);
    CHECK_INT(r, 0, "Ctrl-C cancels");
    tui_lineedit_free(&le);
    tui_tty_raw_exit(&raw);
    close_pty();
}

static void cap_readline(void) {
    open_pty(24, 80);
    tui_readline_opts o;
    tui_readline_opts_init(&o);
    CHECK_INT(o.in_fd, 0, "default in");
    CHECK_INT(o.out_fd, 1, "default out");
    o.in_fd = slave;
    o.out_fd = slave;
    tui_history h;
    tui_history_init(&h, 4);
    o.history = &h;
    char buf[32], out[1024];
    pid_t t = typist("hello\r", 150);
    int n = tui_readline("$ ", buf, sizeof buf, &o);
    join(t);
    CHECK_INT(n, 5, "length");
    CHECK_STR(buf, "hello", "line");
    CHECK_INT(tui_history_count(&h), 1, "line added to the history");
    drain(out, sizeof out);
    CHECK_HAS(out, "$ ", "prompt written");

    t = typist("\x04", 150);
    CHECK_INT(tui_readline("$ ", buf, sizeof buf, &o), TUI_READLINE_EOF, "Ctrl-D is EOF");
    join(t);
    t = typist("\x03", 150);
    CHECK_INT(tui_readline("$ ", buf, sizeof buf, &o), TUI_READLINE_CANCEL, "Ctrl-C cancels");
    join(t);
    o.timeout_sec = 1;
    CHECK_INT(tui_readline("$ ", buf, sizeof buf, &o), TUI_READLINE_TIMEOUT, "idle timeout");
    tui_history_free(&h);
    close_pty();
}

int main(void) {
    cap_tty_size_and_mode();
    cap_tty_input();
    cap_tty_password();
    cap_term();
    cap_lineedit_run();
    cap_readline();
    return cap_finish("cap-terminal");
}
