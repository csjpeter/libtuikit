/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* The full-screen session: raw mode, screen modes, size, signals and the event wait. */

/*
 * The full-screen session: raw mode, the screen modes around it, size,
 * resize notification, signals, and the event wait.
 *
 * Signal handlers touch only what POSIX lists as async-signal-safe:
 * tcsetattr, write, sigaction, sigprocmask, raise.  Everything they need is
 * prepared in file-scope variables before the handlers are installed, and
 * the handled signals are blocked while those variables change.
 */

#define _POSIX_C_SOURCE 200809L

#include "tuikit/term.h"
#include "internal.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#define SEQ_MAX 64

struct tui_term {
    int            in_fd, out_fd;
    unsigned       flags;
    int            esc_delay_ms;
    struct termios saved, raw;
    int            signals;          /* this session owns the handlers */
    int            wake_rd, wake_wr; /* self-pipe; -1 without signals */
    int            suspended;        /* tui_term_suspend() in effect */
    int            resize_pending;
    int            hangup;
    int64_t        pending_since;    /* last byte fed while incomplete */
    tui_decoder    dec;
    char           enter[SEQ_MAX];
    size_t         enter_len;
    char           leave[SEQ_MAX];
    size_t         leave_len;
};

/* ── State shared with the signal handlers ──────────────────────────── */

static const int g_signals[] = { SIGTERM, SIGHUP, SIGINT, SIGQUIT,
                                 SIGTSTP, SIGCONT, SIGWINCH };
#define N_SIGNALS ((int)(sizeof(g_signals) / sizeof(g_signals[0])))

static struct sigaction      g_prev[N_SIGNALS];
static int                   g_installed[N_SIGNALS];
static tui_term             *g_active;        /* the session with handlers */
static volatile sig_atomic_t g_mode_active;   /* raw mode is in effect */
static volatile sig_atomic_t g_user_suspended;
static int                   g_tty_fd = -1, g_out_fd = -1, g_wake_wr = -1;
static struct termios        g_saved, g_raw;
static char                  g_enter[SEQ_MAX], g_leave[SEQ_MAX];
static size_t                g_enter_len, g_leave_len;
static int                   g_atexit_registered;

/* The slot of a handled signal; every caller passes one of g_signals. */
static int sig_index(int sig) {
    int i = 0;
    while (i < N_SIGNALS - 1 && g_signals[i] != sig) i++;
    return i;
}

/* SIGWINCH and SIGCONT are observed, not taken over: a handler the
 * program installed before us still runs. */
static void chain(int sig) {
    int i = sig_index(sig);
    if (g_prev[i].sa_flags & SA_SIGINFO) return;
    void (*h)(int) = g_prev[i].sa_handler;
    if (h != SIG_DFL && h != SIG_IGN) h(sig);
}

/* Async-signal-safe: hands the terminal back. */
static void leave_mode_async(void) {
    if (!g_mode_active) return;
    g_mode_active = 0;
    ssize_t w = write(g_out_fd, g_leave, g_leave_len);
    (void)w;
    tcsetattr(g_tty_fd, TCSANOW, &g_saved);
}

/* Async-signal-safe: takes the terminal over again. */
static void enter_mode_async(void) {
    if (g_mode_active || g_user_suspended) return;
    tcsetattr(g_tty_fd, TCSANOW, &g_raw);
    ssize_t w = write(g_out_fd, g_enter, g_enter_len);
    (void)w;
    g_mode_active = 1;
}

static void wake_async(void) {
    if (g_wake_wr < 0) return;
    /* EAGAIN means a wake-up is already waiting: nothing is lost. */
    ssize_t w = write(g_wake_wr, "w", 1);
    (void)w;
}

/* Gives the terminal back and puts the previous disposition of @p sig
 * back, for on_fatal(). */
static void prepare_fatal(int sig) {
    int e = errno;
    leave_mode_async();
    int i = sig_index(sig);
    sigaction(sig, &g_prev[i], NULL);
    g_installed[i] = 0;
    errno = e;
}

/* The signal is blocked while this handler runs; raised again, it is
 * delivered with the previous disposition as soon as the handler returns,
 * so the exit status is the signal's.  One line on purpose: the process
 * dies at that return, and a closing brace on a line of its own would be a
 * line that can never be measured as run. */
static void on_fatal(int sig) { prepare_fatal(sig); raise(sig); }

static void on_tstp(int sig) {
    (void)sig;
    int e = errno;
    leave_mode_async();
    struct sigaction dfl, mine;
    memset(&dfl, 0, sizeof(dfl));
    dfl.sa_handler = SIG_DFL;
    sigemptyset(&dfl.sa_mask);
    sigaction(SIGTSTP, &dfl, &mine);
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGTSTP);
    sigprocmask(SIG_UNBLOCK, &s, NULL);
    raise(SIGTSTP);                    /* stops here until SIGCONT */
    sigprocmask(SIG_BLOCK, &s, NULL);
    sigaction(SIGTSTP, &mine, NULL);
    enter_mode_async();
    wake_async();                      /* the screen has to be repainted */
    errno = e;
}

static void on_cont(int sig) {
    int e = errno;
    /* Stopped by something other than SIGTSTP (SIGSTOP): the shell may
     * have reset the terminal meanwhile.  Take it over again. */
    if (g_mode_active && !g_user_suspended) {
        tcsetattr(g_tty_fd, TCSANOW, &g_raw);
        ssize_t w = write(g_out_fd, g_enter, g_enter_len);
        (void)w;
    } else {
        enter_mode_async();
    }
    wake_async();
    errno = e;
    chain(sig);
}

static void on_winch(int sig) {
    int e = errno;
    wake_async();
    errno = e;
    chain(sig);
}

static void at_exit_restore(void) {
    if (g_active) leave_mode_async();
}

static void block_handled(sigset_t *old) {
    sigset_t s;
    sigemptyset(&s);
    for (int i = 0; i < N_SIGNALS; i++) sigaddset(&s, g_signals[i]);
    sigprocmask(SIG_BLOCK, &s, old);
}

static void install_handlers(void) {
    for (int i = 0; i < N_SIGNALS; i++) {
        int sig = g_signals[i];
        struct sigaction prev;
        if (sigaction(sig, NULL, &prev) != 0) continue;
        int observe = (sig == SIGWINCH || sig == SIGCONT);
        /* A fatal signal or SIGTSTP is taken over only while it has its
         * default action.  Ignored, it stays ignored (nohup); caught by the
         * program (mamind's interrupt flag), it stays the program's, and
         * the program restores the terminal by tui_term_close(). */
        if (!observe && ((prev.sa_flags & SA_SIGINFO) || prev.sa_handler != SIG_DFL))
            continue;
        struct sigaction sa;
        memset(&sa, 0, sizeof(sa));
        sigemptyset(&sa.sa_mask);
        switch (sig) {
        case SIGTSTP:  sa.sa_handler = on_tstp; break;
        case SIGCONT:  sa.sa_handler = on_cont; break;
        case SIGWINCH: sa.sa_handler = on_winch; sa.sa_flags = SA_RESTART; break;
        default:       sa.sa_handler = on_fatal; break;
        }
        if (sigaction(sig, &sa, &g_prev[i]) == 0) g_installed[i] = 1;
    }
}

static void remove_handlers(void) {
    for (int i = 0; i < N_SIGNALS; i++) {
        if (!g_installed[i]) continue;
        sigaction(g_signals[i], &g_prev[i], NULL);
        g_installed[i] = 0;
    }
}

/* ── Helpers ────────────────────────────────────────────────────────── */

static void append_seq(char *buf, size_t *len, const char *s) {
    size_t n = strlen(s);
    if (*len + n < SEQ_MAX) {
        memcpy(buf + *len, s, n);
        *len += n;
    }
}

static void build_sequences(tui_term *t) {
    t->enter_len = t->leave_len = 0;
    if (t->flags & TUI_TERM_ALTSCREEN)   append_seq(t->enter, &t->enter_len, "\033[?1049h");
    if (t->flags & TUI_TERM_HIDE_CURSOR) append_seq(t->enter, &t->enter_len, "\033[?25l");
    if (t->flags & TUI_TERM_NOWRAP)      append_seq(t->enter, &t->enter_len, "\033[?7l");
    if (t->flags & TUI_TERM_PASTE)       append_seq(t->enter, &t->enter_len, "\033[?2004h");

    append_seq(t->leave, &t->leave_len, "\033[0m");
    if (t->flags & TUI_TERM_PASTE)       append_seq(t->leave, &t->leave_len, "\033[?2004l");
    if (t->flags & TUI_TERM_NOWRAP)      append_seq(t->leave, &t->leave_len, "\033[?7h");
    if (t->flags & TUI_TERM_HIDE_CURSOR) append_seq(t->leave, &t->leave_len, "\033[?25h");
    if (t->flags & TUI_TERM_ALTSCREEN)   append_seq(t->leave, &t->leave_len, "\033[?1049l");
}

static int set_flags(int fd) {
    int fl = fcntl(fd, F_GETFL);
    if (fl < 0 || fcntl(fd, F_SETFL, fl | O_NONBLOCK) != 0) return -1;
    int fd_fl = fcntl(fd, F_GETFD);
    if (fd_fl < 0 || fcntl(fd, F_SETFD, fd_fl | FD_CLOEXEC) != 0) return -1;
    return 0;
}

static void restore_mode(int fd, const struct termios *t) {
    while (tcsetattr(fd, TCSANOW, t) != 0 && errno == EINTR) {}
}

/* ── Public API ─────────────────────────────────────────────────────── */

void tui_term_opts_init(tui_term_opts *o) {
    if (!o) return;
    memset(o, 0, sizeof(*o));
    o->in_fd = STDIN_FILENO;
    o->out_fd = STDOUT_FILENO;
    o->flags = TUI_TERM_ALTSCREEN | TUI_TERM_HIDE_CURSOR | TUI_TERM_NOWRAP;
    o->esc_delay_ms = TUI_ESC_DELAY_DEFAULT;
}

/* Undoes a half-made session; errno is kept. */
static tui_term *discard(tui_term *t) {
    int e = errno;
    if (t->wake_rd >= 0) close(t->wake_rd);
    if (t->wake_wr >= 0) close(t->wake_wr);
    tui_decoder_free(&t->dec);
    free(t);
    errno = e;
    return NULL;
}

/* Gives the mode back after a failed switch; errno is kept. */
static tui_term *switch_failed(tui_term *t, const sigset_t *old) {
    int e = errno;
    restore_mode(t->in_fd, &t->saved);
    sigprocmask(SIG_SETMASK, old, NULL);
    errno = e;
    return discard(t);
}

tui_term *tui_term_open(const tui_term_opts *o) {
    tui_term_opts def;
    if (!o) {
        tui_term_opts_init(&def);
        o = &def;
    }
    if (!isatty(o->in_fd)) {
        errno = ENOTTY;
        return NULL;
    }
    int with_signals = !(o->flags & TUI_TERM_NO_SIGNALS);
    if (with_signals && g_active) {
        errno = EBUSY;
        return NULL;
    }

    tui_term *t = calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->in_fd = o->in_fd;
    t->out_fd = o->out_fd;
    t->flags = o->flags;
    t->esc_delay_ms = o->esc_delay_ms > 0 ? o->esc_delay_ms : TUI_ESC_DELAY_DEFAULT;
    t->wake_rd = t->wake_wr = -1;
    t->pending_since = -1;
    tui_decoder_init(&t->dec);
    build_sequences(t);

    if (tcgetattr(t->in_fd, &t->saved) != 0) return discard(t);
    t->raw = t->saved;
    tui__make_raw(&t->raw, t->flags);

    if (with_signals) {
        int p[2];
        if (pipe(p) != 0) return discard(t);
        t->wake_rd = p[0];
        t->wake_wr = p[1];
        if (set_flags(p[0]) != 0 || set_flags(p[1]) != 0) return discard(t);
    }

    /* No handled signal may arrive half-way through the switch. */
    sigset_t old;
    block_handled(&old);

    if (tui__apply_mode(t->in_fd, &t->raw) != 0) return switch_failed(t, &old);
    if (tui__write_all(t->out_fd, t->enter, t->enter_len) != 0) return switch_failed(t, &old);

    if (with_signals) {
        t->signals = 1;
        g_tty_fd = t->in_fd;
        g_out_fd = t->out_fd;
        g_wake_wr = t->wake_wr;
        g_saved = t->saved;
        g_raw = t->raw;
        memcpy(g_enter, t->enter, t->enter_len);
        g_enter_len = t->enter_len;
        memcpy(g_leave, t->leave, t->leave_len);
        g_leave_len = t->leave_len;
        g_user_suspended = 0;
        g_mode_active = 1;
        g_active = t;
        install_handlers();
        if (!g_atexit_registered && atexit(at_exit_restore) == 0)
            g_atexit_registered = 1;
    }
    sigprocmask(SIG_SETMASK, &old, NULL);
    return t;
}

/* Hands the terminal back; used by close and suspend. */
static int leave(tui_term *t) {
    int rc = 0;
    if (tui__write_all(t->out_fd, t->leave, t->leave_len) != 0) rc = -1;
    while (tcsetattr(t->in_fd, TCSANOW, &t->saved) != 0) {
        if (errno != EINTR) { rc = -1; break; }
    }
    return rc;
}

int tui_term_close(tui_term *t) {
    if (!t) return 0;
    int rc = 0;
    if (t->signals) {
        sigset_t old;
        block_handled(&old);
        /* A fatal signal may already have handed the terminal back. */
        if (g_mode_active) {
            g_mode_active = 0;
            rc = leave(t);
        }
        remove_handlers();
        g_active = NULL;
        g_wake_wr = -1;
        g_user_suspended = 0;
        sigprocmask(SIG_SETMASK, &old, NULL);
        close(t->wake_rd);
        close(t->wake_wr);
    } else if (!t->suspended) {
        rc = leave(t);
    }
    int e = errno;
    tui_decoder_free(&t->dec);
    free(t);
    errno = e;
    return rc;
}

int tui_term_size(tui_term *t, int *rows, int *cols) {
    if (!t) {
        errno = EINVAL;
        return -1;
    }
    struct winsize ws;
    if ((ioctl(t->out_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0) ||
        (ioctl(t->in_fd, TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 && ws.ws_col > 0)) {
        if (rows) *rows = ws.ws_row;
        if (cols) *cols = ws.ws_col;
        return 0;
    }
    errno = ENOTTY;
    return -1;
}

int tui_term_write(tui_term *t, const void *p, size_t n) {
    if (!t || (!p && n > 0)) {
        errno = EINVAL;
        return -1;
    }
    return tui__write_all(t->out_fd, p, n);
}

int tui_term_flush_buf(tui_term *t, tui_buf *b) {
    if (!t || !b) {
        errno = EINVAL;
        return -1;
    }
    return tui_buf_write_fd(b, t->out_fd);
}

int tui_term_suspend(tui_term *t) {
    if (!t) {
        errno = EINVAL;
        return -1;
    }
    if (t->suspended) return 0;
    int rc;
    if (t->signals) {
        sigset_t old;
        block_handled(&old);
        g_user_suspended = 1;
        rc = 0;
        if (g_mode_active) {
            g_mode_active = 0;
            rc = leave(t);
        }
        sigprocmask(SIG_SETMASK, &old, NULL);
    } else {
        rc = leave(t);
    }
    t->suspended = 1;
    return rc;
}

int tui_term_resume(tui_term *t) {
    if (!t) {
        errno = EINVAL;
        return -1;
    }
    if (!t->suspended) return 0;
    sigset_t old;
    if (t->signals) block_handled(&old);
    int rc = tui__apply_mode(t->in_fd, &t->raw);
    if (rc == 0) rc = tui__write_all(t->out_fd, t->enter, t->enter_len);
    if (rc == 0) {
        t->suspended = 0;
        t->resize_pending = 1;      /* everything has to be drawn again */
        if (t->signals) {
            g_user_suspended = 0;
            g_mode_active = 1;
        }
    } else {
        int e = errno;
        restore_mode(t->in_fd, &t->saved);
        errno = e;
    }
    if (t->signals) {
        int e = errno;
        sigprocmask(SIG_SETMASK, &old, NULL);
        errno = e;
    }
    return rc;
}

int tui_term_fd(const tui_term *t) {
    return t ? t->in_fd : -1;
}

static void drain_wake(tui_term *t) {
    char buf[64];
    while (read(t->wake_rd, buf, sizeof(buf)) > 0) {}
}

/* Reads what the terminal has; 1 = got bytes, 0 = nothing, -1 = error,
 * 2 = the terminal is gone. */
static int read_input(tui_term *t) {
    size_t held = t->dec.in_len - t->dec.in_pos;
    size_t room = sizeof(t->dec.in) - held;
    if (room == 0) return 0;
    char buf[sizeof(t->dec.in)];
    ssize_t n = read(t->in_fd, buf, room);
    if (n > 0) {
        tui_decoder_feed(&t->dec, buf, (size_t)n);
        t->pending_since = tui__now_ms();
        return 1;
    }
    if (n == 0) return 2;
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return 0;
    if (errno == EIO) return 2;          /* Linux: the other side hung up */
    return -1;
}

#define POLL_STATIC 16

int tui_term_wait(tui_term *t, int timeout_ms, struct pollfd *extra,
                  int n_extra, tui_event *ev) {
    if (!t || !ev || n_extra < 0 || (n_extra > 0 && !extra)) {
        errno = EINVAL;
        return -1;
    }
    struct pollfd fixed[POLL_STATIC];
    struct pollfd *pfd = fixed;
    int npfd = 2 + n_extra;
    if (npfd > POLL_STATIC) {
        pfd = malloc((size_t)npfd * sizeof(*pfd));
        if (!pfd) return -1;
    }
    int64_t deadline = timeout_ms >= 0 ? tui__now_ms() + timeout_ms : -1;
    int rc;

    for (;;) {
        if (t->hangup) {
            /* What arrived before the end is delivered first: a lone ESC
             * held for its delay is still a key the user pressed. */
            if (tui_decoder_timeout(&t->dec, ev)) {
                rc = 1;
                break;
            }
            memset(ev, 0, sizeof(*ev));
            ev->type = TUI_EV_HANGUP;
            rc = 1;
            break;
        }
        if (t->resize_pending) {
            t->resize_pending = 0;
            memset(ev, 0, sizeof(*ev));
            ev->type = TUI_EV_RESIZE;
            if (tui_term_size(t, &ev->rows, &ev->cols) != 0) {
                rc = -1;
                break;
            }
            rc = 1;
            break;
        }
        if (tui_decoder_next(&t->dec, ev)) {
            rc = 1;
            break;
        }

        int64_t now = tui__now_ms();
        int wait = -1;
        if (deadline >= 0) wait = deadline > now ? (int)(deadline - now) : 0;
        if (tui_decoder_pending(&t->dec)) {
            if (t->pending_since < 0) t->pending_since = now;
            int64_t esc_left = t->pending_since + t->esc_delay_ms - now;
            if (esc_left <= 0) {
                t->pending_since = -1;
                if (tui_decoder_timeout(&t->dec, ev)) {
                    rc = 1;
                    break;
                }
                continue;
            }
            if (wait < 0 || esc_left < wait) wait = (int)esc_left;
        } else {
            t->pending_since = -1;
        }

        pfd[0].fd = t->in_fd;
        pfd[0].events = POLLIN;
        pfd[0].revents = 0;
        pfd[1].fd = t->wake_rd;          /* -1 is ignored by poll(2) */
        pfd[1].events = POLLIN;
        pfd[1].revents = 0;
        for (int i = 0; i < n_extra; i++) {
            pfd[2 + i] = extra[i];
            pfd[2 + i].revents = 0;
        }

        int pr = poll(pfd, (nfds_t)npfd, wait);
        if (pr < 0) {
            if (errno == EINTR) continue;
            rc = -1;
            break;
        }
        if (pr == 0) {
            /* An incomplete sequence stays held; its ESC delay keeps
             * running across calls (pending_since is not reset). */
            if (deadline >= 0 && tui__now_ms() >= deadline) {
                rc = 0;
                break;
            }
            continue;
        }
        if (pfd[1].revents & POLLIN) {
            drain_wake(t);
            t->resize_pending = 1;
            continue;
        }
        if (pfd[0].revents & POLLIN) {
            int r = read_input(t);
            if (r < 0) {
                rc = -1;
                break;
            }
            if (r == 2) t->hangup = 1;
            continue;
        }
        if (pfd[0].revents & (POLLHUP | POLLERR | POLLNVAL)) {
            /* Gone without data: reading would answer 0 or EIO for ever
             * and the loop would spin (issue-manager, lesson 4). */
            t->hangup = 1;
            continue;
        }
        int found = -1;
        for (int i = 0; i < n_extra; i++) {
            extra[i].revents = pfd[2 + i].revents;
            if (found < 0 && pfd[2 + i].revents) found = i;
        }
        if (found >= 0) {
            memset(ev, 0, sizeof(*ev));
            ev->type = TUI_EV_FD;
            ev->fd_index = found;
            rc = 1;
            break;
        }
        /* Nothing usable (e.g. an error on the wake-up pipe): the next
         * round's poll uses what is left of the timeout. */
    }
    if (pfd != fixed) {
        int e = errno;
        free(pfd);
        errno = e;
    }
    return rc;
}

