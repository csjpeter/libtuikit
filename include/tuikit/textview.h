/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Public API: tui_textview, a scrolling read-only text. */

#ifndef LIBTUIKIT_TEXTVIEW_H
#define LIBTUIKIT_TEXTVIEW_H

/**
 * @file tuikit/textview.h
 * @brief Word-wrapped, scrollable text: a message, a mail body, a log.
 *
 * The view keeps its own copy of the text and wraps it to the width it is
 * drawn at.  It can follow the end of a growing text (a chat history, a
 * streamed answer) with tui_textview_append() while the user is at the
 * end, without moving under a user who scrolled up.  See
 * tui_textview_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Text view state.  Caller-allocated; the members are private. */
typedef struct tui_textview {
    char   *text;
    size_t  len;
    int     width;          /* the width the lines were wrapped at */
    size_t *starts;         /* byte offset of each wrapped line */
    size_t *lens;           /* its length in bytes */
    int     nlines, cap;
    int     top;            /* first line shown */
    int     rows;           /* height it was last drawn at */
    int     dirty;          /* text changed since wrapping */
    int     follow;         /* keep the last line in view at the next wrap */
} tui_textview;

/** @brief An empty view. */
TUI_API void tui_textview_init(tui_textview *tv);

/** @brief Frees the text and the line index. */
TUI_API void tui_textview_free(tui_textview *tv);

/**
 * @brief Replaces the text (copied) and scrolls to the top.
 * @return 0, or -1 with errno ENOMEM (the old text stays).
 */
TUI_API int tui_textview_set_text(tui_textview *tv, const char *text);

/**
 * @brief Appends @p n bytes.  A view showing the end keeps showing it;
 *        one scrolled elsewhere stays where it is.
 */
TUI_API int tui_textview_append(tui_textview *tv, const char *text, size_t n);

/**
 * @brief Wraps the text to @p cols columns (done by render as needed).
 * @return The number of lines, or -1 with errno ENOMEM.
 */
TUI_API int tui_textview_wrap(tui_textview *tv, int cols);

/** @brief Number of wrapped lines at the last wrap width. */
TUI_API int tui_textview_lines(const tui_textview *tv);

/** @brief Scrolls by @p delta lines (negative: up), within the text. */
TUI_API void tui_textview_scroll(tui_textview *tv, int delta);

/** @brief Scrolls so that line @p line is at the top (clamped). */
TUI_API void tui_textview_scroll_to(tui_textview *tv, int line);

/** @brief Scrolls to show the last line at the bottom. */
TUI_API void tui_textview_scroll_end(tui_textview *tv);

/** @brief 1 when the last line is shown. */
TUI_API int tui_textview_at_end(const tui_textview *tv);

/** @brief Line at the top of the view. */
TUI_API int tui_textview_top(const tui_textview *tv);

/**
 * @brief Applies a scrolling key: Up, Down, PgUp, PgDn, Home, End.
 * @return 1 when the key was one of these, 0 otherwise.
 */
TUI_API int tui_textview_handle(tui_textview *tv, const tui_event *ev);

/**
 * @brief Draws the visible lines into @p c, wrapping to its width first if
 *        needed, and clears the rest of it with @p st.
 *
 * Lines break at spaces where they can and inside a word where they must;
 * a wide character is never split.  A tab advances to the next multiple
 * of eight columns.  CR before LF is dropped; other controls are drawn as
 * U+00B7.
 */
TUI_API void tui_textview_render(tui_textview *tv, tui_canvas c, tui_style st);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_TEXTVIEW_H */
