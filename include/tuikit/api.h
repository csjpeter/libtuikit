/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Public API: version, error conventions and export macros of libtuikit. */

#ifndef LIBTUIKIT_API_H
#define LIBTUIKIT_API_H

/**
 * @file tuikit/api.h
 * @brief Export marker and version of libtuikit.
 *
 * The shared library is built with hidden visibility; only declarations
 * marked TUI_API are exported.  See libtuikit(7).
 */

#if defined(__GNUC__)
#  define TUI_API __attribute__((visibility("default")))
#else
#  define TUI_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The library's version string, e.g. "0.1.0" or "0.1.0.12". */
TUI_API const char *tui_version(void);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_API_H */
