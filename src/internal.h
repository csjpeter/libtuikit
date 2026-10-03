/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

#ifndef LIBTUIKIT_INTERNAL_H
#define LIBTUIKIT_INTERNAL_H

/*
 * Shared between the library's own sources; not installed.
 */

#include <stddef.h>
#include <stdint.h>

#if !defined(_WIN32)
#include <termios.h>

/* The raw-mode transformation of a termios state, as tui_tty_raw_enter(3)
 * documents it.  Used by tui_tty and tui_term alike. */
void tui__make_raw(struct termios *t, unsigned flags);

/* Applies @p want and reads it back; the terminal accepted it only if the
 * flags we changed read back as requested.  0, or -1 with errno (EIO when
 * the terminal silently ignored part of the change). */
int tui__apply_mode(int fd, const struct termios *want);
#endif

/* write(2) of all @p n bytes, retrying on EINTR and polling on EAGAIN. */
int tui__write_all(int fd, const void *p, size_t n);

/* Milliseconds of a monotonic clock. */
int64_t tui__now_ms(void);

#endif /* LIBTUIKIT_INTERNAL_H */
