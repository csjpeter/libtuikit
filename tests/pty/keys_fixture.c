/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-18 */
/* A program for the PTY suite: a list driven entirely by one key table (the action menu, the help). */

/*
 * A demo of the keyboard policy (policies/tui-interface-model.md): three
 * rows, a detail view opened with Enter, and nothing bound but through one
 * tui_keyentry table.  The key handler reads the table (tui_keytable_find()),
 * the help overlay is made from it (tui_keyhelp_open()), ':' opens the action
 * menu, and a serious action asks a confirmation that names the row.
 * Row 1 shows what was done last: "done: <action> <row>".
 */

#include "tuikit/tuikit.h"

#include <stdio.h>
#include <string.h>

enum { A_HELP = 1, A_MENU, A_BACK, A_QUIT, A_UP, A_DOWN, A_OPEN, A_RELOAD, A_STAR, A_JUNK,
       A_DELETE, A_RESTART, A_ROTATE, A_READ, A_ESC };

static const tui_keyentry g_keys[] = {
    { NULL,     TUI_KC_HELP,   TUI_VERB_NONE, "Help", NULL, TUI_ACT_HARMLESS, 0, NULL, A_HELP },
    { NULL,     TUI_KC_MENU,   TUI_VERB_NONE, "Action menu", NULL, TUI_ACT_HARMLESS, 0, NULL, A_MENU },
    { NULL,     TUI_KC_BACK,   TUI_VERB_NONE, "Back", NULL, TUI_ACT_HARMLESS, 0, NULL, A_BACK },
    { NULL,     TUI_KC_ESC,    TUI_VERB_NONE, "Back; quit on the top level", NULL, TUI_ACT_HARMLESS, 0, NULL, A_ESC },
    { NULL,     TUI_KC_QUIT,   TUI_VERB_NONE, "Quit", NULL, TUI_ACT_HARMLESS, 0, NULL, A_QUIT },
    { "list",   TUI_KC_DOWN_J, TUI_VERB_NONE, "Down", NULL, TUI_ACT_HARMLESS, 0, NULL, A_DOWN },
    { "list",   TUI_KC_UP_K,   TUI_VERB_NONE, "Up", NULL, TUI_ACT_HARMLESS, 0, NULL, A_UP },
    { "list",   TUI_KC_ENTER,  TUI_VERB_NONE, "Open", NULL, TUI_ACT_HARMLESS, 0, NULL, A_OPEN },
    { "list",   TUI_KC_RELOAD, TUI_VERB_NONE, "Reload", NULL, TUI_ACT_HARMLESS, 0, NULL, A_RELOAD },
    { "list",   TUI_KC_STAR,   TUI_VERB_STAR, "Star", "toggle the star", TUI_ACT_ROUTINE, 0, NULL, A_STAR },
    { "list",   TUI_KC_JUNK,   TUI_VERB_JUNK, "Junk", NULL, TUI_ACT_ROUTINE, 0, NULL, A_JUNK },
    { "list",   TUI_KC_DELETE_D, TUI_VERB_DELETE, "Delete", NULL, TUI_ACT_SERIOUS, 1,
      "Delete %s? It cannot be undone.", A_DELETE },
    { "list",   TUI_KC_NONE,   TUI_VERB_NONE, "Restart service", "starts a process", TUI_ACT_SERIOUS, 0,
      "Restart the service of %s?", A_RESTART },
    { "list",   TUI_KC_NONE,   TUI_VERB_NONE, "Rotate secret", NULL, TUI_ACT_RARE, 0, NULL, A_ROTATE },
    { "detail", TUI_KC_READ,   TUI_VERB_READ, "Mark read", NULL, TUI_ACT_ROUTINE, 0, NULL, A_READ },
    { "detail", TUI_KC_NONE,   TUI_VERB_NONE, "Rotate secret", NULL, TUI_ACT_RARE, 0, NULL, A_ROTATE },
};
#define N_KEYS ((int)(sizeof g_keys / sizeof g_keys[0]))

static const char *const g_rows[] = { "Alpha", "Bravo", "Charlie" };
#define N_ROWS 3

typedef struct {
    tui_keytable table;
    tui_listview list;
    int          depth;          /* 1: the detail view is open */
    tui_actionmenu menu;
    int          menu_open;
    tui_keyhelp  help;
    int          help_open;
    char         done[96];
} app;

static const char *view_of(const app *a) { return a->depth ? "detail" : "list"; }

static void draw_row(void *ctx, tui_canvas row, int i, int selected) {
    (void)ctx;
    tui_style st = selected ? TUI_STYLE_SELECTION : TUI_STYLE_DEFAULT;
    tui_canvas_clear(row, st);
    tui_canvas_put(row, 0, 0, selected ? "> " : "  ", st);
    tui_canvas_put(row, 0, 2, g_rows[i], st);
}

static void draw(app *a, tui_screen *scr) {
    tui_canvas all = tui_canvas_of(scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    char line[128];
    snprintf(line, sizeof line, " Keys · %s", view_of(a));
    tui_canvas_put_aligned(tui_canvas_sub(all, tui_rect_make(0, 0, 1, tui_canvas_cols(all))), 0,
                           line, TUI_ALIGN_LEFT, TUI_STYLE_HEADER);
    tui_canvas_put(all, 1, 0, a->done, TUI_STYLE_OK);
    if (a->depth) {
        snprintf(line, sizeof line, "Detail of %s", g_rows[a->list.selected]);
        tui_canvas_put(all, 3, 0, line, TUI_STYLE_DEFAULT);
    } else {
        tui_listview_render(&a->list, tui_canvas_sub(all, tui_rect_make(3, 0, N_ROWS, tui_canvas_cols(all))),
                            TUI_STYLE_DEFAULT, draw_row, NULL);
    }
    if (a->menu_open) tui_actionmenu_render(&a->menu, all);
    if (a->help_open) tui_dialog_render(&a->help.dialog, all);
}

static void perform(app *a, int idx) {
    const tui_keyentry *e = &g_keys[idx];
    snprintf(a->done, sizeof a->done, "done: %s %s", e->action, g_rows[a->list.selected]);
}

/* One key of the table; returns 1 to quit. */
static int act(app *a, int idx) {
    const tui_keyentry *e = &g_keys[idx];
    switch (e->id) {
    case A_HELP:
        if (tui_keyhelp_open(&a->help, &a->table, view_of(a),
                             a->depth ? "Detail shortcuts" : "List shortcuts") == 0) a->help_open = 1;
        return 0;
    case A_MENU:
        if (tui_actionmenu_open(&a->menu, &a->table, view_of(a), g_rows[a->list.selected]) == 0)
            a->menu_open = 1;
        return 0;
    case A_DOWN: tui_listview_move_down(&a->list); return 0;
    case A_UP:   tui_listview_move_up(&a->list); return 0;
    case A_OPEN: a->depth = 1; return 0;
    case A_RELOAD: snprintf(a->done, sizeof a->done, "done: reloaded"); return 0;
    default:
        if (e->cls == TUI_ACT_HARMLESS) return 0;
        /* A state change: through the menu's machinery, so a serious one confirms. */
        if (tui_actionmenu_run(&a->menu, &a->table, idx, g_rows[a->list.selected]) != 0) return 0;
        if (a->menu.state == TUI_ACTION_CHOSEN) {
            perform(a, idx);
            tui_actionmenu_free(&a->menu);
        } else {
            a->menu_open = 1;
        }
        return 0;
    }
}

static int handle(app *a, const tui_event *ev) {
    if (a->help_open) {
        if (tui_nav_key(ev, 1) == TUI_NAV_QUIT) return 1;
        if (tui_dialog_handle(&a->help.dialog, ev) != TUI_DIALOG_OPEN) {
            tui_keyhelp_free(&a->help);
            a->help_open = 0;
        }
        return 0;
    }
    if (a->menu_open) {
        if (tui_nav_key(ev, 1) == TUI_NAV_QUIT) return 1;
        tui_action_state st = tui_actionmenu_handle(&a->menu, ev);
        if (st == TUI_ACTION_OPEN) return 0;
        if (st == TUI_ACTION_CHOSEN) perform(a, a->menu.entry);
        tui_actionmenu_free(&a->menu);
        a->menu_open = 0;
        return 0;
    }
    switch (tui_nav_key(ev, a->depth)) {
    case TUI_NAV_QUIT: return 1;
    case TUI_NAV_BACK: a->depth = 0; return 0;
    default: break;
    }
    int idx = tui_keytable_find(&a->table, view_of(a), ev);
    return idx >= 0 ? act(a, idx) : 0;
}

int main(void) {
    app a;
    memset(&a, 0, sizeof a);
    char why[160];
    if (tui_keytable_init(&a.table, g_keys, N_KEYS, why, sizeof why) != 0) {
        fprintf(stderr, "keys_fixture: %s\n", why);
        return 2;
    }
    tui_listview_init(&a.list);
    tui_listview_set_count(&a.list, N_ROWS);
    snprintf(a.done, sizeof a.done, "done: nothing");

    tui_term_opts o;
    tui_term_opts_init(&o);
    tui_term *t = tui_term_open(&o);
    if (!t) {
        perror("keys_fixture");
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
        if (ev.type == TUI_EV_RESIZE) {
            tui_screen_resize(scr, ev.rows, ev.cols);
        } else if (ev.type == TUI_EV_KEY) {
            quit = handle(&a, &ev);
        }
    }
    if (a.menu_open) tui_actionmenu_free(&a.menu);
    if (a.help_open) tui_keyhelp_free(&a.help);
    tui_buf_free(&out);
    tui_screen_free(scr);
    tui_term_close(t);
    return 0;
}
