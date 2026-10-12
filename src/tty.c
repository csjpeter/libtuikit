/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* File-descriptor level helpers: size, raw mode, one key and a password. */

/*
 * File-descriptor level helpers: size, raw mode, one key, a password.
 * The capabilities of email-cli's platform/terminal.h, as libtuikit(7)
 * lists them under "email-cli equivalents".
 */

#define _POSIX_C_SOURCE 200809L

#include "tuikit/tty.h"
#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

struct tui_tty_raw {
    int            fd;
    struct termios saved;
};

int tui__write_all(int fd, const void *p, size_t n) {
    const char *s = p;
    while (n > 0) {
        ssize_t w = write(fd, s, n);
        if (w < 0) {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                struct pollfd pfd = { .fd = fd, .events = POLLOUT, .revents = 0 };
                if (poll(&pfd, 1, -1) < 0 && errno != EINTR) return -1;
                continue;
            }
            return -1;
        }
        s += w;
        n -= (size_t)w;
    }
    return 0;
}

int64_t tui__now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void tui__make_raw(struct termios *t, unsigned flags) {
    t->c_lflag &= ~(tcflag_t)(ICANON | ECHO | IEXTEN);
    if (!(flags & TUI_TERM_KEEP_ISIG)) t->c_lflag &= ~(tcflag_t)ISIG;
    t->c_iflag &= ~(tcflag_t)(ICRNL | INLCR | IGNCR | ISTRIP | BRKINT | INPCK);
    /* Ctrl-S and Ctrl-Q reach the program (issue-manager BUG-208). */
    if (!(flags & TUI_TERM_KEEP_IXON)) t->c_iflag &= ~(tcflag_t)(IXON | IXOFF);
    t->c_cflag |= CS8;
    t->c_cc[VMIN] = 1;
    t->c_cc[VTIME] = 0;
}

int tui__apply_mode(int fd, const struct termios *want) {
    while (tcsetattr(fd, TCSANOW, want) != 0) {
        if (errno != EINTR) return -1;
    }
    /* tcsetattr(3) succeeds if *any* of the changes took; read it back. */
    struct termios got;
    if (tcgetattr(fd, &got) != 0) return -1;
    const tcflag_t lmask = ICANON | ECHO | IEXTEN | ISIG;
    const tcflag_t imask = ICRNL | INLCR | IGNCR | IXON | IXOFF;
    if ((got.c_lflag & lmask) != (want->c_lflag & lmask) ||
        (got.c_iflag & imask) != (want->c_iflag & imask) ||
        got.c_cc[VMIN] != want->c_cc[VMIN] || got.c_cc[VTIME] != want->c_cc[VTIME]) {
        errno = EIO;
        return -1;
    }
    return 0;
}

int tui_tty_is(int fd) {
    return isatty(fd) ? 1 : 0;
}

int tui_tty_cols(int fd) {
    struct winsize ws;
    if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0) return (int)ws.ws_col;
    return 80;
}

int tui_tty_rows(int fd) {
    struct winsize ws;
    if (ioctl(fd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) return (int)ws.ws_row;
    return 0;
}

tui_tty_raw *tui_tty_raw_enter(int fd, unsigned flags) {
    if (!isatty(fd)) {
        errno = ENOTTY;
        return NULL;
    }
    tui_tty_raw *st = malloc(sizeof(*st));
    if (!st) return NULL;
    st->fd = fd;
    if (tcgetattr(fd, &st->saved) != 0) {
        int e = errno;
        free(st);
        errno = e;
        return NULL;
    }
    struct termios raw = st->saved;
    tui__make_raw(&raw, flags);
    if (tui__apply_mode(fd, &raw) != 0) {
        int e = errno;
        /* Part of the change may have taken: put everything back. */
        tcsetattr(fd, TCSANOW, &st->saved);
        free(st);
        errno = e;
        return NULL;
    }
    return st;
}

void tui_tty_raw_exit(tui_tty_raw **state) {
    if (!state || !*state) return;
    int e = errno;
    while (tcsetattr((*state)->fd, TCSANOW, &(*state)->saved) != 0 && errno == EINTR) {}
    free(*state);
    *state = NULL;
    errno = e;
}

int tui_tty_wait(int fd, int timeout_ms) {
    struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
    int r = poll(&pfd, 1, timeout_ms < 0 ? -1 : timeout_ms);
    if (r < 0) return -1;
    if (r == 0) return 0;
    if (pfd.revents & POLLNVAL) {
        errno = EBADF;
        return -1;
    }
    return 1;      /* input, or a hang-up: a read will not block either way */
}

int tui_tty_read_key(int fd, tui_event *ev) {
    if (!ev) {
        errno = EINVAL;
        return -1;
    }
    tui_decoder d;
    tui_decoder_init(&d);
    int rc = -1;
    for (;;) {
        if (tui_decoder_next(&d, ev)) { rc = 1; break; }
        if (tui_decoder_pending(&d)) {
            /* Something incomplete is held: wait at most the ESC delay
             * for the rest of it. */
            struct pollfd pfd = { .fd = fd, .events = POLLIN, .revents = 0 };
            int pr = poll(&pfd, 1, TUI_ESC_DELAY_DEFAULT);
            if (pr < 0) {
                if (errno == EINTR) continue;
                break;
            }
            if (pr == 0) {
                if (tui_decoder_timeout(&d, ev)) { rc = 1; break; }
                continue;
            }
        }
        unsigned char c;
        ssize_t n = read(fd, &c, 1);
        if (n < 0 && errno == EINTR) continue;
        if (n == 0 || (n < 0 && errno == EIO)) {
            /* The end of input (EIO: the terminal hung up).  A key held
             * for its ESC delay is delivered first. */
            if (tui_decoder_timeout(&d, ev)) {
                rc = 1;
                break;
            }
            memset(ev, 0, sizeof(*ev));
            ev->type = TUI_EV_HANGUP;
            rc = 0;
            break;
        }
        if (n < 0) break;
        tui_decoder_feed(&d, (const char *)&c, 1);
    }
    if (rc == 1 && ev->type == TUI_EV_PASTE) {
        /* The paste lives in the decoder, which ends here: it is not
         * returned through this one-shot interface. */
        memset(ev, 0, sizeof(*ev));
        ev->type = TUI_EV_PASTE;
        ev->paste = "";
    }
    int e = errno;
    tui_decoder_free(&d);
    errno = e;
    return rc;
}

/* Reads one line from fd with read(2), never stdio: stdio would buffer
 * input that belongs to whoever reads the descriptor next. */
static int read_line(int fd, char *buf, size_t size) {
    size_t len = 0;
    int got_any = 0;
    for (;;) {
        char c;
        ssize_t n = read(fd, &c, 1);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (n == 0) {
            if (!got_any) return -1;
            break;
        }
        got_any = 1;
        if (c == '\n') break;
        if (len + 1 < size) buf[len++] = c;
    }
    if (len > 0 && buf[len - 1] == '\r') len--;
    buf[len] = '\0';
    return (int)len;
}

/* While a password is read with echo off: the mode to restore if a fatal
 * signal ends the process, and the dispositions to put back. */
static const int g_pw_signals[] = { SIGINT, SIGTERM, SIGHUP, SIGQUIT };
#define N_PW_SIGNALS ((int)(sizeof(g_pw_signals) / sizeof(g_pw_signals[0])))
static struct termios   g_pw_saved;
static int              g_pw_fd = -1;
static struct sigaction g_pw_prev[N_PW_SIGNALS];
static int              g_pw_installed[N_PW_SIGNALS];

/* Async-signal-safe (tcsetattr, sigaction): the mode and the previous
 * disposition back, for pw_on_signal(). */
static void pw_prepare(int sig) {
    tcsetattr(g_pw_fd, TCSANOW, &g_pw_saved);
    for (int i = 0; i < N_PW_SIGNALS; i++) {
        if (g_pw_signals[i] == sig) sigaction(sig, &g_pw_prev[i], NULL);
    }
}

/* Raised again, the signal is delivered with its default action as the
 * handler returns; one line, as on_fatal() in term.c explains. */
static void pw_on_signal(int sig) { pw_prepare(sig); raise(sig); }

static void pw_install(int fd, const struct termios *saved) {
    g_pw_saved = *saved;
    g_pw_fd = fd;
    for (int i = 0; i < N_PW_SIGNALS; i++) {
        g_pw_installed[i] = 0;
        struct sigaction cur;
        if (sigaction(g_pw_signals[i], NULL, &cur) != 0) continue;
        /* Only a signal that would end the process as it is: one the
         * program handles or ignores stays the program's. */
        if ((cur.sa_flags & SA_SIGINFO) || cur.sa_handler != SIG_DFL) continue;
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sa.sa_handler = pw_on_signal;
        sigemptyset(&sa.sa_mask);
        if (sigaction(g_pw_signals[i], &sa, &g_pw_prev[i]) == 0) g_pw_installed[i] = 1;
    }
}

static void pw_remove(void) {
    for (int i = 0; i < N_PW_SIGNALS; i++) {
        if (g_pw_installed[i]) sigaction(g_pw_signals[i], &g_pw_prev[i], NULL);
        g_pw_installed[i] = 0;
    }
    g_pw_fd = -1;
}

int tui_tty_read_password(int in_fd, int out_fd, const char *prompt,
                          char *buf, size_t size) {
    if (!buf || size == 0) {
        errno = EINVAL;
        return -1;
    }
    buf[0] = '\0';
    if (!isatty(in_fd)) return read_line(in_fd, buf, size);

    struct termios saved, quiet;
    if (tcgetattr(in_fd, &saved) != 0) return -1;
    quiet = saved;
    quiet.c_lflag &= ~(tcflag_t)ECHO;
    quiet.c_lflag |= ICANON;
    pw_install(in_fd, &saved);
    /* Echo goes off before the prompt appears: whatever is typed once the
     * prompt is visible is never echoed (tg-cli wrote the prompt first, and
     * a quick typist could see the start of the password). */
    if (tcsetattr(in_fd, TCSANOW, &quiet) != 0) {
        int e = errno;
        pw_remove();
        errno = e;
        return -1;
    }
    if (prompt && (tui__write_all(out_fd, prompt, strlen(prompt)) != 0 ||
                   tui__write_all(out_fd, ": ", 2) != 0)) {
        int e = errno;
        tcsetattr(in_fd, TCSANOW, &saved);
        pw_remove();
        errno = e;
        return -1;
    }
    int n = read_line(in_fd, buf, size);
    int e = errno;
    tcsetattr(in_fd, TCSANOW, &saved);
    pw_remove();
    if (tui__write_all(out_fd, "\n", 1) != 0 && n >= 0) return -1;
    errno = e;
    return n;
}


int tui_tty_read_password_masked(int in_fd, int out_fd, const char *prompt,
                                 char *buf, size_t size) {
    if (!buf || size == 0) {
        errno = EINVAL;
        return -1;
    }
    buf[0] = '\0';
    if (!isatty(in_fd)) return read_line(in_fd, buf, size);

    struct termios saved, quiet;
    if (tcgetattr(in_fd, &saved) != 0) return -1;
    quiet = saved;
    quiet.c_lflag &= ~(tcflag_t)(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    /* Ctrl-S/Ctrl-Q are keys here, and Enter arrives as CR. */
    quiet.c_iflag &= ~(tcflag_t)(IXON | ICRNL);
    quiet.c_cc[VMIN] = 1;
    quiet.c_cc[VTIME] = 0;
    pw_install(in_fd, &saved);
    /* TCSAFLUSH: what was typed ahead was echoed by the previous, canonical
     * field and must not become part of the password.  Echo is off before
     * the prompt appears. */
    if (tcsetattr(in_fd, TCSAFLUSH, &quiet) != 0) {
        int e = errno;
        pw_remove();
        errno = e;
        return -1;
    }
    int rc = -1;
    int e = 0;
    size_t len = 0;     /* bytes in buf */
    int masks = 0;      /* mask characters on the screen */
    if (prompt && (tui__write_all(out_fd, prompt, strlen(prompt)) != 0 ||
                   tui__write_all(out_fd, ": ", 2) != 0)) {
        e = errno;
    } else {
        for (;;) {
            tui_event ev;
            int r = tui_tty_read_key(in_fd, &ev);
            if (r < 0) {
                e = errno;
                break;
            }
            if (r == 0) break;                              /* end of input */
            if (ev.type != TUI_EV_KEY) continue;
            if (ev.key == TUI_KEY_ENTER) {
                rc = (int)len;
                break;
            }
            if (TUI_EV_IS_CTRL(&ev, 'c')) {
                tui__write_all(out_fd, "^C", 2);
                break;
            }
            if (TUI_EV_IS_CTRL(&ev, 'd')) break;
            if (TUI_EV_IS_CTRL(&ev, 'u')) {                 /* clear the field */
                int failed = 0;
                for (; masks > 0 && !failed; masks--) {
                    failed = tui__write_all(out_fd, "\b \b", 3) != 0;
                }
                if (failed) {
                    e = errno;
                    break;
                }
                len = 0;
                continue;
            }
            if (ev.key == TUI_KEY_BACKSPACE) {              /* one character */
                if (len == 0) continue;
                do { len--; } while (len > 0 && ((unsigned char)buf[len] & 0xC0) == 0x80);
                masks--;
                if (tui__write_all(out_fd, "\b \b", 3) != 0) {
                    e = errno;
                    break;
                }
                continue;
            }
            if (ev.key != TUI_KEY_CHAR || (ev.mods & (TUI_MOD_CTRL | TUI_MOD_ALT))) continue;
            size_t n = strlen(ev.utf8);
            if (len + n >= size) {                          /* the field is full */
                tui__write_all(out_fd, "\a", 1);
                continue;
            }
            memcpy(buf + len, ev.utf8, n);
            len += n;
            masks++;
            if (tui__write_all(out_fd, "*", 1) != 0) {
                e = errno;
                break;
            }
        }
    }
    buf[len] = '\0';
    tcsetattr(in_fd, TCSANOW, &saved);
    pw_remove();
    if (rc < 0) {
        memset(buf, 0, size);
        errno = e;
    }
    if (tui__write_all(out_fd, "\n", 1) != 0 && rc >= 0) return -1;
    return rc;
}
