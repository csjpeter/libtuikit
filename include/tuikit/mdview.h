/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-11 */
/* Public API: tui_mdview, a scrolling Markdown text view, and the renderer behind it. */

#ifndef LIBTUIKIT_MDVIEW_H
#define LIBTUIKIT_MDVIEW_H

/**
 * @file tuikit/mdview.h
 * @brief Markdown shown readably: headings, lists, tables, code, links,
 *        reflowed paragraphs, scrolling.
 *
 * The text of a ticket (its description, its comments) and any other
 * Markdown is shown through this view, never raw.  The view renders to a
 * canvas, scrolls with the keys of the other views, and has a plain
 * fallback (no colour, no attribute) for a terminal that has none.  The
 * three tui_md_* functions give the same rendering as text, for a program
 * that prints (an export, a pipe).  See tui_mdview_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/buf.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @name Markers of the styled form
 * The marked text is the plain rendering with zero-width marker bytes in
 * it: an opener (@c TUI_MD_BOLD, @c TUI_MD_ITALIC, @c TUI_MD_CODE,
 * @c TUI_MD_HEAD for a heading, @c TUI_MD_SUM for the summary) is closed by
 * one @c TUI_MD_OFF.  A span that wraps is closed at the end of the line
 * and opened again at the start of the next by the reader.
 * @{
 */
#define TUI_MD_BOLD   '\x01'
#define TUI_MD_ITALIC '\x02'
#define TUI_MD_CODE   '\x03'
#define TUI_MD_OFF    '\x04'
#define TUI_MD_HEAD   '\x05'
#define TUI_MD_SUM    '\x06'
/** @} */

/**
 * @brief Renders Markdown as terminal text.
 *
 * Handles headings, bold, italic, code spans and fenced blocks, bullet and
 * numbered lists, block quotes, tables, links, and wraps prose at @p width
 * columns (values under 20 mean 20).  The lines of one paragraph are joined
 * and wrapped again; a line break counts only after an empty line, in a
 * list item, in a block quote after an empty quote line, and in a code
 * block.  The first paragraph (the summary) is set apart: bold when
 * @p ansi, followed by a blank line either way.
 *
 * With @p ansi 0 the output is plain text and stays readable: headings are
 * underlined with = and -, bold and italic markers are dropped, code is
 * indented, a link reads "text (url)".  With @p ansi 1 the same text
 * carries SGR sequences; each line resets its own.
 *
 * @param md     The Markdown source; NULL is the empty text.  Control
 *               characters are shown as spaces.
 * @param out    Appended to, no trailing newline.
 * @return 0, or -1 with errno ENOMEM (or EINVAL for a NULL @p out).
 */
TUI_API int tui_md_render(const char *md, int width, int ansi, tui_buf *out);

/** @brief Like tui_md_render(), keeping the markers.  Appended to @p out. */
TUI_API int tui_md_render_marked(const char *md, int width, tui_buf *out);

/** @brief The plain text of marked text: markers dropped, same lines. */
TUI_API int tui_md_strip_marks(const char *marked, tui_buf *out);

/** @brief One rendered line of the view (private). */
typedef struct tui_mdline {
    size_t   start, len;    /* in the rendered text */
    uint32_t spans;         /* the spans open at its start */
} tui_mdline;

/** @brief Markdown view state.  Caller-allocated; the members are private. */
typedef struct tui_mdview {
    char       *src;        /* the Markdown, a copy */
    tui_buf     text;       /* the marked rendering at `width` */
    tui_mdline *lines;
    int         nlines, cap;
    int         width;      /* the width it was rendered at */
    int         top;        /* first line shown */
    int         rows;       /* height it was last drawn at */
    int         dirty;      /* source changed since rendering */
    int         plain;      /* draw without colour and attribute */
} tui_mdview;

/** @brief An empty view. */
TUI_API void tui_mdview_init(tui_mdview *mv);

/** @brief Frees the text and the line index. */
TUI_API void tui_mdview_free(tui_mdview *mv);

/**
 * @brief Replaces the Markdown (copied) and scrolls to the top.
 * @return 0, or -1 with errno ENOMEM (the old text stays).
 */
TUI_API int tui_mdview_set_text(tui_mdview *mv, const char *md);

/**
 * @brief The plain fallback: with @p plain 1 the view draws every cell in
 *        the base style, the markers dropped, for a terminal without
 *        colour or attributes.
 */
TUI_API void tui_mdview_set_plain(tui_mdview *mv, int plain);

/** @brief Number of lines at the last render width. */
TUI_API int tui_mdview_lines(const tui_mdview *mv);

/** @brief Line at the top of the view. */
TUI_API int tui_mdview_top(const tui_mdview *mv);

/** @brief Scrolls by @p delta lines (negative: up), within the text. */
TUI_API void tui_mdview_scroll(tui_mdview *mv, int delta);

/** @brief Scrolls so that line @p line is at the top (clamped). */
TUI_API void tui_mdview_scroll_to(tui_mdview *mv, int line);

/** @brief Scrolls to show the last line at the bottom. */
TUI_API void tui_mdview_scroll_end(tui_mdview *mv);

/**
 * @brief Applies a scrolling key: Up, Down, PgUp, PgDn, Home, End and the
 *        shared letters j, k, g, G, ctrl-f, ctrl-b.
 * @return 1 when the key was one of these, 0 otherwise (Esc, q and
 *         Backspace, which go back, are the program's).
 */
TUI_API int tui_mdview_handle(tui_mdview *mv, const tui_event *ev);

/**
 * @brief Draws the visible lines into @p c, rendering to its width first if
 *        needed, and clears the rest of it with @p st.
 *
 * Headings are drawn in the accent role, code in the hint role's colour,
 * bold and the summary in bold, italic in italic, all on the background of
 * @p st.  A scroll bar takes the last column while the text is longer than
 * the area.
 */
TUI_API void tui_mdview_render(tui_mdview *mv, tui_canvas c, tui_style st);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_MDVIEW_H */
