/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-20 */
/* Public API: tui_tree, a list whose rows open and close, over the list view. */

#ifndef LIBTUIKIT_TREE_H
#define LIBTUIKIT_TREE_H

/**
 * @file tuikit/tree.h
 * @brief The tree of policies/tui-interface-model.md, "Trees".
 *
 * The program gives the rows in pre-order, each with its depth and its
 * number of children; the widget keeps which branches are open, keyed by
 * the row's identity (a string), and shows the visible rows through a
 * tui_listview.  Every branch is closed on opening.  The first two cells of
 * a row are the marker: "+ " closed with children, "- " open, two blanks a
 * leaf.  Right and 'l' open the row (on an open row: move to its first
 * child); Left and 'h' close it (on a closed row or a leaf: move to the
 * parent); Enter is not the widget's.  'j', 'k' and the paging keys move over
 * the visible rows only.  Open all and close all are calls the program puts
 * into its ':' menu.  See tui_tree_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"
#include "tuikit/listview.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One row given by the program. */
typedef struct tui_treerow {
    const char *id;      /**< the row's identity, kept across reloads */
    int         depth;   /**< 0 for the top level; at most one more than the row above */
    int         children;/**< number of children; 0: a leaf */
} tui_treerow;

/** @brief A packed list of strings (the open set); private. */
typedef struct tui_treeset {
    char  *data;
    size_t len, cap;
} tui_treeset;

/** @brief Tree state.  The list may be read and drawn with; change the tree
 *         through the functions. */
typedef struct tui_tree {
    tui_listview       list;      /**< over the visible rows: count, selected, scroll */
    const tui_treerow *rows;      /**< the program's rows (borrowed) */
    int                nrows;
    int               *visible;   /**< row index of each list item */
    int               *parent;    /**< row index of each row's parent, or -1 */
    tui_treeset        open;      /**< ids of the open rows */
    tui_treeset        saved;     /**< the open set from before a filter */
    int                filtering; /**< 1 while a filter or search is shown */
    int                counts;    /**< 1: show the child count on a closed row */
} tui_tree;

/** @brief An empty tree: no rows, nothing open. */
TUI_API void tui_tree_init(tui_tree *t);

/** @brief Releases what the tree holds (not the program's rows). */
TUI_API void tui_tree_free(tui_tree *t);

/**
 * @brief Gives the tree its rows (a load or a reload).  The open state and
 *        the selection are kept by identity; the previous rows must still be
 *        valid during the call, and the new ones until the next call.
 * @return 0, or -1 with errno EINVAL (a missing id, a depth below 0 or more
 *         than one deeper than the row above, a negative count) or ENOMEM; the
 *         tree is then unchanged.
 */
TUI_API int tui_tree_set_rows(tui_tree *t, const tui_treerow *rows, int n);

/** @brief The row shown as list item @p item, or -1. */
TUI_API int tui_tree_row_at(const tui_tree *t, int item);

/** @brief The selected row, or -1 when the tree is empty. */
TUI_API int tui_tree_selected_row(const tui_tree *t);

/** @brief 1 when row @p row has children and is open. */
TUI_API int tui_tree_is_open(const tui_tree *t, int row);

/** @brief Opens (1) or closes (0) row @p row; the selection stays on its row.
 *         Returns 0, or -1 with errno ENOMEM or EINVAL (no such row). */
TUI_API int tui_tree_set_open(tui_tree *t, int row, int open);

/** @brief Opens every branch.  0, or -1 with errno ENOMEM. */
TUI_API int tui_tree_open_all(tui_tree *t);

/** @brief Closes every branch; a hidden selection moves to its nearest
 *         visible ancestor. */
TUI_API void tui_tree_close_all(tui_tree *t);

/** @brief Selects row @p row, opening the branches above it.  0, or -1 with
 *         errno ENOMEM or EINVAL. */
TUI_API int tui_tree_select_row(tui_tree *t, int row);

/**
 * @brief Shows a filter or search: @p match has one byte per row, non-zero
 *        for a row that matches.  The branches above every match are opened;
 *        what was open before is remembered, and a call with NULL clears the
 *        filter and restores it.  Another call with a new @p match replaces the
 *        earlier one.  After tui_tree_set_rows() while filtering, call it again.
 * @return 0, or -1 with errno ENOMEM.
 */
TUI_API int tui_tree_show_matches(tui_tree *t, const unsigned char *match);

/** @brief 1: a closed row with children shows " (N)" after its label. */
TUI_API void tui_tree_show_counts(tui_tree *t, int on);

/** @brief The two-cell marker of row @p row: "+ ", "- " or "  ". */
TUI_API const char *tui_tree_marker(const tui_tree *t, int row);

/**
 * @brief Applies a key: Right and 'l' open, Left and 'h' close (or move, see
 *        above), Up, Down, PgUp, PgDn, Home, End and their letters move.
 * @return 1 when the key was one of these, 0 for any other event (Enter
 *         included).
 */
TUI_API int tui_tree_handle(tui_tree *t, const tui_event *ev);

/**
 * @brief Draws the label of row @p row into @p label, which starts after the
 *        marker and the indent and is already cleared with @p base (the
 *        selected style on the selected row).  Returns the columns used (for
 *        the child count), or 0.
 */
typedef int (*tui_tree_draw_fn)(void *ctx, tui_canvas label, int row, int selected,
                                tui_style base);

/**
 * @brief Draws the visible rows into @p c: the marker at the first two cells,
 *        then two cells of indent per level, then the label.  Rows below the
 *        last are cleared with @p blank, the selected row with @p selected.
 */
TUI_API void tui_tree_render(tui_tree *t, tui_canvas c, tui_style blank, tui_style selected,
                             tui_tree_draw_fn draw, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_TREE_H */
