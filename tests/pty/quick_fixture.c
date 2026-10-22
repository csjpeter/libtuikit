/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-21 */
/* A program for the PTY suite: an action menu of ten entries whose quick keys the library assigns. */

/*
 * The stub of TUI-62: nothing but the ':' action menu of ten entries (two of
 * them serious).  Row 0 shows what was done last, "done: <action>".  ':'
 * opens the menu, a quick key or Enter runs an entry, Esc outside the menu
 * quits.
 */

#include "tuikit/tuikit.h"

#include <stdio.h>
#include <string.h>

#define R(name, key, verb, cls, irrev, confirm) \
    { "v", key, verb, name, NULL, cls, irrev, confirm, 0 }

static const tui_keyentry g_keys[] = {
    R("Delete", TUI_KC_DELETE_D, TUI_VERB_DELETE, TUI_ACT_SERIOUS, 1, "Delete %s? It cannot be undone."),
    R("Restart the whole service and its workers", TUI_KC_NONE, TUI_VERB_NONE, TUI_ACT_SERIOUS, 0,
      "Restart %s?"),
    R("Rename", TUI_KC_NONE, TUI_VERB_NONE, TUI_ACT_RARE, 0, NULL),
    R("Archive", TUI_KC_NONE, TUI_VERB_NONE, TUI_ACT_RARE, 0, NULL),
    R("Label", TUI_KC_NONE, TUI_VERB_NONE, TUI_ACT_RARE, 0, NULL),
    R("Move", TUI_KC_MOVE, TUI_VERB_MOVE, TUI_ACT_ROUTINE, 0, NULL),
    R("Export", TUI_KC_NONE, TUI_VERB_NONE, TUI_ACT_RARE, 0, NULL),
    R("Import", TUI_KC_NONE, TUI_VERB_NONE, TUI_ACT_RARE, 0, NULL),
    R("Verify", TUI_KC_NONE, TUI_VERB_NONE, TUI_ACT_RARE, 0, NULL),
    R("Zip", TUI_KC_NONE, TUI_VERB_NONE, TUI_ACT_RARE, 0, NULL),
};
#define N_KEYS ((int)(sizeof g_keys / sizeof g_keys[0]))

int main(void) {
    tui_keytable table;
    char why[160];
    if (tui_keytable_init(&table, g_keys, N_KEYS, why, sizeof why) != 0) {
        fprintf(stderr, "quick_fixture: %s\n", why);
        return 2;
    }
    tui_term_opts o;
    tui_term_opts_init(&o);
    tui_term *t = tui_term_open(&o);
    if (!t) {
        perror("quick_fixture");
        return 2;
    }
    int rows = 0, cols = 0;
    if (tui_term_size(t, &rows, &cols) != 0) return 2;
    tui_screen *scr = tui_screen_new(rows, cols);
    tui_buf out;
    tui_buf_init(&out);
    tui_actionmenu menu;
    int menu_open = 0;
    char done[96] = "done: nothing";
    for (int quit = 0; !quit;) {
        tui_canvas all = tui_canvas_of(scr);
        tui_canvas_clear(all, TUI_STYLE_DEFAULT);
        tui_canvas_put(all, 0, 0, done, TUI_STYLE_OK);
        if (menu_open) tui_actionmenu_render(&menu, all);
        tui_screen_render(scr, &out);
        tui_term_flush_buf(t, &out);
        tui_event ev;
        if (tui_term_wait(t, -1, NULL, 0, &ev) != 1 || ev.type == TUI_EV_HANGUP) break;
        if (ev.type == TUI_EV_RESIZE) {
            tui_screen_resize(scr, ev.rows, ev.cols);
        } else if (ev.type == TUI_EV_KEY && menu_open) {
            tui_action_state st = tui_actionmenu_handle(&menu, &ev);
            if (st == TUI_ACTION_OPEN) continue;
            if (st == TUI_ACTION_CHOSEN)
                snprintf(done, sizeof done, "done: %s", g_keys[menu.entry].action);
            tui_actionmenu_free(&menu);
            menu_open = 0;
        } else if (ev.type == TUI_EV_KEY) {
            if (tui_nav_key(&ev, 0) == TUI_NAV_QUIT) quit = 1;
            else if (tui_keycode_of(&ev) == TUI_KC_MENU &&
                     tui_actionmenu_open(&menu, &table, "v", "Alpha") == 0) menu_open = 1;
        }
    }
    if (menu_open) tui_actionmenu_free(&menu);
    tui_buf_free(&out);
    tui_screen_free(scr);
    tui_term_close(t);
    return 0;
}
