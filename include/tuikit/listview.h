/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Public API: tui_listview, a scrolling list with a selection. */

#ifndef LIBTUIKIT_LISTVIEW_H
#define LIBTUIKIT_LISTVIEW_H

/**
 * @file tuikit/listview.h
 * @brief Scrolling and selection for a vertical list of rows.
 *
 * The list view knows how many items there are, which one is selected and
 * which one is at the top of the viewport -- nothing about the items
 * themselves.  The program draws each visible row through a callback, so
 * the same view serves a dialog list, a message list or a menu.  (tg-cli's
 * ListView, with the same operations.)  See tui_listview_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief List state.  The fields may be read; change them through the
 *         functions, which keep them consistent. */
typedef struct tui_listview {
    int count;         /**< number of items */
    int selected;      /**< 0 .. count-1, or -1 when the list is empty */
    int scroll_top;    /**< index of the item in the top row */
    int rows_visible;  /**< height of the viewport */
} tui_listview;

/** @brief An empty list: no selection, scrolled to the top. */
TUI_API void tui_listview_init(tui_listview *lv);

/** @brief Sets the number of items; keeps the selection in range (the
 *         first item becomes selected when the list stops being empty). */
TUI_API void tui_listview_set_count(tui_listview *lv, int count);

/** @brief Sets the viewport height and brings the selection into view. */
TUI_API void tui_listview_set_viewport(tui_listview *lv, int rows);

/** @brief Selects item @p index (clamped) and brings it into view. */
TUI_API void tui_listview_select(tui_listview *lv, int index);

TUI_API void tui_listview_move_up(tui_listview *lv);    /**< Up: one item */
TUI_API void tui_listview_move_down(tui_listview *lv);  /**< Down: one item */
TUI_API void tui_listview_page_up(tui_listview *lv);    /**< PgUp: one viewport */
TUI_API void tui_listview_page_down(tui_listview *lv);  /**< PgDn: one viewport */
TUI_API void tui_listview_home(tui_listview *lv);       /**< Home: first item */
TUI_API void tui_listview_end(tui_listview *lv);        /**< End: last item */

/** @brief Scrolls the least needed to show the selection; also keeps the
 *         viewport from showing empty rows below the last item when items
 *         above are hidden. */
TUI_API void tui_listview_reveal_selected(tui_listview *lv);

/** @brief 1 when item @p index is in the viewport. */
TUI_API int tui_listview_is_visible(const tui_listview *lv, int index);

/**
 * @brief Applies a navigation key: Up, Down, PgUp, PgDn, Home, End.
 * @return 1 when the key was one of these (the state may be unchanged at
 *         an edge), 0 for any other event.  Mapping other keys (j/k, g/G)
 *         is the program's choice.
 */
TUI_API int tui_listview_handle(tui_listview *lv, const tui_event *ev);

/**
 * @brief Draws one row: @p row is a one-row canvas, already cleared;
 *        @p index is the item; @p selected says whether it is the
 *        selected one.
 */
typedef void (*tui_listview_draw_fn)(void *ctx, tui_canvas row, int index, int selected);

/**
 * @brief Draws the visible rows into @p c.  Sets the viewport to the
 *        canvas height first, so the state always matches what is shown.
 *        Rows below the last item are cleared with @p blank.
 */
TUI_API void tui_listview_render(tui_listview *lv, tui_canvas c, tui_style blank,
                                 tui_listview_draw_fn draw, void *ctx);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_LISTVIEW_H */
