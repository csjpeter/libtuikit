/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-13 */
/* Public API: tui_table, columns, a header and a selection the widget owns. */

#ifndef LIBTUIKIT_TABLE_H
#define LIBTUIKIT_TABLE_H

/**
 * @file tuikit/table.h
 * @brief A table: column definitions, a header and the selection.
 *
 * The program supplies the number of rows, the column definitions and a
 * callback that gives the text of a cell; the widget lays the columns out,
 * cuts the text by display width, draws the header and the selection and
 * scrolls.  The selection (a dark blue row background, the cells' own
 * colours kept, a ">" in the first column) is drawn in this one place;
 * a program never paints a row.  Navigation is tui_listview's.  The set
 * of marked rows (a "*" beside the ">") is the widget's too.
 * See tui_table_init(3).
 */

#include <stdint.h>

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"
#include "tuikit/listview.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Column flags. */
enum {
    TUI_COL_CUT_START = 1  /**< a cell too long loses its beginning ("…tail") */
};

/** @brief A column.  The columns are separated by one blank cell. */
typedef struct tui_table_col {
    const char *title;   /**< header text; NULL for none */
    int         width;   /**< cells; the minimum when @c weight > 0 */
    int         weight;  /**< 0: fixed; > 0: shares the remaining width */
    tui_align   align;   /**< TUI_ALIGN_LEFT / CENTER / RIGHT */
    int         hide_below; /**< hidden when the table is narrower; 0: never */
    int         sort;    /**< header marker: 1 "▲", -1 "▼", 0 none */
    unsigned    flags;   /**< TUI_COL_* */
} tui_table_col;

/** @brief Row kinds. */
typedef enum tui_row_kind {
    TUI_ROW_DATA = 0,    /**< cells, selectable */
    TUI_ROW_SECTION      /**< a heading or a rule across every column, never selected */
} tui_row_kind;

/** @brief The row being described (opaque; valid in the row callback). */
typedef struct tui_table_row tui_table_row;
/** @brief The cell being filled (opaque; valid in the cell callback). */
typedef struct tui_table_cell tui_table_cell;

/** @brief Row kind of the row being described.  Default: TUI_ROW_DATA. */
TUI_API void tui_table_row_kind(tui_table_row *r, tui_row_kind kind);

/** @brief A style under every cell of the row (a colour for the whole
 *         row, dim for an empty one); a cell's own style goes on top. */
TUI_API void tui_table_row_style(tui_table_row *r, tui_style st);

/** @brief One cell shown beside the ">" (a mark, a pending action); it
 *         stays visible when the row is selected.  Only the first
 *         character is used. */
TUI_API void tui_table_row_mark(tui_table_row *r, const char *glyph, tui_style st);

/** @brief The text of a TUI_ROW_SECTION row; the rest of the row is a
 *         horizontal rule.  NULL or "" gives the rule alone. */
TUI_API void tui_table_row_text(tui_table_row *r, const char *text);

/** @brief Text of the cell that is never cut: tree glyphs, an indent.  Call
 *         it before tui_table_cell_put(); the cell is cut after it. */
TUI_API void tui_table_cell_prefix(tui_table_cell *c, const char *utf8, tui_style st);

/** @brief Appends text to the cell in @p st.  A cell is any number of
 *         spans, each in its own style (a status column of single
 *         glyphs).  Beyond about a kilobyte of text, or 24 spans, the
 *         rest is dropped. */
TUI_API void tui_table_cell_put(tui_table_cell *c, const char *utf8, tui_style st);

/** @brief The program's rows. */
typedef struct tui_table_model {
    /** Describes row @p index; NULL when all rows are plain data rows.  It
     *  is also called to find out where the selection may go, so it must
     *  have no effect of its own. */
    void (*row)(void *ctx, int index, tui_table_row *r);
    /** Fills the cell of column @p col of data row @p index. */
    void (*cell)(void *ctx, int index, int col, tui_table_cell *c);
    void *ctx;
} tui_table_model;

/** @brief Table flags. */
enum {
    TUI_TABLE_RULER = 1,     /**< a row of "═" under the header */
    TUI_TABLE_NO_HEADER = 2, /**< no header, whatever the titles */
    TUI_TABLE_MARKS = 4      /**< Space in tui_table_handle() toggles the mark of
                                  the cursor row and moves down */
};

/** @brief Table state.  The fields may be read; change them through the
 *         functions or, for the styles and flags, directly. */
typedef struct tui_table {
    tui_listview          lv;       /**< count, selected, scroll_top, rows_visible */
    int                   total;    /**< rows the program has in all (>= count) */
    const tui_table_col  *cols;
    int                   ncols;
    tui_table_model       model;
    unsigned              flags;    /**< TUI_TABLE_* */
    int                   colors;   /**< 8, 16 or 256 (tui_table_set_colors) */
    tui_style             header;   /**< header row; default bold */
    tui_style             selection;/**< the cursor row's base (see tui_table_set_colors) */
    const char           *message;  /**< shown instead of the rows when set */
    tui_style             message_style;
    void                 *reserved[4];
} tui_table;

/**
 * @brief A table of @p ncols columns (not copied: they must outlive the
 *        table) over @p model.  The selection is the one @p colors-aware
 *        style of tui_table_set_colors(), from $TERM and $COLORTERM.
 */
TUI_API void tui_table_init(tui_table *t, const tui_table_col *cols, int ncols,
                            const tui_table_model *model);

/**
 * @brief Sets the colour depth the selection is drawn for: 256 (dark blue
 *        background 17), 16 (blue) or 8 (no background: the ">" and bold
 *        carry the selection).  Other values give 8.
 */
TUI_API void tui_table_set_colors(tui_table *t, int colors);

/** @brief The number of rows; keeps the selection in range. */
TUI_API void tui_table_set_count(tui_table *t, int count);

/** @brief The rows the program has in all, for "n of N"; at least the count. */
TUI_API void tui_table_set_total(tui_table *t, int total);

/** @brief Shows @p message (not copied; NULL clears it) in place of the
 *         header and the rows: an empty list's text, or a read error. */
TUI_API void tui_table_set_message(tui_table *t, const char *message, tui_style st);

/** @brief Selects row @p index (clamped); a section row is passed over
 *         to the nearest data row in the direction of the move (down
 *         from a row above it, up from one below), else the other way;
 *         with none, the selection stays. */
TUI_API void tui_table_select(tui_table *t, int index);

/** @brief The selected row, or -1. */
TUI_API int tui_table_selected(const tui_table *t);

/** @brief The first row shown, the last one shown (-1 for none): what a
 *         title bar's "a-b of N" needs. */
TUI_API int tui_table_first_visible(const tui_table *t);
TUI_API int tui_table_last_visible(const tui_table *t);

/** @brief Applies Up, Down, PgUp, PgDn, Home, End (and j, k, g, G,
 *         Ctrl-F, Ctrl-B) as tui_listview_handle(); section rows are
 *         skipped.  With TUI_TABLE_MARKS, Space toggles the mark of the
 *         cursor row and moves down.  1 when the key was handled. */
TUI_API int tui_table_handle(tui_table *t, const tui_event *ev);

/**
 * @brief Gives the table a stable identity for each row: @p id returns a
 *        number that is the same for the same row after a reload, a sort
 *        or a filter (a mail's UID, an issue's number).  The marks are
 *        kept by it.  Without it the row index is the identity, and the
 *        marks past a smaller count are dropped by tui_table_set_count().
 *        Changing the callback clears the marks.
 */
TUI_API void tui_table_set_row_id(tui_table *t, uint64_t (*id)(void *ctx, int index),
                                  void *ctx);

/** @brief 1 when row @p index is marked.  A section row never is. */
TUI_API int tui_table_marked(const tui_table *t, int index);

/** @brief The size of the set of marks.  With a row id callback it
 *         includes marked rows the list does not hold at the moment (a
 *         filter hides them, the marks stay). */
TUI_API int tui_table_marked_count(const tui_table *t);

/** @brief Marks (@p on 1) or unmarks row @p index.  A section row is left
 *         alone.  @return 0, or -1 with errno EINVAL (no such row) or
 *         ENOMEM. */
TUI_API int tui_table_set_marked(tui_table *t, int index, int on);

/** @brief Toggles the mark of row @p index; the return as
 *         tui_table_set_marked(). */
TUI_API int tui_table_toggle_mark(tui_table *t, int index);

/** @brief Marks every data row of the list.  @return 0, or -1 (ENOMEM). */
TUI_API int tui_table_mark_all(tui_table *t);

/** @brief Marks the unmarked data rows and unmarks the marked ones; marks
 *         of rows the list does not hold are kept.  @return 0, or -1. */
TUI_API int tui_table_invert_marks(tui_table *t);

/** @brief Drops every mark. */
TUI_API void tui_table_clear_marks(tui_table *t);

/** @brief The index of the first marked row after @p after (-1 to start),
 *         or -1: @code for (i = tui_table_next_marked(t, -1); i >= 0;
 *         i = tui_table_next_marked(t, i)) @endcode */
TUI_API int tui_table_next_marked(const tui_table *t, int after);

/** @brief Releases what the table holds (the marks).  Safe on a table
 *         that never had any; the table can be initialised again. */
TUI_API void tui_table_free(tui_table *t);

/** @brief The widths the columns get on a table @p cols cells wide:
 *         @p widths[i] is 0 for a hidden column.  @p widths has
 *         tui_table.ncols entries.  @return the columns shown. */
TUI_API int tui_table_widths(const tui_table *t, int cols, int *widths);

/**
 * @brief Draws the header and the visible rows into @p c; the viewport
 *        becomes the canvas height less the header.  Rows below the last
 *        are cleared with @p blank.
 */
TUI_API void tui_table_render(tui_table *t, tui_canvas c, tui_style blank);

/** @brief Draws only the header row (and the ruler). */
TUI_API void tui_table_render_header(tui_table *t, tui_canvas c);

/** @brief Draws only row @p index at its place in @p c (no header, no
 *         other row), after a cursor move or a changed row.
 *  @return 1 when it was drawn, 0 when it is not visible. */
TUI_API int tui_table_render_row(tui_table *t, tui_canvas c, int index);

/**
 * @brief The text of row @p index as plain text for output that is not a
 *        screen, with the same columns and cuts.  @p cols <= 0: no cut and
 *        no padding.  A section row gives its text.
 * @return The bytes written (without the NUL), or -1 when @p outsz was
 *         too small (the text is cut, terminated).
 */
TUI_API int tui_table_format_row(const tui_table *t, int cols, int index,
                                 char *out, size_t outsz);

/** @brief The header as tui_table_format_row() does a row. */
TUI_API int tui_table_format_header(const tui_table *t, int cols, char *out, size_t outsz);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_TABLE_H */
