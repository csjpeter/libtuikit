/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Public API: rectangles and clipped canvases for drawing into a screen. */

#ifndef LIBTUIKIT_CANVAS_H
#define LIBTUIKIT_CANVAS_H

/**
 * @file tuikit/canvas.h
 * @brief Rectangles, layout, and drawing clipped to a rectangle.
 *
 * A canvas is a rectangle of a screen with its own coordinates: (0, 0) is
 * its top-left cell, and nothing drawn through it lands outside it.  A pane,
 * a list row or a dialog is a canvas; a widget given one cannot draw over
 * its neighbours.  See tui_canvas_of(3).
 */

#include "tuikit/api.h"
#include "tuikit/screen.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A rectangle: top-left cell and size. */
typedef struct tui_rect {
    int row, col;
    int rows, cols;
} tui_rect;

/** @brief A rectangle from its parts. */
static inline tui_rect tui_rect_make(int row, int col, int rows, int cols) {
    tui_rect r;
    r.row = row;
    r.col = col;
    r.rows = rows;
    r.cols = cols;
    return r;
}

/** @brief True when the rectangle has no cell. */
static inline int tui_rect_is_empty(tui_rect r) {
    return r.rows <= 0 || r.cols <= 0;
}

/**
 * @brief Cuts @p r into a left part of @p left_cols columns and the rest.
 *        A negative @p left_cols counts from the right edge: -20 leaves 20
 *        columns on the right.  Either output may be NULL.
 */
TUI_API void tui_rect_split_h(tui_rect r, int left_cols, tui_rect *left, tui_rect *right);

/** @brief Cuts @p r into a top part of @p top_rows rows and the rest;
 *         negative counts from the bottom. */
TUI_API void tui_rect_split_v(tui_rect r, int top_rows, tui_rect *top, tui_rect *bottom);

/** @brief @p r shrunk by @p n cells on every side (never below empty). */
TUI_API tui_rect tui_rect_inset(tui_rect r, int n);

/** @brief A rectangle of at most @p rows x @p cols centred in @p r. */
TUI_API tui_rect tui_rect_center(tui_rect r, int rows, int cols);

/** @brief The cells common to @p a and @p b (empty when none). */
TUI_API tui_rect tui_rect_intersect(tui_rect a, tui_rect b);

/** @brief Horizontal alignment. */
typedef enum tui_align {
    TUI_ALIGN_LEFT = 0,
    TUI_ALIGN_CENTER,
    TUI_ALIGN_RIGHT
} tui_align;

/** @brief A rectangle of a screen.  @c clip is in screen coordinates. */
typedef struct tui_canvas {
    tui_screen *screen;
    tui_rect    clip;
} tui_canvas;

/** @brief The whole screen. */
TUI_API tui_canvas tui_canvas_of(tui_screen *s);

/**
 * @brief The part @p r of @p c (in @p c's coordinates), clipped to @p c.
 *        When @p r reaches past the top or left of @p c, the sub-canvas
 *        starts at its first visible cell.
 */
TUI_API tui_canvas tui_canvas_sub(tui_canvas c, tui_rect r);

/** @brief Rows of the canvas. */
static inline int tui_canvas_rows(tui_canvas c) { return c.clip.rows > 0 ? c.clip.rows : 0; }

/** @brief Columns of the canvas. */
static inline int tui_canvas_cols(tui_canvas c) { return c.clip.cols > 0 ? c.clip.cols : 0; }

/** @brief The canvas as a rectangle in its own coordinates. */
static inline tui_rect tui_canvas_rect(tui_canvas c) {
    return tui_rect_make(0, 0, tui_canvas_rows(c), tui_canvas_cols(c));
}

/**
 * @brief Writes text at (@p row, @p col), clipped to the canvas.  A
 *        negative @p col starts the text left of the canvas: the columns
 *        before 0 are not drawn (horizontal scrolling).
 * @return The columns written inside the canvas.
 */
TUI_API int tui_canvas_put(tui_canvas c, int row, int col, const char *utf8, tui_style st);

/**
 * @brief Writes text on @p row aligned in the canvas width, shortened with
 *        "…" when it does not fit.
 * @return The column it starts at.
 */
TUI_API int tui_canvas_put_aligned(tui_canvas c, int row, const char *utf8,
                                   tui_align align, tui_style st);

/** @brief Fills @p r (canvas coordinates) with blanks in @p st. */
TUI_API void tui_canvas_fill(tui_canvas c, tui_rect r, tui_style st);

/** @brief Fills the whole canvas with blanks in @p st. */
TUI_API void tui_canvas_clear(tui_canvas c, tui_style st);

/** @brief A horizontal line of @p n cells of U+2500 from (@p row, @p col). */
TUI_API void tui_canvas_hline(tui_canvas c, int row, int col, int n, tui_style st);

/** @brief A vertical line of @p n cells of U+2502 from (@p row, @p col). */
TUI_API void tui_canvas_vline(tui_canvas c, int row, int col, int n, tui_style st);

/**
 * @brief A box border around @p r with the light box-drawing characters
 *        (U+250C ... U+2518), and @p title (may be NULL) in its top edge,
 *        shortened to fit.  The inside is not touched; tui_rect_inset(r, 1)
 *        is the inside.
 */
TUI_API void tui_canvas_box(tui_canvas c, tui_rect r, const char *title, tui_style st);

/**
 * @brief Puts the terminal cursor at (@p row, @p col) of the canvas after
 *        the next render; outside the canvas it is hidden.
 */
TUI_API void tui_canvas_set_cursor(tui_canvas c, int row, int col, int visible);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_CANVAS_H */
