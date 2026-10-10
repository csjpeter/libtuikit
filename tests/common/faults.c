/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Link-time fault injection wrappers for the tests. */

/* The wrappers of faults.h.  See there. */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "faults.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

void *__real_malloc(size_t n);
void *__real_calloc(size_t n, size_t m);
void *__real_realloc(void *p, size_t n);
int   __real_pipe(int fds[2]);
int   __real_fcntl(int fd, int cmd, ...);
int   __real_tcgetattr(int fd, struct termios *t);
int   __real_tcsetattr(int fd, int act, const struct termios *t);
int   __real_poll(struct pollfd *fds, nfds_t n, int timeout);
ssize_t __real_read(int fd, void *buf, size_t n);
ssize_t __real_write(int fd, const void *buf, size_t n);
int   __real_raise(int sig);

/* gcov's flush.  In a coverage build the reference must be a strong one:
 * a weak reference does not pull the member out of the static libgcov,
 * and the function would be missing (NULL) when it is needed. */
#if defined(TUIKIT_COVERAGE_BUILD)
extern void __gcov_dump(void);
#define GCOV_DUMP_AVAILABLE 1
#else
#define GCOV_DUMP_AVAILABLE 0
static void __gcov_dump(void) {}
#endif

typedef struct {
    int skip, times, err, calls;
} fault;

static fault g_f[F_COUNT];
static int   g_lie_skip = -1, g_lie_times;

void fault_arm(fault_fn fn, int skip, int times, int err) {
    g_f[fn].skip = skip;
    g_f[fn].times = times;
    g_f[fn].err = err;
}

void fault_tcgetattr_lies(int skip, int times) {
    g_lie_skip = skip;
    g_lie_times = times;
}

void fault_reset(void) {
    memset(g_f, 0, sizeof(g_f));
    g_lie_skip = -1;
    g_lie_times = 0;
}

int fault_calls(fault_fn fn) {
    return g_f[fn].calls;
}

/* 1 when this call of @p fn is to fail (errno set). */
static int hit(fault_fn fn) {
    fault *f = &g_f[fn];
    f->calls++;
    if (f->times <= 0) return 0;
    if (f->skip > 0) {
        f->skip--;
        return 0;
    }
    f->times--;
    errno = f->err;
    return 1;
}

void *__wrap_malloc(size_t n) { return hit(F_MALLOC) ? NULL : __real_malloc(n); }
void *__wrap_calloc(size_t n, size_t m) { return hit(F_CALLOC) ? NULL : __real_calloc(n, m); }
void *__wrap_realloc(void *p, size_t n) { return hit(F_REALLOC) ? NULL : __real_realloc(p, n); }
int __wrap_pipe(int fds[2]) { return hit(F_PIPE) ? -1 : __real_pipe(fds); }

int __wrap_fcntl(int fd, int cmd, ...) {
    if (hit(F_FCNTL)) return -1;
    if (cmd == F_GETFL || cmd == F_GETFD) return __real_fcntl(fd, cmd);
    va_list ap;
    va_start(ap, cmd);
    long arg = va_arg(ap, long);
    va_end(ap);
    return __real_fcntl(fd, cmd, arg);
}

int __wrap_tcgetattr(int fd, struct termios *t) {
    if (hit(F_TCGETATTR)) return -1;
    int r = __real_tcgetattr(fd, t);
    if (r == 0 && g_lie_times > 0) {
        if (g_lie_skip > 0) {
            g_lie_skip--;
        } else {
            g_lie_times--;
            t->c_lflag |= ICANON;
        }
    }
    return r;
}

int __wrap_tcsetattr(int fd, int act, const struct termios *t) {
    return hit(F_TCSETATTR) ? -1 : __real_tcsetattr(fd, act, t);
}

int __wrap_poll(struct pollfd *fds, nfds_t n, int timeout) {
    return hit(F_POLL) ? -1 : __real_poll(fds, n, timeout);
}

ssize_t __wrap_read(int fd, void *buf, size_t n) {
    return hit(F_READ) ? -1 : __real_read(fd, buf, n);
}

/* With _FORTIFY_SOURCE (the Release build) a read into a buffer of known
 * size becomes __read_chk: the same fault applies to it. */
ssize_t __real___read_chk(int fd, void *buf, size_t n, size_t buflen);
ssize_t __wrap___read_chk(int fd, void *buf, size_t n, size_t buflen) {
    return hit(F_READ) ? -1 : __real___read_chk(fd, buf, n, buflen);
}

ssize_t __wrap_write(int fd, const void *buf, size_t n) {
    return hit(F_WRITE) ? -1 : __real_write(fd, buf, n);
}

/* gcov writes its counters once per process.  Write them when a raise will
 * end the process -- a fatal signal whose action is the default again, as
 * in a handler about to re-raise -- and not on the raises tests make. */
int __wrap_raise(int sig) {
    if (GCOV_DUMP_AVAILABLE && (sig == SIGTERM || sig == SIGHUP || sig == SIGINT || sig == SIGQUIT)) {
        struct sigaction cur;
        if (sigaction(sig, NULL, &cur) == 0 && !(cur.sa_flags & SA_SIGINFO) &&
            cur.sa_handler == SIG_DFL)
            __gcov_dump();
    }
    return __real_raise(sig);
}
