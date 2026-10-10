/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Péter Császár */

#ifndef LIBTUIKIT_DIALOG_H
#define LIBTUIKIT_DIALOG_H

/**
 * @file tuikit/dialog.h
 * @brief Modal dialogs: a message, a yes/no question, a text prompt, a menu.
 *
 * A dialog does not block and does not read the terminal.  The program
 * routes events to it first while it is open (tui_dialog_handle()), draws
 * it last (tui_dialog_render()), and reads the outcome once its state is
 * no longer TUI_DIALOG_OPEN -- the same shape as issue-manager's modals,
 * so the decision stays in the program's model.  See tui_dialog_message(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"
#include "tuikit/lineedit.h"
#include "tuikit/listview.h"
#include "tuikit/textview.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief The kind of dialog. */
typedef enum tui_dialog_kind {
    TUI_DIALOG_MESSAGE = 0,  /**< text and an OK button */
    TUI_DIALOG_CONFIRM,      /**< text and Yes / No */
    TUI_DIALOG_PROMPT,       /**< text and a line of input */
    TUI_DIALOG_MENU,         /**< a list to choose from */
    TUI_DIALOG_CHECKLIST,    /**< a list to tick several rows of */
    TUI_DIALOG_HELP          /**< the keys of a view, closed by any key */
} tui_dialog_kind;

/** @brief Where the dialog stands. */
typedef enum tui_dialog_state {
    TUI_DIALOG_OPEN = 0,     /**< still waiting for the user */
    TUI_DIALOG_OK,           /**< confirmed: see @c answer, or the prompt's buffer */
    TUI_DIALOG_CANCELLED     /**< Esc (or Ctrl-C) */
} tui_dialog_state;

/** @brief A dialog.  Read @c state and @c answer; set the styles if wanted. */
typedef struct tui_dialog {
    tui_dialog_kind  kind;
    tui_dialog_state state;
    int              answer;       /**< CONFIRM: 1 yes, 0 no; MENU, CHECKLIST: the item
                                        the list stood on */
    char            *title;        /* private copies */
    char            *text;
    const char *const *items;      /**< MENU: the caller's items, kept alive by it;
                                        HELP: key and description, in pairs */
    int              nitems;       /**< MENU: items; HELP: pairs */
    unsigned char   *checked;      /**< CHECKLIST: 1 for each ticked item (the
                                        dialog's own copy; read it on OK) */
    tui_lineedit     edit;         /**< PROMPT: edits the caller's buffer */
    tui_listview     list;         /**< MENU */
    tui_textview     body;         /* private: the wrapped text */
    tui_style        frame;        /**< border and title */
    tui_style        fill;         /**< inside */
    tui_style        focus;        /**< the focused button, the selected item */
} tui_dialog;

/** @brief A message with an OK button: Enter, Space or Esc closes it. */
TUI_API int tui_dialog_message(tui_dialog *d, const char *title, const char *text);

/**
 * @brief A yes/no question.  y/n answer at once; Left, Right and Tab move
 *        between the buttons and Enter takes the focused one; Esc cancels.
 *        @p default_yes puts the focus on Yes first.
 */
TUI_API int tui_dialog_confirm(tui_dialog *d, const char *title, const char *text,
                               int default_yes);

/**
 * @brief A prompt for one line, edited in @p buf (capacity @p size) with
 *        @p initial text: Enter confirms, Esc or Ctrl-C cancels.
 */
TUI_API int tui_dialog_prompt(tui_dialog *d, const char *title, const char *text,
                              char *buf, size_t size, const char *initial);

/**
 * @brief A menu of @p n items, @p selected first: the list keys move,
 *        Enter chooses, Esc cancels.  The items are not copied.
 */
TUI_API int tui_dialog_menu(tui_dialog *d, const char *title,
                            const char *const *items, int n, int selected);

/**
 * @brief A list of @p n items in which several can be ticked: Space ticks
 *        or unticks the row the list stands on, the list keys move, Enter
 *        confirms, Esc cancels.  @p initial (may be NULL: nothing ticked)
 *        gives each item's first state; it is copied, so a cancelled dialog
 *        leaves the caller's set as it was.  The items are not copied.
 *
 * On OK the ticked set is @c checked (or tui_dialog_is_checked()), and
 * @c answer is the row Enter was pressed on -- the one choice a program can
 * fall back to when nothing is ticked.
 */
TUI_API int tui_dialog_checklist(tui_dialog *d, const char *title,
                                 const char *const *items, int n,
                                 const unsigned char *initial, int selected);

/**
 * @brief The help screen of a view: @p n rows of a key and what it does,
 *        given as 2 * @p n strings in @p pairs (key, description, key,
 *        description, ...), in two columns with the keys in bold, under
 *        @p title (the view's name, e.g. "Message list shortcuts").
 *
 * Esc, q and Backspace close it (state TUI_DIALOG_OK), as they go back
 * from any view, and so does Ctrl-C; the list's movement keys (arrows,
 * page keys, Home/End, j/k, ctrl-f/ctrl-b, g/G) scroll it when the rows do
 * not all fit; every other key changes nothing.  The strings are not
 * copied.
 * policies/tui-interface-model.md, "The help screen".
 */
TUI_API int tui_dialog_help(tui_dialog *d, const char *title,
                            const char *const *pairs, int n);

/** @brief 1 when item @p i of a checklist is ticked, 0 otherwise. */
TUI_API int tui_dialog_is_checked(const tui_dialog *d, int i);

/** @brief Frees what the dialog allocated.  Safe after any constructor. */
TUI_API void tui_dialog_free(tui_dialog *d);

/**
 * @brief Gives the dialog one event.
 * @return The state afterwards.  Events after it closed are ignored.
 */
TUI_API tui_dialog_state tui_dialog_handle(tui_dialog *d, const tui_event *ev);

/**
 * @brief Draws the dialog centred in @p c, sized to its content and to at
 *        most the canvas less a margin of one cell; a prompt puts the
 *        terminal cursor in its input line.
 */
TUI_API void tui_dialog_render(tui_dialog *d, tui_canvas c);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_DIALOG_H */
