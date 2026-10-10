/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Péter Császár */

#ifndef LIBTUIKIT_TEXTEDIT_H
#define LIBTUIKIT_TEXTEDIT_H

/**
 * @file tuikit/textedit.h
 * @brief A multi-line text editor over a caller-owned buffer.
 *
 * tui_lineedit edits one line and turns a newline into a space; this keeps
 * every newline and tab, typed or pasted -- an issue description, a message
 * body.  It is a pure model like tui_lineedit: tui_textedit_handle() applies
 * one event, tui_textedit_render() draws the visible part on a canvas,
 * scrolled to keep the cursor in view (vertically and horizontally; lines
 * are not wrapped, so a column on the screen is a column in the text).
 *
 * Positions are byte offsets on character boundaries; columns are display
 * columns: a wide character takes two, a combining mark none, a tab reaches
 * the next multiple of eight.  (issue-manager's own text-area widget,
 * generalised; named differently, as issue-manager keeps its name.)
 * See tui_textedit_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Editor state.  The members are readable. */
typedef struct tui_textedit {
    char   *buf;        /**< the caller's buffer, NUL-terminated */
    size_t  size;       /**< its capacity, NUL included */
    size_t  len;        /**< bytes of text */
    size_t  cur;        /**< cursor, a byte offset on a character boundary */
    int     want_col;   /**< the column Up/Down aim for, -1 when none */
    int     top;        /**< first line shown */
    int     left;       /**< first column shown */
    int     rows;       /**< height it was last drawn at: a PgUp/PgDn step */
    int     truncated;  /**< the last insertion did not fit whole */
    int     dirty;      /**< the text changed since init or set_text */
} tui_textedit;

/** @brief Starts editing @p buf (capacity @p size) holding @p initial (may be
 *         NULL), the cursor at the start. */
TUI_API void tui_textedit_init(tui_textedit *te, char *buf, size_t size,
                               const char *initial);

/** @brief Replaces the text, the cursor at the start; not counted as an edit. */
TUI_API void tui_textedit_set_text(tui_textedit *te, const char *text);

/**
 * @brief Inserts @p n bytes of @p s at the cursor.  CR LF and a lone CR
 *        become a newline; tab and newline are kept; other controls are
 *        dropped; what does not fit is left out at a character boundary
 *        and @c truncated is set.
 * @return The bytes inserted.
 */
TUI_API size_t tui_textedit_insert(tui_textedit *te, const char *s, size_t n);

/**
 * @brief Applies one event.
 *
 * Characters, pastes, Enter (a newline) and Tab (a tab) are inserted.
 * Left/Right move by character; Up/Down by line and PgUp/PgDn by the height
 * last drawn, keeping the column aimed for; Home/End and Ctrl-A/Ctrl-E go to
 * the line's ends, Ctrl+Home/Ctrl+End to the text's.  Backspace and Delete
 * remove a character (a newline joins lines); Ctrl-K removes to the end of
 * the line, or the newline when already there.
 * @return 1 when the event was one of the editor's; 0 leaves it to the
 *         program (Esc, Ctrl-S, Shift-Tab, other Ctrl and Alt chords).
 */
TUI_API int tui_textedit_handle(tui_textedit *te, const tui_event *ev);

/** @brief Number of lines; a text with no newline has one. */
TUI_API int tui_textedit_lines(const tui_textedit *te);

/** @brief The cursor's line and display column, both zero-based. */
TUI_API void tui_textedit_cursor(const tui_textedit *te, int *row, int *col);

/**
 * @brief Draws the visible part into @p c, scrolled so the cursor is in
 *        view, clears the rest with @p st, and puts the terminal cursor at
 *        the cursor.
 */
TUI_API void tui_textedit_render(tui_textedit *te, tui_canvas c, tui_style st);

/**
 * @brief One line summarising @p text for a form row: its first line, cut
 *        to fit @p cols columns with "…", and " (N lines)" when there are
 *        more.
 * @return The columns the summary takes, or -1 if @p outsz is too small.
 */
TUI_API int tui_textedit_summary(const char *text, int cols, char *out, size_t outsz);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_TEXTEDIT_H */
