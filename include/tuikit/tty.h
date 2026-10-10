/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Péter Császár */

#ifndef LIBTUIKIT_TTY_H
#define LIBTUIKIT_TTY_H

/**
 * @file tuikit/tty.h
 * @brief File-descriptor level terminal helpers that need no session.
 *
 * For programs that are not full-screen all the time: size a paged
 * listing, enter raw mode for one keypress, ask for a password.  This is
 * the style of email-cli's terminal layer, and every capability of that
 * layer is here (see libtuikit(7), "email-cli equivalents").
 *
 * On Windows these compile to email-cli's stub behaviour: not a terminal,
 * 80 columns, 0 rows, raw mode unavailable.
 */

#include "tuikit/api.h"
#include "tuikit/event.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @name Mode flags, shared with tui_term_opts.flags. */
/**@{*/
#define TUI_TERM_ALTSCREEN    0x0001u /**< switch to the alternate screen */
#define TUI_TERM_HIDE_CURSOR  0x0002u /**< hide the cursor while open */
#define TUI_TERM_NOWRAP       0x0004u /**< disable auto-wrap while open */
#define TUI_TERM_PASTE        0x0008u /**< bracketed paste: TUI_EV_PASTE */
#define TUI_TERM_KEEP_ISIG    0x0010u /**< Ctrl-C/Ctrl-Z/Ctrl-\ stay signals */
#define TUI_TERM_KEEP_IXON    0x0020u /**< Ctrl-S/Ctrl-Q stay flow control */
#define TUI_TERM_NO_SIGNALS   0x0040u /**< install no signal handlers */
/**@}*/

/** @brief Default ESC delay in milliseconds (email-cli's 100 ms). */
#define TUI_ESC_DELAY_DEFAULT 100

/** @brief 1 when @p fd is a terminal, 0 otherwise. */
TUI_API int tui_tty_is(int fd);

/** @brief Columns of the terminal on @p fd; 80 when unknown. */
TUI_API int tui_tty_cols(int fd);

/** @brief Rows of the terminal on @p fd; 0 when unknown. */
TUI_API int tui_tty_rows(int fd);

/** @brief Saved terminal mode (opaque). */
typedef struct tui_tty_raw tui_tty_raw;

/**
 * @brief Saves the mode of the terminal on @p fd and enters raw mode: no
 *        canonical input, no echo, no IEXTEN, no CR/NL translation, and,
 *        unless kept by @p flags, no signal keys and no flow control.
 *
 * Every change is read back; if the terminal did not take it, nothing is
 * left changed.  Only TUI_TERM_KEEP_ISIG and TUI_TERM_KEEP_IXON are
 * meaningful in @p flags.
 * @return The saved state, or NULL with errno (ENOTTY, ENOMEM, EIO, ...).
 */
TUI_API tui_tty_raw *tui_tty_raw_enter(int fd, unsigned flags);

/**
 * @brief Restores the mode saved in @p *state, frees it and sets
 *        @p *state to NULL.  Safe with NULL or *state == NULL.
 */
TUI_API void tui_tty_raw_exit(tui_tty_raw **state);

#if defined(__GNUC__)
/** @brief Declares a tui_tty_raw pointer restored when it leaves scope. */
#  define TUI_RAII_RAW __attribute__((cleanup(tui_tty_raw_exit)))
#endif

/**
 * @brief Waits up to @p timeout_ms (negative: no limit) until @p fd has
 *        input -- tg-cli's terminal_wait_key(), with an interruption that
 *        can be told from an error.
 * @return 1 when a read will not block (input, or the end of input), 0 on
 *         timeout, -1 with errno: EINTR when a signal interrupted the
 *         wait, otherwise the error of poll(2).
 */
TUI_API int tui_tty_wait(int fd, int timeout_ms);

/**
 * @brief Reads one key from @p fd, blocking until there is one.
 *
 * The terminal is expected to be in raw mode.  Reads one byte at a time, so
 * no input beyond the key is consumed.  A lone ESC is reported after
 * TUI_ESC_DELAY_DEFAULT milliseconds of silence.
 * @return 1 with @p ev filled (TUI_EV_KEY or TUI_EV_PASTE), 0 at end of
 *         input (@p ev is TUI_EV_HANGUP), -1 on error with errno.
 */
TUI_API int tui_tty_read_key(int fd, tui_event *ev);

/**
 * @brief Asks for a password with echo off, as email-cli's
 *        terminal_read_password().
 *
 * When @p in_fd is a terminal, writes "<prompt>: " to @p out_fd, turns
 * echo off, reads one line, restores the mode and writes a newline.  When
 * it is not a terminal, reads one line with no prompt.  The trailing CR/LF
 * is removed; at most @p size - 1 bytes are kept, NUL-terminated.
 *
 * While echo is off, SIGINT, SIGTERM, SIGHUP and SIGQUIT -- those with
 * their default action -- restore the terminal before they end the
 * process, so Ctrl-C at the prompt does not leave the shell without echo.
 * @return The length of the password, or -1 at end of input or on error.
 */
TUI_API int tui_tty_read_password(int in_fd, int out_fd, const char *prompt,
                                  char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_TTY_H */
