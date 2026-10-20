/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-18 */
/* Public API: key codes, the key table, the action menu and the help made from it. */

#ifndef LIBTUIKIT_ACTIONS_H
#define LIBTUIKIT_ACTIONS_H

/**
 * @file tuikit/actions.h
 * @brief The keyboard policy of the terminal interfaces as a library.
 *
 * policies/tui-interface-model.md, "Actions that change state: the action
 * menu".  A program lists everything its keys do in one table of
 * tui_keyentry; tui_keytable_init() refuses a table that breaks the policy
 * (the program cannot start with it), the key handler asks the table what an
 * event means (tui_keytable_find()), the help overlay is made from the same
 * table (tui_keyhelp_open()), and ':' opens the action menu of the view
 * (tui_actionmenu_open()), which asks the confirmation of a serious action
 * with the target named and No as the default answer.  See
 * tui_keytable_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/dialog.h"
#include "tuikit/event.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A key as one number: a character is its code point, a special key
 *        is its tui_key value, Ctrl+letter is TUI_KC_CTRL | the lower-case
 *        letter.  0 is "no key" (a menu-only action).
 */
typedef uint32_t tui_keycode;

/** @brief Named key codes: the common table, the reserved set, the registry. */
enum {
    TUI_KC_NONE   = 0,
    TUI_KC_CTRL   = 0x40000000u,           /**< OR-ed with a lower-case letter */

    /* The common table of tui-interface-model.md. */
    TUI_KC_DOWN_J = 'j',   TUI_KC_UP_K = 'k',
    TUI_KC_FIRST  = 'g',   TUI_KC_LAST = 'G',
    TUI_KC_BACK   = 'q',                   /**< goes back; never quits */
    TUI_KC_HELP   = '?',
    TUI_KC_MENU   = ':',                   /**< opens the action menu */
    TUI_KC_RELOAD = 'r',
    TUI_KC_SEARCH = '/',
    TUI_KC_CLEAR  = 'c',
    TUI_KC_OPEN_L = 'l',                   /**< opens a tree row (like Right) */
    TUI_KC_CLOSE_H = 'h',                  /**< closes a tree row (like Left) */
    TUI_KC_RIGHT  = TUI_KEY_RIGHT,
    TUI_KC_LEFT   = TUI_KEY_LEFT,
    TUI_KC_MARK   = ' ',
    TUI_KC_UNMARK_ALL = 'U',
    TUI_KC_ESC    = TUI_KEY_ESC,
    TUI_KC_BACKSPACE = TUI_KEY_BACKSPACE,  /**< goes back like q and Esc; a delete in a text field */
    TUI_KC_ENTER  = TUI_KEY_ENTER,
    TUI_KC_QUIT   = TUI_KC_CTRL | 'c',     /**< quits at once, from anywhere */
    TUI_KC_SUSPEND = TUI_KC_CTRL | 'z',
    TUI_KC_SUBMIT = TUI_KC_CTRL | 's',

    /* The registry of the keys that may change state. */
    TUI_KC_STAR   = '*',                   /**< toggle the star or flag */
    TUI_KC_JUNK   = '!',                   /**< toggle junk */
    TUI_KC_READ   = '+',                   /**< mark read */
    TUI_KC_UNREAD = '-',                   /**< mark unread */
    TUI_KC_DELETE_D = 'd',                 /**< delete or withdraw */
    TUI_KC_DELETE = TUI_KEY_DELETE,        /**< delete or withdraw */
    TUI_KC_NEW    = 'n',
    TUI_KC_EDIT   = 'e',
    TUI_KC_MOVE   = 't',
    TUI_KC_MESSAGE = 'm'
};

/** @brief The registry's verbs: what a direct key of a state change means. */
typedef enum tui_verb {
    TUI_VERB_NONE = 0,   /**< no registry entry: the action is menu-only */
    TUI_VERB_STAR, TUI_VERB_JUNK, TUI_VERB_READ, TUI_VERB_UNREAD,
    TUI_VERB_DELETE, TUI_VERB_NEW, TUI_VERB_EDIT, TUI_VERB_MOVE,
    TUI_VERB_MESSAGE
} tui_verb;

/** @brief The class of an action (the table of the policy). */
typedef enum tui_action_class {
    TUI_ACT_HARMLESS = 0,  /**< changes nothing outside the view: any non-registry key */
    TUI_ACT_ROUTINE,       /**< routine and reversible: a registry key, no confirmation */
    TUI_ACT_SERIOUS,       /**< deletes, cannot be undone, starts a process: registry key
                                or menu, and a confirmation naming the target */
    TUI_ACT_RARE           /**< configure, renew, rotate...: the menu only */
} tui_action_class;

/** @brief One row of a program's key table. */
typedef struct tui_keyentry {
    const char      *view;         /**< the view it applies to; NULL: every view */
    tui_keycode      key;          /**< the direct key; TUI_KC_NONE: the menu only */
    tui_verb         verb;         /**< the registry verb the key stands for */
    const char      *action;       /**< the name shown in the menu and the help */
    const char      *desc;         /**< a longer description, or NULL */
    tui_action_class cls;
    int              irreversible; /**< 1: cannot be undone (needs a confirmation) */
    const char      *confirm;      /**< the question; "%s" is replaced by the target.
                                        Required for SERIOUS and irreversible actions */
    int              id;           /**< the program's own number for the action */
} tui_keyentry;

/**
 * @par The description of a view
 * A row with no @c action, no key and only @c desc set (and @c view the view
 * it describes; NULL: every view) is the view's description: one to three
 * lines (at most three wrapped to 64 columns) shown by tui_keyhelp_open()
 * between the title and the keys.  Written in the program's language of the
 * screen; at most one per view; tui_keytable_init() refuses a longer text.
 * A view without one shows the keys only.
 */

/** @brief A validated table.  Build it with tui_keytable_init(). */
typedef struct tui_keytable {
    const tui_keyentry *entries;   /**< the caller's array, kept alive by it */
    int                 n;
} tui_keytable;

/**
 * @brief Validates and takes over @p n entries.
 *
 * Refused (-1, errno EINVAL, the reason in @p why when not NULL): a
 * state-changing entry whose direct key is not in the registry, or is the
 * registry key of another verb; a harmless entry on a registry key; a
 * serious or irreversible entry without a confirmation that contains "%s";
 * a rare entry with a direct key; the same key twice in one view; an entry
 * without an action name.  A program calls this at start and exits on -1.
 */
TUI_API int tui_keytable_init(tui_keytable *t, const tui_keyentry *entries, int n,
                              char *why, size_t size);

/**
 * @brief The entry bound to @p ev in @p view, or -1.  An entry applies to
 *        its own view and, with a NULL view, to every one.
 */
TUI_API int tui_keytable_find(const tui_keytable *t, const char *view, const tui_event *ev);

/** @brief The registry verb of @p key, or TUI_VERB_NONE when not in the registry. */
TUI_API tui_verb tui_registry_verb(tui_keycode key);

/** @brief 1 when @p key is in the reserved harmless set (views, filters,
 *         movement): no program gives it a state-changing meaning. */
TUI_API int tui_key_reserved(tui_keycode key);

/** @brief The key code of a key event; TUI_KC_NONE for anything else (and
 *         for Alt and modified special keys). */
TUI_API tui_keycode tui_keycode_of(const tui_event *ev);

/** @brief The name of @p key ("d", "Delete", "ctrl-c", "Space") in @p buf;
 *         returns its length, as snprintf() does. */
TUI_API int tui_keycode_name(tui_keycode key, char *buf, size_t size);

/** @brief What the way-out keys do. */
typedef enum tui_nav {
    TUI_NAV_NONE = 0,   /**< not a way-out key */
    TUI_NAV_BACK,       /**< go back one level */
    TUI_NAV_QUIT        /**< leave the program */
} tui_nav;

/**
 * @brief The way out: q and Backspace go back (and do nothing on the top
 *        level, they never quit); Esc goes back, and on the top level with nothing
 *        open (@p depth 0) quits; ctrl-c quits from anywhere.
 *        Call it only from views where no text field has the cursor:
 *        Backspace in a field stays a delete.
 */
TUI_API tui_nav tui_nav_key(const tui_event *ev, int depth);

/** @brief Where the action menu stands. */
typedef enum tui_action_state {
    TUI_ACTION_OPEN = 0,    /**< waiting for the user */
    TUI_ACTION_CHOSEN,      /**< run entry @c entry (confirmed when it needed it) */
    TUI_ACTION_CANCELLED    /**< Esc, ctrl-c, or the confirmation was refused */
} tui_action_state;

/** @brief The action menu, and the confirmation of a serious action. */
typedef struct tui_actionmenu {
    tui_action_state    state;
    int                 entry;     /**< CHOSEN: the index in the table */
    const tui_keytable *table;
    tui_dialog          dialog;    /* private: the menu, then the confirmation */
    int                 have_dialog;
    int                 confirming;
    const char         *target;    /* private: kept in @c mem */
    int                *idx;       /* private: table index of each menu row */
    void               *mem;       /* private */
} tui_actionmenu;

/**
 * @brief Opens the ':' menu of @p view: every state-changing action that
 *        applies there, each with its direct key beside it.  @p target
 *        (copied; may be NULL) names what the action will act on and is put
 *        into the confirmation.
 */
TUI_API int tui_actionmenu_open(tui_actionmenu *m, const tui_keytable *t, const char *view,
                                const char *target);

/**
 * @brief Runs entry @p entry from its direct key: CHOSEN at once when it
 *        needs no confirmation, otherwise the confirmation is open (the
 *        target named, No the default).
 */
TUI_API int tui_actionmenu_run(tui_actionmenu *m, const tui_keytable *t, int entry,
                               const char *target);

/** @brief Gives the menu one event; returns its state.  Enter on the
 *         default of a confirmation never performs the action. */
TUI_API tui_action_state tui_actionmenu_handle(tui_actionmenu *m, const tui_event *ev);

/** @brief Draws the menu or the confirmation centred in @p c. */
TUI_API void tui_actionmenu_render(tui_actionmenu *m, tui_canvas c);

/** @brief Frees what the menu allocated.  Safe after any open. */
TUI_API void tui_actionmenu_free(tui_actionmenu *m);

/** @brief A help overlay made from a key table. */
typedef struct tui_keyhelp {
    tui_dialog dialog;   /**< give it events with tui_dialog_handle(), draw it
                              with tui_dialog_render() */
    void      *mem;      /* private */
} tui_keyhelp;

/**
 * @brief The help of @p view: the view's description (if the table has one)
 *        under @p title, then a row for every entry that has a direct key
 *        there, in table order, the key and the action's name (and its
 *        description).  The same table drives tui_keytable_find().
 */
TUI_API int tui_keyhelp_open(tui_keyhelp *h, const tui_keytable *t, const char *view,
                             const char *title);

/** @brief Frees what the help allocated. */
TUI_API void tui_keyhelp_free(tui_keyhelp *h);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_ACTIONS_H */
