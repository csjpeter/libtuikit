/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-20 */
/* A program for the PTY suite: a tree closed on opening, with markers, counts and the ':' menu. */

/*
 * A demo of the tree of policies/tui-interface-model.md: six rows, all
 * branches closed on opening, the marker in the first two cells, the child
 * count on a closed row, the keys of tui_tree_handle().  Enter is the
 * program's own ("done: open <id>"), 'r' reloads, ':' opens a menu with
 * "Open all" and "Close all".  Row 1 shows what was done last.
 */

#include "tuikit/tuikit.h"

#include <stdio.h>
#include <string.h>

enum { A_MENU = 1, A_OPEN_ALL, A_CLOSE_ALL };

static const tui_keyentry g_keys[] = {
    { NULL, TUI_KC_MENU, TUI_VERB_NONE, "Action menu", NULL, TUI_ACT_HARMLESS, 0, NULL, A_MENU },
    { "tree", TUI_KC_NONE, TUI_VERB_NONE, "Open all", NULL, TUI_ACT_RARE, 0, NULL, A_OPEN_ALL },
    { "tree", TUI_KC_NONE, TUI_VERB_NONE, "Close all", NULL, TUI_ACT_RARE, 0, NULL, A_CLOSE_ALL },
};
#define N_KEYS ((int)(sizeof g_keys / sizeof g_keys[0]))

static const tui_treerow g_rows[] = {
    { "Alpha", 0, 2 }, { "Alpha1", 1, 1 }, { "Alpha1a", 2, 0 }, { "Alpha2", 1, 0 },
    { "Bravo", 0, 1 }, { "Bravo1", 1, 0 }, { "Charlie", 0, 0 } };
#define N_ROWS ((int)(sizeof g_rows / sizeof g_rows[0]))

typedef struct {
    tui_tree       tree;
    tui_keytable   table;
    tui_actionmenu menu;
    int            menu_open;
    char           done[96];
} app;

static int draw_label(void *ctx, tui_canvas c, int row, int selected, tui_style base) {
    (void)ctx;
    (void)selected;
    tui_canvas_put(c, 0, 0, g_rows[row].id, base);
    return (int)strlen(g_rows[row].id);
}

static void draw(app *a, tui_screen *scr) {
    tui_canvas all = tui_canvas_of(scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_canvas_put_aligned(tui_canvas_sub(all, tui_rect_make(0, 0, 1, tui_canvas_cols(all))), 0,
                           " Tree", TUI_ALIGN_LEFT, TUI_STYLE_HEADER);
    tui_canvas_put(all, 1, 0, a->done, TUI_STYLE_OK);
    tui_canvas body = tui_canvas_sub(all, tui_rect_make(3, 0, tui_canvas_rows(all) - 3,
                                                        tui_canvas_cols(all)));
    tui_tree_render(&a->tree, body, TUI_STYLE_DEFAULT, TUI_STYLE_SELECTION, draw_label, NULL);
    if (a->menu_open) tui_actionmenu_render(&a->menu, all);
}

static int handle(app *a, const tui_event *ev) {
    if (a->menu_open) {
        if (tui_nav_key(ev, 1) == TUI_NAV_QUIT) return 1;
        tui_action_state st = tui_actionmenu_handle(&a->menu, ev);
        if (st == TUI_ACTION_OPEN) return 0;
        if (st == TUI_ACTION_CHOSEN) {
            if (g_keys[a->menu.entry].id == A_OPEN_ALL) tui_tree_open_all(&a->tree);
            else tui_tree_close_all(&a->tree);
            snprintf(a->done, sizeof a->done, "done: %s", g_keys[a->menu.entry].action);
        }
        tui_actionmenu_free(&a->menu);
        a->menu_open = 0;
        return 0;
    }
    if (tui_nav_key(ev, 0) == TUI_NAV_QUIT) return 1;
    if (tui_tree_handle(&a->tree, ev)) return 0;
    if (ev->key == TUI_KEY_ENTER) {
        snprintf(a->done, sizeof a->done, "done: open %s", g_rows[tui_tree_selected_row(&a->tree)].id);
    } else if (ev->key == TUI_KEY_CHAR && ev->cp == 'r' && ev->mods == 0) {
        tui_tree_set_rows(&a->tree, g_rows, N_ROWS);
        snprintf(a->done, sizeof a->done, "done: reloaded");
    } else if (tui_keytable_find(&a->table, "tree", ev) >= 0 &&
               tui_actionmenu_open(&a->menu, &a->table, "tree", NULL) == 0) {
        a->menu_open = 1;
    }
    return 0;
}

int main(void) {
    app a;
    memset(&a, 0, sizeof a);
    char why[160];
    if (tui_keytable_init(&a.table, g_keys, N_KEYS, why, sizeof why) != 0) {
        fprintf(stderr, "tree_fixture: %s\n", why);
        return 2;
    }
    tui_tree_init(&a.tree);
    tui_tree_show_counts(&a.tree, 1);
    if (tui_tree_set_rows(&a.tree, g_rows, N_ROWS) != 0) return 2;
    snprintf(a.done, sizeof a.done, "done: nothing");

    tui_term_opts o;
    tui_term_opts_init(&o);
    tui_term *t = tui_term_open(&o);
    if (!t) {
        perror("tree_fixture");
        return 2;
    }
    int rows = 0, cols = 0;
    if (tui_term_size(t, &rows, &cols) != 0) return 2;
    tui_screen *scr = tui_screen_new(rows, cols);
    tui_buf out;
    tui_buf_init(&out);
    for (int quit = 0; !quit;) {
        draw(&a, scr);
        tui_screen_render(scr, &out);
        tui_term_flush_buf(t, &out);
        tui_event ev;
        if (tui_term_wait(t, -1, NULL, 0, &ev) != 1 || ev.type == TUI_EV_HANGUP) break;
        if (ev.type == TUI_EV_RESIZE) tui_screen_resize(scr, ev.rows, ev.cols);
        else if (ev.type == TUI_EV_KEY) quit = handle(&a, &ev);
    }
    if (a.menu_open) tui_actionmenu_free(&a.menu);
    tui_tree_free(&a.tree);
    tui_buf_free(&out);
    tui_screen_free(scr);
    tui_term_close(t);
    return 0;
}
