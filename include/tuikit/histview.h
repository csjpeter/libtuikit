/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-21 */
/* Public API: tui_histview, the list of recorded steps with a Revert entry. */

#ifndef LIBTUIKIT_HISTVIEW_H
#define LIBTUIKIT_HISTVIEW_H

/**
 * @file tuikit/histview.h
 * @brief The history view of policies/undo-model.md, shared by the TUIs.
 *
 * The program supplies the recorded steps (an array of tui_histview_row, read
 * from its server) and a revert callback; the widget draws them in a
 * tui_table (time, who, operation, thing, old value, new value and a marker
 * column: "reverts N" for a step that reverts another, "not revertable" for
 * one that cannot be; a step that is both shows "not revertable"), scrolls
 * them with the common list keys and goes back with Esc, q and Backspace.
 * ':' opens the action menu with "Revert" (its quick key comes from the
 * library's verb table); Enter on a step opens the same confirmation, which
 * names the step, the thing and what it will become, with No as the default
 * answer.  A step marked not revertable shows a notice and opens no dialog.
 * What the server refuses (a later change overtook the step) is shown in the
 * server's words in one line at the bottom.
 *
 * Two modes: the history of one object, and the recent changes (a column
 * "object" is added).  '?' opens the help made from the widget's key table.
 * The view has an empty state ("no recorded changes") and an error state
 * ("could not be read").  See tui_histview_init(3).
 */

#include "tuikit/actions.h"
#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/dialog.h"
#include "tuikit/event.h"
#include "tuikit/table.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What the view shows. */
typedef enum tui_histview_mode {
    TUI_HISTVIEW_OBJECT = 0,   /**< the history of one object */
    TUI_HISTVIEW_RECENT        /**< recent changes: a column "object" is added */
} tui_histview_mode;

/** @brief One recorded step.  Strings are the program's, not copied; NULL is "". */
typedef struct tui_histview_row {
    uint64_t    id;          /**< the step's number, given to the revert callback */
    const char *time;        /**< shown as given */
    const char *who;
    const char *operation;
    const char *object;      /**< shown in TUI_HISTVIEW_RECENT only */
    const char *thing;       /**< the field or part that changed */
    const char *old_value;
    const char *new_value;
    uint64_t    reverts;     /**< the id of the step this one reverts; 0: none */
    int         revertable;  /**< 0: marked "not revertable", Enter opens no dialog */
} tui_histview_row;

/**
 * @brief Asks the program (its server) to revert step @p id.
 * @return 0 when it was reverted; -1 when it was refused, with the server's
 *         words in @p refusal (capacity @p size), shown in one line.
 */
typedef int (*tui_histview_revert_fn)(void *ctx, uint64_t id, char *refusal, size_t size);

/** @brief What the program does after tui_histview_handle(). */
typedef enum tui_histview_state {
    TUI_HISTVIEW_OPEN = 0,    /**< stay in the view */
    TUI_HISTVIEW_BACK,        /**< Esc, q or Backspace: leave the view */
    TUI_HISTVIEW_REVERTED,    /**< the callback reverted a step: reload the rows */
    TUI_HISTVIEW_QUIT         /**< ctrl-c: leave the program */
} tui_histview_state;

/** @brief The view's state.  The table may be read and its colours set. */
typedef struct tui_histview {
    tui_table              table;
    tui_histview_mode       mode;
    const tui_histview_row *rows;
    int                    n;
    tui_histview_revert_fn  revert;
    void                  *ctx;
    int                    failed;        /* private from here on */
    int                    notice_ok;
    char                   notice[160];
    char                   target[320];
    tui_keytable           keys;
    tui_actionmenu         menu;
    tui_keyhelp            help;
    int                    overlay;
    void                  *reserved[2];
} tui_histview;

/**
 * @brief A history view in @p mode; @p revert (may be NULL: nothing can be
 *        reverted) is called with @p ctx.
 * @return 0, or -1 with errno ENOMEM.
 */
TUI_API int tui_histview_init(tui_histview *h, tui_histview_mode mode,
                             tui_histview_revert_fn revert, void *ctx);

/** @brief The steps to show (not copied: they must stay valid until the next
 *         call).  The selection is kept by step id.  Clears the error state;
 *         no rows shows "no recorded changes". */
TUI_API void tui_histview_set_rows(tui_histview *h, const tui_histview_row *rows, int n);

/** @brief 1: the history could not be read ("could not be read" instead of
 *         the rows); 0 clears it. */
TUI_API void tui_histview_set_failed(tui_histview *h, int failed);

/** @brief The selected step as an index into the rows, or -1. */
TUI_API int tui_histview_selected(const tui_histview *h);

/** @brief The line at the bottom (a refusal, a notice), or "". */
TUI_API const char *tui_histview_notice(const tui_histview *h);

/** @brief 1 while the menu, the confirmation or the help is open. */
TUI_API int tui_histview_modal(const tui_histview *h);

/** @brief Gives the view one key event. */
TUI_API tui_histview_state tui_histview_handle(tui_histview *h, const tui_event *ev);

/** @brief Draws the view into @p c: the table, the line at the bottom and
 *         whatever dialog is open, centred. */
TUI_API void tui_histview_render(tui_histview *h, tui_canvas c);

/** @brief Releases what the view holds.  Safe after init. */
TUI_API void tui_histview_free(tui_histview *h);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_HISTVIEW_H */
