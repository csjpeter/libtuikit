/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-21 */
/* Public API: tui_viewdoc, the help page and the view description. */

#ifndef LIBTUIKIT_VIEWDOC_H
#define LIBTUIKIT_VIEWDOC_H

/**
 * @file tuikit/viewdoc.h
 * @brief The help page (?) and the view description (d), one rule for every program.
 *
 * A view registers once: its keys, a statement of what it is for and one
 * line for each column of its table (tui_viewdoc).  The page the key ?
 * opens lists the keys of the view, then the keys of every view, and its
 * first line reminds of d; d opens the description in a dialog of its own
 * (the statement, then a line for each column); Esc closes the description
 * and the page with it.  A helper compares the columns of a table with the
 * registered ones, for the tests of the programs.  See tui_viewdoc_open(3);
 * policies/tui-interface-model.md, "The help page and the view description".
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/dialog.h"
#include "tuikit/event.h"
#include "tuikit/table.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One column of a view's table and what it holds. */
typedef struct tui_viewdoc_col {
    const char *title;   /**< the header text of the column, as the table shows it */
    const char *text;    /**< one line: what the column holds */
} tui_viewdoc_col;

/** @brief What a program registers for a view.  Nothing is copied. */
typedef struct tui_viewdoc {
    const char *name;                /**< "Work": the dialog is titled "Work view" */
    const char *statement;           /**< what the view is for (a view that is no
                                          table says what it shows and what each part means) */
    const tui_viewdoc_col *cols;     /**< one entry for each column of the table; NULL for none */
    int ncols;
    const char *const *keys;         /**< the keys of this view: key, description, key, ... */
    int nkeys;                       /**< pairs */
} tui_viewdoc;

/** @brief An open help page.  Read @c state; the rest is private to the widget. */
typedef struct tui_viewdoc_page {
    tui_dialog_state    state;       /**< OPEN while the page or the description is up */
    const tui_viewdoc  *view;
    tui_dialog          help;        /* the page */
    tui_dialog          desc;        /* the description, when @c in_desc */
    int                 in_desc;
    const char        **pairs;       /* the rows of the page (own array, strings not copied) */
    void               *reserved[2];
} tui_viewdoc_page;

/**
 * @brief Opens the help page of @p view: first a line that reminds of d, then
 *        the keys of the view, then the @p ncommon keys of every view
 *        (@p common holds key and description in pairs; NULL for none).
 * @return 0, or -1 with errno (EINVAL, ENOMEM).
 */
TUI_API int tui_viewdoc_open(tui_viewdoc_page *p, const tui_viewdoc *view,
                             const char *const *common, int ncommon);

/**
 * @brief Gives the page one event: d (outside the search of the page) opens the
 *        description, Esc, q or Backspace close the page, or the description
 *        and the page with it; the rest is the help dialog's (scroll, search).
 * @return The state afterwards: TUI_DIALOG_OPEN until it closed (TUI_DIALOG_OK).
 */
TUI_API tui_dialog_state tui_viewdoc_handle(tui_viewdoc_page *p, const tui_event *ev);

/** @brief Draws the page, or the description over it, centred in @p c. */
TUI_API void tui_viewdoc_render(tui_viewdoc_page *p, tui_canvas c);

/** @brief Frees the page.  Safe after a failed open. */
TUI_API void tui_viewdoc_free(tui_viewdoc_page *p);

/**
 * @brief The text of the description: the statement, a blank line, then one
 *        line "TITLE  text" for each column, the titles padded to the widest.
 * @return the length written (as snprintf), or -1 with errno (EINVAL).
 */
TUI_API int tui_viewdoc_text(const tui_viewdoc *view, char *out, size_t size);

/**
 * @brief Compares the column titles of @p cols (a table's, @p ncols of them) with
 *        the registered columns of @p view, in order.  A column without a title
 *        (a mark column) is left out of the comparison.
 * @param msg  where to say the first difference (may be NULL)
 * @return 0 when they agree; -1 when a column is not described, or a description
 *         names a column that is gone.
 */
TUI_API int tui_viewdoc_check(const tui_viewdoc *view, const tui_table_col *cols, int ncols,
                              char *msg, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_VIEWDOC_H */
