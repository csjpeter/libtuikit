/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

#ifndef LIBTUIKIT_TERM_H
#define LIBTUIKIT_TERM_H

/**
 * @file tuikit/term.h
 * @brief A full-screen terminal session: mode, size, resize, signals, input.
 *
 * tui_term_open() takes over the terminal; every way out gives it back:
 * tui_term_close(), exit(3), the fatal signals SIGTERM, SIGHUP, SIGINT and
 * SIGQUIT (which are re-raised so the exit status stays truthful), and
 * suspension (SIGTSTP, then SIGCONT re-enters and reports a resize so the
 * program repaints).
 *
 * A resize is never lost: the SIGWINCH handler writes to a pipe that
 * tui_term_wait() polls together with the terminal, so a resize during
 * drawing wakes the next wait.  See tui_term_open(3) and tui_term_wait(3).
 */

#include "tuikit/api.h"
#include "tuikit/buf.h"
#include "tuikit/event.h"
#include "tuikit/tty.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct pollfd;

/** @brief A terminal session (opaque). */
typedef struct tui_term tui_term;

/** @brief How to open a session. */
typedef struct tui_term_opts {
    int      in_fd;        /**< input terminal; default STDIN_FILENO */
    int      out_fd;       /**< output; default STDOUT_FILENO */
    unsigned flags;        /**< TUI_TERM_* (tuikit/tty.h) */
    int      esc_delay_ms; /**< bare-ESC delay; 0 means the default */
} tui_term_opts;

/**
 * @brief The defaults: stdin/stdout, TUI_TERM_ALTSCREEN |
 *        TUI_TERM_HIDE_CURSOR | TUI_TERM_NOWRAP, ESC delay
 *        TUI_ESC_DELAY_DEFAULT.
 */
TUI_API void tui_term_opts_init(tui_term_opts *o);

/**
 * @brief Takes over the terminal.  @p o may be NULL for the defaults.
 * @return The session, or NULL with errno: ENOTTY (input is not a
 *         terminal), EBUSY (another session with signal handling is
 *         open), ENOMEM, or the error of a system call.  On failure the
 *         terminal is left as it was.
 */
TUI_API tui_term *tui_term_open(const tui_term_opts *o);

/**
 * @brief Gives the terminal back (modes, screen, cursor, signal handlers)
 *        and frees the session.  Safe with NULL.
 * @return 0, or -1 if restoring the mode failed (the session is freed
 *         anyway).
 */
TUI_API int tui_term_close(tui_term *t);

/**
 * @brief The current size.
 * @return 0, or -1 with errno ENOTTY when neither fd reports a size.
 */
TUI_API int tui_term_size(tui_term *t, int *rows, int *cols);

/** @brief Writes all of @p n bytes to the output (EINTR/EAGAIN-safe). */
TUI_API int tui_term_write(tui_term *t, const void *p, size_t n);

/** @brief Writes the buffer to the output and empties it. */
TUI_API int tui_term_flush_buf(tui_term *t, tui_buf *b);

/**
 * @brief Gives the terminal back temporarily, e.g. to run an editor.
 * @return 0, or -1 with errno.
 */
TUI_API int tui_term_suspend(tui_term *t);

/**
 * @brief Takes the terminal over again after tui_term_suspend(); the next
 *        tui_term_wait() reports TUI_EV_RESIZE so the program repaints.
 */
TUI_API int tui_term_resume(tui_term *t);

/**
 * @brief Waits for the next event.
 *
 * @param timeout_ms  milliseconds; negative waits indefinitely, 0 polls.
 * @param extra       further descriptors to watch (may be NULL); their
 *                    @c revents are filled in when one is reported.
 * @param n_extra     number of entries in @p extra.
 * @return 1 with @p ev filled, 0 on timeout, -1 on error with errno.
 *
 * Events: TUI_EV_KEY, TUI_EV_PASTE, TUI_EV_RESIZE (several resizes
 * collapse into one, carrying the current size), TUI_EV_FD (an entry of
 * @p extra is ready; fd_index says which), TUI_EV_HANGUP (the terminal is
 * gone; every later call reports it again).
 */
TUI_API int tui_term_wait(tui_term *t, int timeout_ms,
                          struct pollfd *extra, int n_extra, tui_event *ev);

/** @brief The input descriptor, for a caller that polls by itself. */
TUI_API int tui_term_fd(const tui_term *t);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_TERM_H */
