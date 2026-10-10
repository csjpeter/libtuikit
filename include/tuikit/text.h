/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Public API: UTF-8, display widths, sanitising and fitting of text. */

#ifndef LIBTUIKIT_TEXT_H
#define LIBTUIKIT_TEXT_H

/**
 * @file tuikit/text.h
 * @brief UTF-8 and the display width of text.
 *
 * Widths come from a table built into the library, derived from glibc's
 * C.UTF-8 locale, so the program's locale has no effect: in the "C" locale
 * glibc's own wcwidth(3) reports -1 for every non-ASCII character.  A
 * printable code point the table does not know is one column wide, never
 * zero: text is never silently dropped.  See tui_cp_width(3).
 */

#include "tuikit/api.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief U+FFFD, what malformed UTF-8 decodes to. */
#define TUI_CP_REPLACEMENT 0xFFFDu

/** @brief U+00B7, what tui_cp_sanitize() puts in place of a control. */
#define TUI_CP_SANITIZED 0x00B7u

/**
 * @brief Decodes one code point from at most @p n bytes of @p s.
 * @return Bytes consumed (1-4); 0 when @p n is 0.  Malformed input, an
 *         overlong form, a surrogate or a value above U+10FFFF yields
 *         U+FFFD and consumes one byte; a sequence cut short by @p n
 *         yields U+FFFD and consumes the bytes present.
 */
TUI_API int tui_utf8_decode(const char *s, size_t n, uint32_t *cp);

/**
 * @brief Encodes @p cp into @p out (not NUL-terminated).
 * @return Bytes written (1-4).  An invalid code point is encoded as U+FFFD.
 */
TUI_API int tui_utf8_encode(uint32_t cp, char out[4]);

/** @brief Length of the UTF-8 sequence @p lead starts, 1 for a bad lead. */
TUI_API int tui_utf8_seq_len(unsigned char lead);

/**
 * @brief Display width of @p cp: 0 (combining, zero-width), 1 or 2 (wide),
 *        or -1 for a control character (C0, DEL, C1).
 */
TUI_API int tui_cp_width(uint32_t cp);

/**
 * @brief The code point to draw for @p cp: controls (C0, DEL, and C1 with
 *        the 8-bit CSI U+009B) become U+00B7, so text from an untrusted
 *        source cannot carry an escape sequence; everything else is
 *        returned unchanged.
 */
TUI_API uint32_t tui_cp_sanitize(uint32_t cp);

/**
 * @brief Display width of the UTF-8 string @p s, as drawn: controls count
 *        as the one column of their replacement.
 */
TUI_API int tui_str_width(const char *s);

/** @brief tui_str_width() of the first @p n bytes of @p s. */
TUI_API int tui_strn_width(const char *s, size_t n);

/**
 * @brief How many bytes of @p s fit into @p cols columns without cutting a
 *        character; zero-width characters following the last fitting one
 *        are included.
 * @param used  receives the columns those bytes take (may be NULL).
 */
TUI_API size_t tui_str_fit(const char *s, int cols, int *used);

/**
 * @brief Copies @p s into @p out, shortened to @p cols columns with a
 *        trailing "…" (U+2026) when it does not fit.
 * @return The columns the result takes, or -1 if @p outsz is too small
 *         for the result (then @p out holds as much as fits, terminated).
 */
TUI_API int tui_str_fit_ellipsis(const char *s, int cols, char *out, size_t outsz);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_TEXT_H */
