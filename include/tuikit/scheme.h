/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-22 */
/* Public API: colour schemes, the roles' styles read from NAME.scheme files. */

#ifndef LIBTUIKIT_SCHEME_H
#define LIBTUIKIT_SCHEME_H

/**
 * @file tuikit/scheme.h
 * @brief The style of every role (policies/tui-interface-model.md, "Colour
 *        schemes"), from a scheme file the user can change.
 *
 * A scheme is a file NAME.scheme of lines `key = fg bg attr,attr`.  It is
 * searched in $XDG_CONFIG_HOME/tuikit/schemes/ (default
 * ~/.config/tuikit/schemes/) and then in /usr/share/libtuikit/schemes/; the
 * name is set by `scheme = NAME` in ~/.config/tuikit/tuikit.conf or
 * /etc/tuikit/tuikit.conf, and without one the built-in palette is used.
 * Every problem is reported and harmless: the role keeps its built-in
 * style.  The palette macros of screen.h keep their values.  See
 * tui_scheme_load(3).
 */

#include "tuikit/api.h"
#include "tuikit/screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The roles; the key in a scheme file is tui_role_name(). */
typedef enum tui_role {
    TUI_ROLE_TEXT,
    TUI_ROLE_HEADER,
    TUI_ROLE_STATUS,
    TUI_ROLE_SELECTION,
    TUI_ROLE_CURSOR,
    TUI_ROLE_ACCENT,
    TUI_ROLE_FRAME,
    TUI_ROLE_INPUT,
    TUI_ROLE_INPUT_FOCUS,
    TUI_ROLE_TAB_ACTIVE,
    TUI_ROLE_TAB_INACTIVE,
    TUI_ROLE_OK,
    TUI_ROLE_ERROR,
    TUI_ROLE_HINT,
    TUI_ROLE_PROGRESS,
    TUI_ROLE_COUNT
} tui_role;

/** @brief A style for every role. */
typedef struct tui_scheme {
    tui_style styles[TUI_ROLE_COUNT];
} tui_scheme;

/** @brief What a load found: the number of problems and the first one,
 *         as "origin:line: what". */
typedef struct tui_scheme_report {
    int  problems;
    char first[192];
} tui_scheme_report;

/** @brief The key of a role ("input.focus"), or NULL for a bad value. */
TUI_API const char *tui_role_name(tui_role role);

/** @brief The role of a key, or -1 when there is none. */
TUI_API int tui_role_from_name(const char *name);

/** @brief The built-in style of a role (what default.scheme holds). */
TUI_API tui_style tui_scheme_builtin(tui_role role);

/** @brief Sets every role of @p sc to its built-in style. */
TUI_API void tui_scheme_defaults(tui_scheme *sc);

/**
 * @brief Applies the lines of @p text to @p sc.  A malformed line or key
 *        leaves its role as it was.
 * @param origin Names the text in the report (a file name).
 * @param rep    Receives the problems (it is reset first); may be NULL.
 * @return The number of problems found in this text.
 */
TUI_API int tui_scheme_parse(tui_scheme *sc, const char *text, const char *origin,
                             tui_scheme_report *rep);

/**
 * @brief Fills @p sc with the built-in styles and applies the scheme file
 *        @p name over them, searched as the header says.  With @p name NULL
 *        the name comes from tuikit.conf, and none there is the built-in
 *        palette.  A scheme named "default" that is not installed is the
 *        built-in palette and no problem.
 * @param rep Receives the problems; may be NULL.
 * @return The number of problems; @p sc is usable whatever it is.
 */
TUI_API int tui_scheme_load(tui_scheme *sc, const char *name, tui_scheme_report *rep);

/**
 * @brief Loads the active scheme, the one tuikit.conf selects.  Done by
 *        itself at the first query; call it again to reload.
 * @return The number of problems.
 */
TUI_API int tui_scheme_init(void);

/** @brief The style of a role in the active scheme. */
TUI_API tui_style tui_scheme_style(tui_role role);

/** @brief The first problem of the active scheme's load, or NULL: a
 *         program shows it once. */
TUI_API const char *tui_scheme_diagnostic(void);

/**
 * @brief The colour nearest to @p c that a terminal of @p colors colours
 *        (256, 16 or 8) shows; the default colour stays.
 */
TUI_API tui_color tui_color_reduce(tui_color c, int colors);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_SCHEME_H */
