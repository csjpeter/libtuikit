/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Péter Császár */

#ifndef LIBTUIKIT_BUF_H
#define LIBTUIKIT_BUF_H

/**
 * @file tuikit/buf.h
 * @brief A growable byte buffer: the abstract output of the renderer.
 *
 * Everything libtuikit draws is appended to a tui_buf.  The program (or
 * tui_term_flush_buf()) writes the buffer to a file descriptor; a test
 * hands the same bytes to a terminal emulator instead.
 *
 * An allocation failure is sticky: @c failed is set, later appends do
 * nothing, and tui_buf_write_fd() refuses to write the half-built frame.
 */

#include "tuikit/api.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct tui_buf {
    char  *data;   /**< bytes; NUL-terminated when len > 0 or after init */
    size_t len;    /**< bytes in use */
    size_t cap;    /**< bytes allocated */
    int    failed; /**< an append failed; the content is incomplete */
} tui_buf;

/** @brief An empty buffer.  Allocates nothing. */
TUI_API void tui_buf_init(tui_buf *b);

/** @brief Releases the memory and re-initialises the buffer. */
TUI_API void tui_buf_free(tui_buf *b);

/** @brief Empties the buffer and clears @c failed; keeps the memory. */
TUI_API void tui_buf_reset(tui_buf *b);

/**
 * @brief Appends @p n bytes.
 * @return 0, or -1 with errno ENOMEM (and @c failed set).
 */
TUI_API int tui_buf_append(tui_buf *b, const void *p, size_t n);

/** @brief Appends a NUL-terminated string. */
TUI_API int tui_buf_puts(tui_buf *b, const char *s);

/** @brief Appends formatted text, as printf(3). */
TUI_API int tui_buf_printf(tui_buf *b, const char *fmt, ...)
#if defined(__GNUC__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/**
 * @brief Writes the whole buffer to @p fd, retrying on EINTR and waiting
 *        on EAGAIN, then empties it.
 * @return 0, or -1 with errno: ENOMEM when @c failed was set (nothing is
 *         written), or the error of write(2).
 */
TUI_API int tui_buf_write_fd(tui_buf *b, int fd);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_BUF_H */
