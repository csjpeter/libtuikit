/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-09 */
/* Public API: tui_menu, the main menu of a program with several main views. */

#ifndef LIBTUIKIT_MENU_H
#define LIBTUIKIT_MENU_H

/**
 * @file tuikit/menu.h
 * @brief The main menu: one numbered entry per main view, a live count and
 *        a description of the entry under the cursor.
 *
 * Drawn on a canvas (the free area between the top bars and the status
 * line) as policies/tui-interface-model.md, "Navigation", says: a block
 * whose rows are left-aligned inside it, centered in the canvas and a
 * little above the middle so that the block and the description below it
 * look centred together.  No title and no program name.  A row is
 * <tt>"> 1  Label  N (M)"</tt>: the cursor row has the @c > mark and the
 * role "selection"; the hot key (the digits 1 to 9) is in the text colour;
 * the count is @c N, and when @c attention is above zero @c "(M)" follows
 * it in the warning colour (bold orange).  The description of the entry under the cursor
 * is wrapped to two or three centered lines below the block.  Everything
 * follows from the canvas size, so a resize needs nothing but the next
 * render.  See tui_menu_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Most entries a menu holds: the hot keys are 1 to 9. */
#define TUI_MENU_MAX 9

/** @brief One entry.  The strings are not copied. */
typedef struct tui_menu_entry {
    const char *label;        /**< the view's name */
    int         count;        /**< the live count; below 0: none shown */
    int         attention;    /**< how many of them need attention; 0: not shown */
    const char *description;  /**< shown under the block while the cursor is here; may be NULL */
} tui_menu_entry;

/** @brief What tui_menu_handle() did with an event. */
typedef enum tui_menu_result {
    TUI_MENU_IGNORED = 0,   /**< not for the menu; the program uses it */
    TUI_MENU_MOVED,         /**< the cursor moved: draw again */
    TUI_MENU_ACTIVATED      /**< Enter or a hot key: open tui_menu_selected() */
} tui_menu_result;

/** @brief The menu.  Plain data: initialise it with tui_menu_init(). */
typedef struct tui_menu {
    const tui_menu_entry *entries;   /* private: not copied */
    int                   n;
    int                   selected;
} tui_menu;

/** @brief An empty menu. */
TUI_API void tui_menu_init(tui_menu *m);

/**
 * @brief The entries and the cursor.  The array is not copied: it must stay
 *        valid while set, and a program may change the counts in it and
 *        draw again.  @p selected is clamped.  Controls in a text are
 *        drawn as blanks.
 * @return 0, or -1 with errno EINVAL (more than TUI_MENU_MAX entries, or a
 *         NULL menu, array or label).
 */
TUI_API int tui_menu_set(tui_menu *m, const tui_menu_entry *entries, int n, int selected);

/** @brief Moves the cursor to entry @p i (clamped). */
TUI_API void tui_menu_select(tui_menu *m, int i);

/** @brief The entry under the cursor; 0 for an empty menu. */
TUI_API int tui_menu_selected(const tui_menu *m);

/** @brief The number of entries. */
TUI_API int tui_menu_count(const tui_menu *m);

/**
 * @brief Up and Down (and k and j) move the cursor, Home and End go to the
 *        first and last entry; none of them wraps.  Enter is ACTIVATED; a
 *        digit 1 to 9 moves the cursor to that entry and is ACTIVATED when
 *        it exists.  Anything else is IGNORED.
 */
TUI_API tui_menu_result tui_menu_handle(tui_menu *m, const tui_event *ev);

/** @brief Draws the menu on @p c, whose cells it overwrites only where
 *         it has text. */
TUI_API void tui_menu_render(const tui_menu *m, tui_canvas c);

/**
 * @brief The menu as plain text for a canvas of @p rows by @p cols, as
 *        render() lays it out: one line per row, ending in a newline,
 *        trailing blanks not written (for a CLI or a test).
 * @return The bytes written without the NUL, or -1 when @p outsz was too
 *         small (the text is cut, terminated).
 */
TUI_API int tui_menu_format(const tui_menu *m, int rows, int cols, char *out, size_t outsz);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_MENU_H */
