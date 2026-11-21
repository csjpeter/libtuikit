/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-21 */
/* The help page and the view description: a view registers once, every program gets the rule. */

#include "tuikit/viewdoc.h"
#include "tuikit/text.h"
#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REMINDER "Press d: what this view is and what each column means."

static const char *nz(const char *s) { return s ? s : ""; }

static int valid(const tui_viewdoc *v) {
    return v && (v->ncols == 0 || v->cols) && v->ncols >= 0 && (v->nkeys == 0 || v->keys) &&
           v->nkeys >= 0;
}

int tui_viewdoc_text(const tui_viewdoc *v, char *out, size_t size) {
    if (!valid(v) || (!out && size > 0)) {
        errno = EINVAL;
        return -1;
    }
    int w = 0;
    for (int i = 0; i < v->ncols; i++) {
        int tw = tui_str_width(nz(v->cols[i].title));
        if (tw > w) w = tw;
    }
    size_t need = 0;
    char *o = out;
    size_t left = size;
#define PUT(...) do { \
        int n_ = snprintf(o, left, __VA_ARGS__); \
        need += (size_t)n_; \
        size_t adv_ = (size_t)n_ < left ? (size_t)n_ : (left ? left - 1 : 0); \
        if (o) o += adv_; \
        left -= adv_; \
    } while (0)
    PUT("%s", nz(v->statement));
    if (v->ncols > 0) PUT("\n");
    for (int i = 0; i < v->ncols; i++) {
        const char *t = nz(v->cols[i].title);
        PUT("\n%s%*s  %s", t, w - tui_str_width(t), "", nz(v->cols[i].text));
    }
#undef PUT
    return (int)need;
}

int tui_viewdoc_open(tui_viewdoc_page *p, const tui_viewdoc *v, const char *const *common,
                     int ncommon) {
    if (!p || !valid(v) || ncommon < 0 || (ncommon > 0 && !common)) {
        errno = EINVAL;
        return -1;
    }
    memset(p, 0, sizeof *p);
    p->view = v;
    p->state = TUI_DIALOG_OPEN;
    /* "This view" and "Every view" head the two groups. */
    int n = 2 + v->nkeys + ncommon;
    p->pairs = calloc((size_t)n * 2, sizeof *p->pairs);
    if (!p->pairs) {
        errno = ENOMEM;
        return -1;
    }
    int k = 0;
    p->pairs[k++] = "This view";
    p->pairs[k++] = "";
    for (int i = 0; i < 2 * v->nkeys; i++) p->pairs[k++] = v->keys[i];
    p->pairs[k++] = "Every view";
    p->pairs[k++] = "";
    for (int i = 0; i < 2 * ncommon; i++) p->pairs[k++] = common[i];
    char title[96];
    snprintf(title, sizeof title, "%s help", nz(v->name));
    if (tui_dialog_help(&p->help, title, p->pairs, n) != 0 ||
        tui__dialog_help_text(&p->help, REMINDER) != 0) {
        tui_viewdoc_free(p);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* The description opens in a dialog of its own. */
static int open_desc(tui_viewdoc_page *p) {
    int len = tui_viewdoc_text(p->view, NULL, 0);
    char *text = len < 0 ? NULL : malloc((size_t)len + 1);
    if (!text) return -1;
    tui_viewdoc_text(p->view, text, (size_t)len + 1);
    char title[96];
    snprintf(title, sizeof title, "%s view", nz(p->view->name));
    int r = tui_dialog_text(&p->desc, title, text);
    free(text);
    if (r != 0) return -1;
    p->in_desc = 1;
    return 0;
}

tui_dialog_state tui_viewdoc_handle(tui_viewdoc_page *p, const tui_event *ev) {
    if (!p || !ev) return TUI_DIALOG_CANCELLED;
    if (p->state != TUI_DIALOG_OPEN) return p->state;
    if (p->in_desc) {
        if (tui_dialog_handle(&p->desc, ev) != TUI_DIALOG_OPEN) p->state = TUI_DIALOG_OK;
        return p->state;
    }
    /* answer is 1 while the search line of the page has the keyboard: a d is then text. */
    if (TUI_EV_IS_CHAR(ev, 'd') && p->help.answer != 1) {
        open_desc(p);   /* on failure the page stays as it is */
        return p->state;
    }
    if (tui_dialog_handle(&p->help, ev) != TUI_DIALOG_OPEN) p->state = TUI_DIALOG_OK;
    return p->state;
}

void tui_viewdoc_render(tui_viewdoc_page *p, tui_canvas c) {
    if (!p) return;
    tui_dialog_render(p->in_desc ? &p->desc : &p->help, c);
}

void tui_viewdoc_free(tui_viewdoc_page *p) {
    if (!p) return;
    tui_dialog_free(&p->help);
    tui_dialog_free(&p->desc);
    free(p->pairs);
    memset(p, 0, sizeof *p);
}

int tui_viewdoc_check(const tui_viewdoc *v, const tui_table_col *cols, int ncols, char *msg,
                      size_t size) {
    if (!valid(v) || ncols < 0 || (ncols > 0 && !cols)) {
        errno = EINVAL;
        return -1;
    }
    if (msg && size) msg[0] = '\0';
    int j = 0;
    for (int i = 0; i < ncols; i++) {
        if (!cols[i].title || !cols[i].title[0]) continue;
        if (j >= v->ncols) {
            if (msg) snprintf(msg, size, "column \"%s\" is not described", cols[i].title);
            return -1;
        }
        if (strcmp(cols[i].title, nz(v->cols[j].title)) != 0) {
            if (msg)
                snprintf(msg, size, "column \"%s\" is described as \"%s\"", cols[i].title,
                         nz(v->cols[j].title));
            return -1;
        }
        j++;
    }
    if (j < v->ncols) {
        if (msg) snprintf(msg, size, "description of column \"%s\" has no column", nz(v->cols[j].title));
        return -1;
    }
    return 0;
}
