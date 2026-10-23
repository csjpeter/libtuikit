/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-21 */
/* The history view: recorded steps in a table, a Revert entry with a confirmation. */

#include "tuikit/histview.h"
#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

enum { OV_NONE = 0, OV_MENU, OV_CONFIRM, OV_HELP };

enum { K_DESC = 0, K_ENTER, K_REVERT, K_MENU, K_HELP, K_DOWN, K_UP, K_FIRST, K_LAST, K_BACK };

#define VIEW "history"

static const tui_keyentry g_keys[] = {
    { VIEW, TUI_KC_NONE, TUI_VERB_NONE, NULL,
      "The recorded steps, newest first or as the program gives them. Enter or the Revert "
      "entry of the menu asks to take a step back; nothing is changed without a Yes.",
      TUI_ACT_HARMLESS, 0, NULL, 0 },
    { VIEW, TUI_KC_ENTER, TUI_VERB_NONE, "Revert step", "asks first, No is the default",
      TUI_ACT_HARMLESS, 0, NULL, 0 },
    { VIEW, TUI_KC_NONE, TUI_VERB_NONE, "Revert", "undo the selected step",
      TUI_ACT_SERIOUS, 0, "Revert %s?", 0 },
    { VIEW, TUI_KC_MENU, TUI_VERB_NONE, "Action menu", NULL, TUI_ACT_HARMLESS, 0, NULL, 0 },
    { VIEW, TUI_KC_HELP, TUI_VERB_NONE, "Help", NULL, TUI_ACT_HARMLESS, 0, NULL, 0 },
    { VIEW, TUI_KC_DOWN_J, TUI_VERB_NONE, "Down", NULL, TUI_ACT_HARMLESS, 0, NULL, 0 },
    { VIEW, TUI_KC_UP_K, TUI_VERB_NONE, "Up", NULL, TUI_ACT_HARMLESS, 0, NULL, 0 },
    { VIEW, TUI_KC_FIRST, TUI_VERB_NONE, "First step", NULL, TUI_ACT_HARMLESS, 0, NULL, 0 },
    { VIEW, TUI_KC_LAST, TUI_VERB_NONE, "Last step", NULL, TUI_ACT_HARMLESS, 0, NULL, 0 },
    { VIEW, TUI_KC_BACK, TUI_VERB_NONE, "Back", NULL, TUI_ACT_HARMLESS, 0, NULL, 0 },
};
#define N_KEYS ((int)(sizeof g_keys / sizeof g_keys[0]))

/* The columns.  Both fit 80 columns at their minimum widths. */
static const tui_table_col g_cols_object[] = {
    { "Time", 16, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "Who", 6, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "Operation", 9, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "Thing", 8, 2, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "Old value", 9, 1, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "New value", 9, 1, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "Note", 14, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
};
static const tui_table_col g_cols_recent[] = {
    { "Time", 16, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "Who", 6, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "Operation", 9, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "Object", 8, 1, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "Thing", 6, 1, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "Old", 6, 1, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "New", 6, 1, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "Note", 14, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
};

static const char *s(const char *p) { return p ? p : ""; }

static void model_cell(void *ctx, int index, int col, tui_table_cell *c) {
    const tui_histview *h = ctx;
    const tui_histview_row *r = &h->rows[index];
    int recent = h->mode == TUI_HISTVIEW_RECENT;
    const char *text[8] = { s(r->time), s(r->who), s(r->operation), NULL, NULL, NULL, NULL, NULL };
    int k = 3;
    if (recent) text[k++] = s(r->object);
    text[k++] = s(r->thing);
    text[k++] = s(r->old_value);
    text[k++] = s(r->new_value);
    if (col < k) {
        tui_table_cell_put(c, text[col], tui_scheme_style(TUI_ROLE_TEXT));
        return;
    }
    char note[48];
    if (!r->revertable) {
        tui_table_cell_put(c, "not revertable", TUI_STYLE_HINT);
    } else if (r->reverts) {
        snprintf(note, sizeof note, "reverts %llu", (unsigned long long)r->reverts);
        tui_table_cell_put(c, note, TUI_STYLE_ACCENT);
    }
}

static void clear_overlay(tui_histview *h) {
    if (h->overlay == OV_MENU || h->overlay == OV_CONFIRM) tui_actionmenu_free(&h->menu);
    else if (h->overlay == OV_HELP) tui_keyhelp_free(&h->help);
    h->overlay = OV_NONE;
}

static void set_notice(tui_histview *h, int ok, const char *text) {
    size_t j = 0;
    for (size_t i = 0; text[i] && j + 1 < sizeof h->notice; i++) {
        unsigned char ch = (unsigned char)text[i];
        h->notice[j++] = (char)(ch == '\n' || ch == '\r' || ch == '\t' ? ' ' : ch);
    }
    h->notice[j] = '\0';
    h->notice_ok = ok;
}

static void apply_message(tui_histview *h) {
    if (h->failed) tui_table_set_message(&h->table, "could not be read", TUI_STYLE_ERROR);
    else if (h->n == 0) tui_table_set_message(&h->table, "no recorded changes", TUI_STYLE_HINT);
    else tui_table_set_message(&h->table, NULL, TUI_STYLE_DEFAULT);
}

int tui_histview_init(tui_histview *h, tui_histview_mode mode, tui_histview_revert_fn revert,
                     void *ctx) {
    if (!h) {
        errno = EINVAL;
        return -1;
    }
    memset(h, 0, sizeof *h);
    h->mode = mode;
    h->revert = revert;
    h->ctx = ctx;
    if (mode == TUI_HISTVIEW_RECENT)
        tui_table_init(&h->table, g_cols_recent, (int)(sizeof g_cols_recent / sizeof g_cols_recent[0]),
                       &(tui_table_model){ NULL, model_cell, h });
    else
        tui_table_init(&h->table, g_cols_object, (int)(sizeof g_cols_object / sizeof g_cols_object[0]),
                       &(tui_table_model){ NULL, model_cell, h });
    if (tui_keytable_init(&h->keys, g_keys, N_KEYS, NULL, 0) != 0) {
        errno = ENOMEM;
        return -1;
    }
    apply_message(h);
    return 0;
}

void tui_histview_set_rows(tui_histview *h, const tui_histview_row *rows, int n) {
    if (!h) return;
    uint64_t keep = 0;
    int had = tui_histview_selected(h);
    if (had >= 0 && h->rows) keep = h->rows[had].id;
    int sel = had;
    h->rows = rows;
    h->n = n > 0 && rows ? n : 0;
    h->failed = 0;
    tui_table_set_count(&h->table, h->n);
    if (keep)
        for (int i = 0; i < h->n; i++)
            if (rows[i].id == keep) sel = i;
    if (h->n) tui_table_select(&h->table, sel < 0 ? 0 : sel);
    apply_message(h);
}

void tui_histview_set_failed(tui_histview *h, int failed) {
    if (!h) return;
    h->failed = failed != 0;
    apply_message(h);
}

int tui_histview_selected(const tui_histview *h) {
    if (!h || h->failed || h->n == 0) return -1;
    return tui_table_selected(&h->table);
}

const char *tui_histview_notice(const tui_histview *h) {
    return h ? h->notice : "";
}

int tui_histview_modal(const tui_histview *h) {
    return h && h->overlay != OV_NONE;
}

/* What the confirmation names: the step, the thing and what it becomes. */
static void make_target(tui_histview *h, const tui_histview_row *r) {
    char obj[96] = "";
    if (h->mode == TUI_HISTVIEW_RECENT && r->object && r->object[0])
        snprintf(obj, sizeof obj, " of %s", r->object);
    snprintf(h->target, sizeof h->target, "step %llu (%s%s%s%s): %s becomes \"%s\"",
             (unsigned long long)r->id, s(r->operation), r->thing && r->thing[0] ? ", " : "",
             s(r->thing), obj, r->thing && r->thing[0] ? s(r->thing) : "the thing",
             s(r->old_value));
}

/* 1 when a dialog may open for the selected step; else the notice is set. */
static const tui_histview_row *usable(tui_histview *h) {
    int i = tui_histview_selected(h);
    if (i < 0) return NULL;
    const tui_histview_row *r = &h->rows[i];
    if (!r->revertable) {
        char msg[96];
        snprintf(msg, sizeof msg, "step %llu is not revertable", (unsigned long long)r->id);
        set_notice(h, 0, msg);
        return NULL;
    }
    if (!h->revert) {
        set_notice(h, 0, "reverting is not available here");
        return NULL;
    }
    return r;
}

static void do_revert(tui_histview *h, const tui_histview_row *r, tui_histview_state *st) {
    char why[sizeof h->notice] = "";
    if (h->revert(h->ctx, r->id, why, sizeof why) == 0) {
        char msg[64];
        snprintf(msg, sizeof msg, "reverted step %llu", (unsigned long long)r->id);
        set_notice(h, 1, msg);
        *st = TUI_HISTVIEW_REVERTED;
    } else {
        set_notice(h, 0, why[0] ? why : "refused");
    }
}

tui_histview_state tui_histview_handle(tui_histview *h, const tui_event *ev) {
    tui_histview_state st = TUI_HISTVIEW_OPEN;
    if (!h || !ev || ev->type != TUI_EV_KEY) return st;
    if (h->overlay == OV_HELP) {
        if (tui_nav_key(ev, 1) == TUI_NAV_QUIT) return TUI_HISTVIEW_QUIT;
        if (tui_dialog_handle(&h->help.dialog, ev) != TUI_DIALOG_OPEN) clear_overlay(h);
        return st;
    }
    if (h->overlay != OV_NONE) {
        if (tui_nav_key(ev, 1) == TUI_NAV_QUIT) return TUI_HISTVIEW_QUIT;
        tui_action_state as = tui_actionmenu_handle(&h->menu, ev);
        if (as == TUI_ACTION_OPEN) {
            h->overlay = h->menu.confirming ? OV_CONFIRM : OV_MENU;
            return st;
        }
        int chosen = as == TUI_ACTION_CHOSEN;
        clear_overlay(h);
        if (chosen) {
            const tui_histview_row *r = usable(h);
            if (r) do_revert(h, r, &st);
        }
        return st;
    }
    tui_nav nav = tui_nav_key(ev, 1);
    if (nav == TUI_NAV_QUIT) return TUI_HISTVIEW_QUIT;
    if (nav == TUI_NAV_BACK) return TUI_HISTVIEW_BACK;
    h->notice[0] = '\0';
    if (tui_table_handle(&h->table, ev)) return st;
    tui_keycode k = tui_keycode_of(ev);
    if (k == TUI_KC_HELP) {
        if (tui_keyhelp_open(&h->help, &h->keys, VIEW, "History shortcuts") == 0) h->overlay = OV_HELP;
    } else if (k == TUI_KC_MENU || k == TUI_KC_ENTER) {
        const tui_histview_row *r = usable(h);
        if (!r) return st;
        make_target(h, r);
        int ok = k == TUI_KC_MENU ? tui_actionmenu_open(&h->menu, &h->keys, VIEW, h->target)
                                  : tui_actionmenu_run(&h->menu, &h->keys, K_REVERT, h->target);
        if (ok == 0) h->overlay = k == TUI_KC_MENU ? OV_MENU : OV_CONFIRM;
    }
    return st;
}

void tui_histview_render(tui_histview *h, tui_canvas c) {
    if (!h) return;
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    if (rows < 2) return;
    tui_canvas_clear(c, tui_scheme_style(TUI_ROLE_TEXT));
    tui_canvas body = tui_canvas_sub(c, tui_rect_make(0, 0, rows - 1, cols));
    tui_table_render(&h->table, body, tui_scheme_style(TUI_ROLE_TEXT));
    if (h->notice[0])
        tui_canvas_put(c, rows - 1, 0, h->notice, h->notice_ok ? TUI_STYLE_OK : TUI_STYLE_ERROR);
    if (h->overlay == OV_HELP) tui_dialog_render(&h->help.dialog, c);
    else if (h->overlay != OV_NONE) tui_actionmenu_render(&h->menu, c);
}

void tui_histview_free(tui_histview *h) {
    if (!h) return;
    clear_overlay(h);
    tui_table_free(&h->table);
}
