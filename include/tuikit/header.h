/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-09 */
/* Public API: tui_header, the top bar: view, program name and a notice area. */

#ifndef LIBTUIKIT_HEADER_H
#define LIBTUIKIT_HEADER_H

/**
 * @file tuikit/header.h
 * @brief The header bar: the view on the left, the program's name on the
 *        right, and between them a notice area for messages and standing
 *        warnings.
 *
 * Policy "The header carries no statistics, the bottom line carries the
 * keys" (policies/tui-interface-model.md): the header holds the view, at
 * most one count or filter, and the program's name, and keeps the space
 * between for a short notice.  Messages and warnings use the notice, in
 * the scheme's roles ok, error and progress; no other colour.
 *
 * Two kinds of notice:
 *   - a plain notice (tui_header_set_notice()) clears on the next key
 *     that reaches tui_header_handle();
 *   - a standing warning (tui_header_set_warning(), red, the error role)
 *     stays across keys until tui_header_clear_warning() or until the
 *     program sets another warning.  It wins over a plain notice: it
 *     replaces one that shows, and a plain notice set while a warning
 *     stands is not shown.
 *
 * Clipping, when the row is too narrow: the left text is cut first
 * (with an ellipsis, then dropped), then the notice; the name is never
 * cut unless it alone is wider than the row.  See tui_header_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What tui_header_handle() did with an event. */
typedef enum tui_header_result {
    TUI_HEADER_IGNORED = 0,   /**< nothing changed; the program uses the key */
    TUI_HEADER_CHANGED        /**< a plain notice was cleared: redraw */
} tui_header_result;

/** @brief The bar.  Plain data: initialise it with tui_header_init(),
 *         do not copy it.  Only @c bar may be set directly. */
typedef struct tui_header {
    tui_style bar;             /**< the bar: the scheme's header role */
    tui_style notice_style;    /* private */
    char      left[256];
    char      right[128];
    char      notice[256];
    int       standing;        /* private: the notice is a warning */
} tui_header;

/** @brief An empty bar. */
TUI_API void tui_header_init(tui_header *h);

/** @brief The left text (the view, at most one count or filter).
 *         Controls become spaces; NULL or "" clears. */
TUI_API void tui_header_set_left(tui_header *h, const char *text);

/** @brief The right text (the program's name); NULL or "" clears. */
TUI_API void tui_header_set_right(tui_header *h, const char *text);

/**
 * @brief A plain notice in @p style (TUI_STYLE_OK, TUI_STYLE_ERROR,
 *        TUI_STYLE_PROGRESS, or TUI_STYLE_DEFAULT), cleared by the next
 *        key.  NULL or "" clears a plain notice.  Ignored while a
 *        standing warning shows.
 */
TUI_API void tui_header_set_notice(tui_header *h, const char *text, tui_style style);

/** @brief A standing warning (error role): shown until
 *         tui_header_clear_warning() or another warning.  NULL or ""
 *         clears it. */
TUI_API void tui_header_set_warning(tui_header *h, const char *text);

/** @brief Clears the standing warning (its cause is gone). */
TUI_API void tui_header_clear_warning(tui_header *h);

/** @brief The notice text that shows, "" when none. */
TUI_API const char *tui_header_notice(const tui_header *h);

/** @brief Whether the notice that shows is a standing warning. */
TUI_API int tui_header_warning_active(const tui_header *h);

/** @brief A key or paste clears a plain notice (CHANGED); a standing
 *         warning and everything else are left (IGNORED). */
TUI_API tui_header_result tui_header_handle(tui_header *h, const tui_event *ev);

/** @brief Draws the bar on the first row of @p c, over its whole width. */
TUI_API void tui_header_render(tui_header *h, tui_canvas c);

/**
 * @brief The bar as plain text for @p cols columns, as render() lays it
 *        out; trailing blanks are not written.
 * @return The bytes written without the NUL, or -1 when @p outsz was too
 *         small (the text is cut, terminated).
 */
TUI_API int tui_header_format(const tui_header *h, int cols, char *out, size_t outsz);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_HEADER_H */
