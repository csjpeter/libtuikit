/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* Public API: tui_yesno, the inline one-row yes/no question "Text? (y/N)". */

#ifndef LIBTUIKIT_YESNO_H
#define LIBTUIKIT_YESNO_H

/**
 * @file tuikit/yesno.h
 * @brief The inline yes/no question: "Delete R01? (y/N)" on one row of a
 *        canvas, answered by one key.
 *
 * A pure model like the other widgets: the program routes the events to it
 * while it is open (tui_yesno_handle()), draws it on a row
 * (tui_yesno_render()) and reads the outcome once it is no longer
 * TUI_YESNO_OPEN.  The answer is one event: y or Y is yes; every other key,
 * Esc included, is no (the default is the capital N of the suffix); Ctrl-C
 * is reported apart, so the program can leave.  It is the one-row form of
 * the modal tui_dialog_confirm(), for a program that asks on its bottom row
 * and keeps the screen as it is.  See tui_yesno_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Where the question stands, and the answer. */
typedef enum tui_yesno_result {
    TUI_YESNO_OPEN = 0,   /**< waiting for a key */
    TUI_YESNO_YES,        /**< y or Y */
    TUI_YESNO_NO,         /**< any other key, Esc included */
    TUI_YESNO_INTERRUPT   /**< Ctrl-C (or the terminal hung up) */
} tui_yesno_result;

/** @brief The question.  Plain data: initialise it with tui_yesno_init(). */
typedef struct tui_yesno {
    char text[256];        /* private */
    int  result;           /* private: a tui_yesno_result */
} tui_yesno;

/**
 * @brief Opens the question @p text ("Delete R01?"; the suffix " (y/N)" is
 *        added by the widget).  Controls become blanks; more than 255 bytes
 *        are cut at a character.  NULL is an empty question.
 */
TUI_API void tui_yesno_init(tui_yesno *yn, const char *text);

/**
 * @brief Applies one event while the question is open: a key sets the
 *        outcome (y or Y: YES; Ctrl-C: INTERRUPT; any other key: NO).  A
 *        paste, a resize and the other events leave it open (HANGUP is
 *        INTERRUPT).  Once answered, the outcome stays.
 * @return The outcome so far: TUI_YESNO_OPEN until a key answered.
 */
TUI_API tui_yesno_result tui_yesno_handle(tui_yesno *yn, const tui_event *ev);

/** @brief The outcome so far. */
TUI_API tui_yesno_result tui_yesno_get(const tui_yesno *yn);

/**
 * @brief Draws the question on row @p row of @p c: the whole row is filled
 *        in the role "text" and the question is drawn on it, the suffix
 *        "(y/N)" in bold.  A question wider than the row is cut with an
 *        ellipsis so that the suffix stays; with the question open the
 *        terminal cursor is placed after the suffix.  Nothing is drawn
 *        outside the canvas.
 */
TUI_API void tui_yesno_render(const tui_yesno *yn, tui_canvas c, int row);

/**
 * @brief The row as plain text for @p cols columns, as render() lays it
 *        out (for a CLI or a test).
 * @return The bytes written without the NUL, or -1 when @p outsz was too
 *         small (the text is cut, terminated).
 */
TUI_API int tui_yesno_format(const tui_yesno *yn, int cols, char *out, size_t outsz);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_YESNO_H */
