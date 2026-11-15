/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* Public API: tui_sgrview, a scrolling text view of text that carries SGR styles. */

#ifndef LIBTUIKIT_SGRVIEW_H
#define LIBTUIKIT_SGRVIEW_H

/**
 * @file tuikit/sgrview.h
 * @brief Word-wrapped, scrollable text that carries ANSI SGR sequences:
 *        a mail body with bold, italic, underline, strike-through and
 *        colour (16, 256 and 24-bit).
 *
 * Like tui_textview, but the escape sequences @c ESC [ ... m in the text
 * are styles, not characters: they take no column, the style in force at
 * the start of every wrapped line is remembered, so a styled run keeps its
 * style across a wrap and a scroll, and the cells are drawn through
 * tui_canvas (tui_style).  Other CSI sequences are dropped.  See
 * tui_sgrview_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief One wrapped line of the view (private). */
typedef struct tui_sgrline {
    size_t    start, len;   /* in the text */
    tui_style style;        /* the SGR state at its start (default colours = none) */
} tui_sgrline;

/** @brief SGR text view state.  Caller-allocated; the members are private. */
typedef struct tui_sgrview {
    char        *text;
    size_t       len;
    tui_sgrline *lines;
    int          nlines, cap;
    int          width;     /* the width the lines were wrapped at */
    int          top;       /* first line shown */
    int          rows;      /* height it was last drawn at */
    int          dirty;     /* text changed since wrapping */
} tui_sgrview;

/** @brief An empty view. */
TUI_API void tui_sgrview_init(tui_sgrview *sv);

/** @brief Frees the text and the line index. */
TUI_API void tui_sgrview_free(tui_sgrview *sv);

/**
 * @brief Replaces the text (copied) and scrolls to the top.
 * @return 0, or -1 with errno ENOMEM (the old text stays) or EINVAL.
 */
TUI_API int tui_sgrview_set_text(tui_sgrview *sv, const char *text);

/** @brief Number of wrapped lines at the last render width. */
TUI_API int tui_sgrview_lines(const tui_sgrview *sv);

/** @brief Line at the top of the view. */
TUI_API int tui_sgrview_top(const tui_sgrview *sv);

/** @brief Scrolls by @p delta lines (negative: up), within the text. */
TUI_API void tui_sgrview_scroll(tui_sgrview *sv, int delta);

/** @brief Scrolls so that line @p line is at the top (clamped). */
TUI_API void tui_sgrview_scroll_to(tui_sgrview *sv, int line);

/** @brief Scrolls to show the last line at the bottom. */
TUI_API void tui_sgrview_scroll_end(tui_sgrview *sv);

/**
 * @brief Applies a scrolling key: Up, Down, PgUp, PgDn, Home, End and the
 *        shared letters j, k, g, G, ctrl-f, ctrl-b.
 * @return 1 when the key was one of these, 0 otherwise.
 */
TUI_API int tui_sgrview_handle(tui_sgrview *sv, const tui_event *ev);

/**
 * @brief Draws the visible lines into @p c, wrapping to its width first if
 *        needed, and clears the rest of it with @p st.
 *
 * The styles of the text are laid over @p st: a colour the text sets
 * replaces the one of @p st, a colour it leaves (default) keeps it, the
 * attributes add to its attributes.  SGR 1 bold, 2 dim, 3 italic,
 * 4 underline, 5/6 blink, 7 reverse, 9 strike, their offs (22-29), the
 * colours 30-37, 90-97, 40-47, 100-107, 38/48 with 5;n or 2;r;g;b (also
 * with ':'), 39, 49 and 0 are understood; others are ignored.  Lines break
 * at spaces where they can and inside a word where they must; a wide
 * character is never split; a tab advances to the next multiple of eight
 * columns; other control characters are drawn as U+00B7.  A scroll bar
 * takes the last column while the text is longer than the area.
 */
TUI_API void tui_sgrview_render(tui_sgrview *sv, tui_canvas c, tui_style st);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_SGRVIEW_H */
