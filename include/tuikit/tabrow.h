/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-24 */
/* Public API: tui_tabrow, the row of sub-view tabs of a screen, the active one by attribute. */

#ifndef LIBTUIKIT_TABROW_H
#define LIBTUIKIT_TABROW_H

/**
 * @file tuikit/tabrow.h
 * @brief The tab row: labels on one row, the active tab bold and
 *        underlined, scrolled so that it is always visible.
 *
 * Each tab is its label between two blanks, one blank between tabs.  The
 * active tab is drawn in the role "tab.active" plus bold and underline, the
 * others in "tab.inactive", so that the active one is told apart without
 * colour (policies/tui-interface-model.md, "Navigation").  When the row is
 * wider than the canvas, it scrolls: the active tab is always whole on the
 * row (cut with an ellipsis only when it alone is wider), and the columns at
 * both ends show a guillemet (the left one when tabs are hidden before the
 * window, the right one when tabs are hidden after it).  See tui_tabrow_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Most tabs a row holds. */
#define TUI_TABROW_MAX 64

/** @brief What tui_tabrow_handle() did with an event. */
typedef enum tui_tabrow_result {
    TUI_TABROW_IGNORED = 0,   /**< not for the row; the program uses it */
    TUI_TABROW_CHANGED        /**< the active tab moved: show its view */
} tui_tabrow_result;

/** @brief The row.  Plain data: initialise it with tui_tabrow_init(). */
typedef struct tui_tabrow {
    const char *const *labels;   /* private: not copied */
    int                n;
    int                active;
    int                first;    /* private: the first tab of the window */
} tui_tabrow;

/** @brief An empty row. */
TUI_API void tui_tabrow_init(tui_tabrow *tr);

/**
 * @brief The labels and the active index.  The array and its strings are
 *        not copied: they must stay valid while set.  @p active is clamped
 *        to the range.  Controls in a label are drawn as blanks.
 * @return 0, or -1 with errno EINVAL (more than TUI_TABROW_MAX tabs, or a
 *         NULL row or label).
 */
TUI_API int tui_tabrow_set(tui_tabrow *tr, const char *const *labels, int n, int active);

/** @brief Makes tab @p i the active one (clamped to the range). */
TUI_API void tui_tabrow_set_active(tui_tabrow *tr, int i);

/** @brief The active index; 0 for an empty row. */
TUI_API int tui_tabrow_active(const tui_tabrow *tr);

/** @brief The number of tabs. */
TUI_API int tui_tabrow_count(const tui_tabrow *tr);

/**
 * @brief Tab cycles to the next tab and Shift-Tab to the previous (both
 *        wrap); the digits 1 to 9 jump to that tab when it exists.
 *        Anything else is IGNORED.
 */
TUI_API tui_tabrow_result tui_tabrow_handle(tui_tabrow *tr, const tui_event *ev);

/** @brief Draws the row on the first row of @p c, over its whole width. */
TUI_API void tui_tabrow_render(tui_tabrow *tr, tui_canvas c);

/**
 * @brief The row as plain text for @p cols columns, as render() lays it
 *        out (for a CLI or a test); trailing blanks are not written.
 * @return The bytes written without the NUL, or -1 when @p outsz was too
 *         small (the text is cut, terminated).
 */
TUI_API int tui_tabrow_format(const tui_tabrow *tr, int cols, char *out, size_t outsz);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_TABROW_H */
