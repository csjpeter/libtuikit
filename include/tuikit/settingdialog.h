/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* Public API: tui_settingdialog, a description, one input and OK / Cancel. */

#ifndef LIBTUIKIT_SETTINGDIALOG_H
#define LIBTUIKIT_SETTINGDIALOG_H

/**
 * @file tuikit/settingdialog.h
 * @brief The dialog that changes one configuration value (TUI-138): a
 *        title, a wrapped description of what the value is for, the input
 *        with the current value, a hint of the allowed values, and the
 *        buttons OK and Cancel.
 *
 * policies/tui-interface-model.md, "Editing a setting".  Like every dialog
 * it neither blocks nor reads the terminal: the program gives it events
 * (tui_settingdialog_handle()), draws it last (tui_settingdialog_render())
 * and reads the outcome once the state is no longer TUI_DIALOG_OPEN.
 * See tui_settingdialog_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/dialog.h"
#include "tuikit/event.h"
#include "tuikit/lineedit.h"
#include "tuikit/textview.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Checks @p value when OK is pressed.  Returns 0 to accept it; a
 *        non-zero value refuses it, having written the reason into @p err
 *        (shown in the dialog, in the warning role).
 */
typedef int (*tui_setting_validate_fn)(void *user, const char *value, char *err, size_t errsz);

/** @brief What has the focus. */
typedef enum tui_setting_focus {
    TUI_SETTING_FIELD = 0,   /**< the input */
    TUI_SETTING_OK,          /**< the OK button */
    TUI_SETTING_CANCEL       /**< the Cancel button */
} tui_setting_focus;

/** @brief The setting dialog.  @c state, @c focus and @c error may be read;
 *         the validation members and the styles may be set. */
typedef struct tui_settingdialog {
    tui_dialog_state state;     /**< OPEN until OK (accepted) or Cancel / Esc */
    tui_setting_focus focus;
    tui_setting_validate_fn validate;   /**< may be NULL: every value is accepted */
    void            *user;      /**< for validate */
    char             error[256];/**< the reason of the last refusal; "" when none */
    tui_lineedit     edit;      /**< edits the caller's buffer */
    char            *title;     /* private copies */
    char            *text;
    char            *hint;
    char            *orig;      /* the value at the start, put back on Cancel */
    tui_textview     body;      /* private: the wrapped description */
    tui_style        frame;     /**< border and title */
    tui_style        fill;      /**< inside */
    tui_style        focus_style;   /**< the focused button */
} tui_settingdialog;

/**
 * @brief A dialog under @p title that explains the setting in @p text
 *        (plain text, wrapped, may hold newlines), edits it in @p buf
 *        (capacity @p size) starting from @p initial and shows @p hint
 *        (the allowed values; may be NULL) under the input.  Returns 0, or
 *        -1 with errno.
 */
TUI_API int tui_settingdialog_init(tui_settingdialog *d, const char *title, const char *text,
                                   const char *hint, char *buf, size_t size, const char *initial);

/** @brief Frees what the dialog allocated.  Safe after a failed init. */
TUI_API void tui_settingdialog_free(tui_settingdialog *d);

/**
 * @brief Gives the dialog one event.
 *
 * In the input the keys of tui_lineedit_handle(3) edit; Enter presses OK;
 * Tab goes to OK and Shift-Tab to Cancel.  On a button Tab and Shift-Tab
 * move on (around to the input), Left and Right move between the buttons,
 * Enter presses the focused one, and Up, Down, PgUp and PgDn scroll a long
 * description.  Esc and Ctrl-C are Cancel everywhere.  OK runs @c validate:
 * a refusal puts the reason in @c error, keeps the dialog open and moves
 * the focus to the input; Cancel puts the initial value back into the
 * buffer.  Events after it closed are ignored.
 * @return The state afterwards.
 */
TUI_API tui_dialog_state tui_settingdialog_handle(tui_settingdialog *d, const tui_event *ev);

/**
 * @brief Draws the dialog centred in @p c, its width following the text
 *        (40 to 70 cells), its height the content, a little above the
 *        middle; the cursor is in the input while it has the focus.
 */
TUI_API void tui_settingdialog_render(tui_settingdialog *d, tui_canvas c);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_SETTINGDIALOG_H */
