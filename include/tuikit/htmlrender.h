/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* Public API: tui_html_render, a bounded HTML subset rendered as styled terminal text. */

#ifndef LIBTUIKIT_HTMLRENDER_H
#define LIBTUIKIT_HTMLRENDER_H

/**
 * @file tuikit/htmlrender.h
 * @brief HTML (a mail body, a fragment) shown readably as text with SGR
 *        runs, at a width.  No script runs and nothing is loaded.
 */

#include "tuikit/api.h"
#include "tuikit/buf.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Renders HTML as terminal text.
 *
 * The subset: b/strong, i/em, u, s/strike/del, code/tt/kbd, a, br, hr, p,
 * h1-h6 (bold), div and the other block elements, ul, ol (with start),
 * li (nested), blockquote, pre, table (tr, th, td; the widest column is
 * shortened with an ellipsis to fit), img (its alt text in brackets, the
 * image is never loaded), the entities (named, decimal and hexadecimal), and
 * the style colour of an element (style="color:..." or font color=, as
 * #rgb, #rrggbb, rgb() or a basic name; also font-weight, font-style and
 * text-decoration).  The text is reflowed to @p width columns (values
 * under 20 mean 20): white space collapses, a word wider than a line is
 * split at the column, a list item wraps under its text, a quote line starts
 * with "> ".
 *
 * Dropped with their content: script, style, head, title, template,
 * object, embed, iframe, applet, svg, math, select, textarea and comments.
 * A link reads "text (url)" for an http, https or mailto address that is
 * not the text itself; any other target (javascript:, data:, cid:, a
 * relative one) is not shown.  Unknown tags are ignored and their text kept;
 * broken markup (unclosed, mis-nested, stray end tags, an unfinished tag or
 * entity) renders what it can, never fails.  Control characters, bidi
 * overrides and the byte order mark in the text are dropped or shown as
 * spaces.
 *
 * With @p ansi 0 the output is plain text and stays readable (no colour, no
 * attribute).  With @p ansi 1 the same text carries SGR sequences: bold 1,
 * italic 3, underline 4 (also a link), strike 9, code 36 and the colour as
 * 38;2;r;g;b; each line resets its own, a style open at a line break is
 * opened again on the next line.
 *
 * @param html   The HTML source (UTF-8); NULL is the empty text.
 * @param out    Appended to, no trailing newline.
 * @return 0, or -1 with errno ENOMEM (or EINVAL for a NULL @p out).
 */
TUI_API int tui_html_render(const char *html, int width, int ansi, tui_buf *out);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_HTMLRENDER_H */
