/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-14 */
/* Public API: tui_statusline, the one bottom line: hints, message, wait, position, prompt. */

#ifndef LIBTUIKIT_STATUSLINE_H
#define LIBTUIKIT_STATUSLINE_H

/**
 * @file tuikit/statusline.h
 * @brief The status line: key hints that fit, a transient message, a
 *        waiting text, a position and a one-line prompt.
 *
 * A pure model drawn on the last row of a canvas, in reverse video, so
 * that the terminal's own background is kept everywhere else
 * (policies/tui-interface-model.md, "Layout").  One line, never wrapped:
 * what the line shows, in this order, is
 *
 *   1. the prompt, while one is open (tui_statusline_begin_prompt());
 *   2. the message, until the next key (a message replaces the hints);
 *   3. the waiting text, until it is cleared;
 *   4. the key hints of the view.
 *
 * The position text is right-aligned and always kept; the rest is cut
 * to make room for it.  Hints are dropped whole, lowest priority first,
 * and the trailing help hint ("? help") stays.  See tui_statusline_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"
#include "tuikit/lineedit.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Most hints a line holds. */
#define TUI_STATUSLINE_HINTS_MAX 64

/** @brief One key hint: "key label".  Higher @c priority is kept longer;
 *         among equals the earlier entry is kept. */
typedef struct tui_statusline_hint {
    const char *key;     /**< e.g. "Enter", "/", "^X" */
    const char *label;   /**< e.g. "open"; may be NULL */
    int         priority;
} tui_statusline_hint;

/** @brief What tui_statusline_handle() did with an event. */
typedef enum tui_statusline_result {
    TUI_STATUSLINE_IGNORED = 0,  /**< not for the line; the program uses it */
    TUI_STATUSLINE_CHANGED,      /**< a message was cleared, or the prompt text changed */
    TUI_STATUSLINE_ACCEPT,       /**< Enter in the prompt: tui_statusline_prompt_text() */
    TUI_STATUSLINE_CANCEL        /**< Esc or Ctrl-C in the prompt */
} tui_statusline_result;

/** @brief The line.  Plain data: initialise it with tui_statusline_init()
 *         and do not copy it (the prompt editor points into it).  Only
 *         @c bar may be set directly. */
typedef struct tui_statusline {
    tui_style   bar;          /**< the bar: reverse video by default */
    const tui_statusline_hint *hints;  /* private */
    int         nhints;
    const char *help_key;     /* private */
    const char *help_label;
    tui_style   msg_style;
    char        message[256];
    char        wait[128];
    int         wait_done, wait_total;
    char        position[64];
    int         prompting;
    char        prompt[32];
    char        pbuf[256];
    tui_lineedit le;
    int         shown;        /* private: hints shown by the last layout */
} tui_statusline;

/** @brief An empty line with the help hint "? help". */
TUI_API void tui_statusline_init(tui_statusline *sl);

/** @brief Frees the prompt editor's own memory. */
TUI_API void tui_statusline_free(tui_statusline *sl);

/**
 * @brief The hints of the view.  The array is not copied: it must stay
 *        valid while it is set (a static table).  NULL or 0 clears.
 * @return 0, or -1 with errno EINVAL (more than TUI_STATUSLINE_HINTS_MAX).
 */
TUI_API int tui_statusline_set_hints(tui_statusline *sl,
                                     const tui_statusline_hint *hints, int n);

/** @brief The always-kept last hint; @p key NULL turns it off.  The
 *         strings are not copied. */
TUI_API void tui_statusline_set_help(tui_statusline *sl, const char *key, const char *label);

/** @brief Number of hints set (the full set the help screen lists). */
TUI_API int tui_statusline_hint_count(const tui_statusline *sl);

/** @brief Hint @p i of the full set; NULL when out of range. */
TUI_API const tui_statusline_hint *tui_statusline_hint_at(const tui_statusline *sl, int i);

/** @brief How many of the hints are shown on a line of @p cols columns
 *         (the help hint not counted), with no message, wait or prompt. */
TUI_API int tui_statusline_hints_fit(const tui_statusline *sl, int cols);

/**
 * @brief A transient message in @p style (TUI_STYLE_ERROR, _OK, _PROGRESS,
 *        TUI_STYLE_DEFAULT for plain information), shown until the next
 *        key event reaches tui_statusline_handle() or it is cleared.
 *        Controls become spaces; a text longer than 255 bytes is cut at a
 *        character.  NULL or "" clears it.
 */
TUI_API void tui_statusline_set_message(tui_statusline *sl, const char *text, tui_style style);

/** @brief Clears the message. */
TUI_API void tui_statusline_clear_message(tui_statusline *sl);

/** @brief The message, "" when none. */
TUI_API const char *tui_statusline_message(const tui_statusline *sl);

/**
 * @brief A waiting text ("Syncing INBOX ..."), with " done/total" after
 *        it when @p total > 0, in TUI_STYLE_PROGRESS.  It shows until
 *        tui_statusline_clear_wait(); a program calls it again from its
 *        fetch loop as the numbers move.  NULL or "" clears it.
 */
TUI_API void tui_statusline_set_wait(tui_statusline *sl, const char *text, int done, int total);

/** @brief Clears the waiting text. */
TUI_API void tui_statusline_clear_wait(tui_statusline *sl);

/** @brief The right-aligned segment ("page 2/7"); NULL or "" clears it. */
TUI_API void tui_statusline_set_position(tui_statusline *sl, const char *text);

/** @brief The position as "[cur/total]" (@p cur is 1-based); a @p total
 *         below 1 clears it. */
TUI_API void tui_statusline_set_position_index(tui_statusline *sl, int cur, int total);

/**
 * @brief Opens the prompt: @p prompt ("/", "filter: ") and an editor with
 *        @p initial text (may be NULL), 255 bytes at most.  Until Enter
 *        or Esc, tui_statusline_handle() gives it every event.
 */
TUI_API void tui_statusline_begin_prompt(tui_statusline *sl, const char *prompt, const char *initial);

/** @brief Whether the prompt is open. */
TUI_API int tui_statusline_prompt_active(const tui_statusline *sl);

/** @brief The text of the prompt, still readable after Enter or Esc, until
 *         the next prompt. */
TUI_API const char *tui_statusline_prompt_text(const tui_statusline *sl);

/**
 * @brief Applies one event.  With the prompt open it is the editor's
 *        (tui_lineedit_handle(): Enter is ACCEPT and Esc CANCEL, both
 *        close it).  Otherwise a key or a paste clears the message
 *        (CHANGED) and is IGNORED for the line: the program handles it.
 */
TUI_API tui_statusline_result tui_statusline_handle(tui_statusline *sl, const tui_event *ev);

/**
 * @brief Draws the line on the last row of @p c: the whole row is the
 *        bar, with the pieces on it.  With the prompt open the terminal
 *        cursor is placed in it.  Everything follows from the canvas
 *        width, so a resize needs nothing more than the next render.
 */
TUI_API void tui_statusline_render(tui_statusline *sl, tui_canvas c);

/**
 * @brief The line as plain text for a line of @p cols columns, as
 *        render() lays it out (for a CLI or a test); trailing blanks
 *        are not written.
 * @return The bytes written without the NUL, or -1 when @p outsz was too
 *         small (the text is cut, terminated).
 */
TUI_API int tui_statusline_format(const tui_statusline *sl, int cols, char *out, size_t outsz);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_STATUSLINE_H */
