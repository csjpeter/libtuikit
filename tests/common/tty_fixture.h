/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* A pseudo-terminal pair for in-process tests. */

#ifndef TUIKIT_TTY_FIXTURE_H
#define TUIKIT_TTY_FIXTURE_H

/*
 * A pseudo-terminal pair for in-process tests: the code under test works on
 * the slave, the test plays the terminal on the master -- types into it and
 * reads what was written.  No terminal emulation here; for that the PTY
 * suites use libptytest.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pty.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

typedef struct {
    int master;
    int slave;
} TPair;

static inline int64_t test_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static inline int tpair_resize(TPair *p, int rows, int cols) {
    struct winsize ws;
    memset(&ws, 0, sizeof(ws));
    ws.ws_row = (unsigned short)rows;
    ws.ws_col = (unsigned short)cols;
    return ioctl(p->master, TIOCSWINSZ, &ws);
}

/* A fixture that cannot be created fails the test (policies/testing-model.md):
 * the caller ASSERTs the result. */
static inline int tpair_open(TPair *p, int rows, int cols) {
    p->master = p->slave = -1;
    if (openpty(&p->master, &p->slave, NULL, NULL, NULL) != 0) return -1;
    return tpair_resize(p, rows, cols);
}

static inline void tpair_close_master(TPair *p) {
    if (p->master >= 0) close(p->master);
    p->master = -1;
}

static inline void tpair_close(TPair *p) {
    tpair_close_master(p);
    if (p->slave >= 0) close(p->slave);
    p->slave = -1;
}

/* Types @p s into the terminal, as a user would. */
static inline void tpair_type(TPair *p, const char *s) {
    size_t n = strlen(s);
    while (n > 0) {
        ssize_t w = write(p->master, s, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            return;
        }
        s += w;
        n -= (size_t)w;
    }
}

/* Types @p s from a child process as soon as the terminal is in raw mode
 * (canonical input off), the way a user types after the program has
 * started reading.  Typed earlier, the line discipline would echo the keys
 * and act on Ctrl-C itself.  Returns the child's pid, -1 on failure; the
 * child exits 1 if raw mode never came. */
static inline pid_t tpair_type_when_raw(TPair *p, const char *s) {
    pid_t pid = fork();
    if (pid != 0) return pid;
    int64_t until = test_now_ms() + 5000;
    for (;;) {
        struct termios t;
        if (tcgetattr(p->slave, &t) == 0 && !(t.c_lflag & ICANON)) break;
        if (test_now_ms() > until) _exit(1);
        usleep(1000);
    }
    tpair_type(p, s);
    _exit(0);
}

/* Reads what was written to the terminal until @p needle appears or the
 * timeout passes; @p buf receives everything read (NUL-terminated,
 * truncated to @p size).  0 when found, -1 otherwise. */
static inline int tpair_read_until(TPair *p, const char *needle, char *buf,
                                   size_t size, int timeout_ms) {
    size_t len = 0;
    buf[0] = '\0';
    int64_t until = test_now_ms() + timeout_ms;
    for (;;) {
        if (strstr(buf, needle)) return 0;
        int64_t left = until - test_now_ms();
        if (left <= 0) return -1;
        struct pollfd pfd = { .fd = p->master, .events = POLLIN, .revents = 0 };
        int r = poll(&pfd, 1, (int)left);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return -1;
        char tmp[512];
        ssize_t n = read(p->master, tmp, sizeof(tmp));
        if (n <= 0) return -1;
        size_t take = (size_t)n;
        if (len + take + 1 > size) take = size - len - 1;
        memcpy(buf + len, tmp, take);
        len += take;
        buf[len] = '\0';
    }
}

/* Reads whatever the terminal has received within @p quiet_ms of silence. */
static inline size_t tpair_read_all(TPair *p, char *buf, size_t size, int quiet_ms) {
    size_t len = 0;
    for (;;) {
        struct pollfd pfd = { .fd = p->master, .events = POLLIN, .revents = 0 };
        int r = poll(&pfd, 1, quiet_ms);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0 || !(pfd.revents & POLLIN)) break;
        char tmp[512];
        ssize_t n = read(p->master, tmp, sizeof(tmp));
        if (n <= 0) break;
        size_t take = (size_t)n;
        if (len + take + 1 > size) take = size - len - 1;
        memcpy(buf + len, tmp, take);
        len += take;
    }
    buf[len] = '\0';
    return len;
}

#endif /* TUIKIT_TTY_FIXTURE_H */
