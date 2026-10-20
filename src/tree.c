/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-20 */
/* The tree: a list whose rows open and close, over the list view. */

#include "tuikit/tree.h"
#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── The open set: ids packed one after another, each NUL-terminated ── */

static const char *set_find(const tui_treeset *s, const char *id) {
    size_t n = strlen(id) + 1;
    for (size_t at = 0; at < s->len;) {
        size_t l = strlen(s->data + at) + 1;
        if (l == n && memcmp(s->data + at, id, n) == 0) return s->data + at;
        at += l;
    }
    return NULL;
}

static int set_add(tui_treeset *s, const char *id) {
    if (set_find(s, id)) return 0;
    size_t n = strlen(id) + 1;
    if (s->len + n > s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 64;
        while (cap < s->len + n) cap *= 2;
        char *p = realloc(s->data, cap);
        if (!p) {
            errno = ENOMEM;
            return -1;
        }
        s->data = p;
        s->cap = cap;
    }
    memcpy(s->data + s->len, id, n);
    s->len += n;
    return 0;
}

static void set_remove(tui_treeset *s, const char *id) {
    char *at = (char *)set_find(s, id);
    if (!at) return;
    size_t n = strlen(at) + 1;
    memmove(at, at + n, s->len - (size_t)(at + n - s->data));
    s->len -= n;
}

/* dst = a copy of src; on failure dst is unchanged. */
static int set_copy(tui_treeset *dst, const tui_treeset *src) {
    if (src->len > dst->cap) {
        char *p = realloc(dst->data, src->len);
        if (!p) {
            errno = ENOMEM;
            return -1;
        }
        dst->data = p;
        dst->cap = src->len;
    }
    if (src->len) memcpy(dst->data, src->data, src->len);
    dst->len = src->len;
    return 0;
}

/* open = saved.  Cannot fail: saved was copied from open, whose capacity
 * never shrinks, so it holds saved.len bytes. */
static void set_restore(tui_treeset *open, const tui_treeset *saved) {
    if (saved->len) memcpy(open->data, saved->data, saved->len);
    open->len = saved->len;
}

/* ── State ───────────────────────────────────────────────────────────── */

void tui_tree_init(tui_tree *t) {
    if (!t) return;
    memset(t, 0, sizeof(*t));
    tui_listview_init(&t->list);
}

void tui_tree_free(tui_tree *t) {
    if (!t) return;
    free(t->visible);
    free(t->open.data);
    free(t->saved.data);
    tui_tree_init(t);
}

static int has_children(const tui_tree *t, int row) {
    return t->rows[row].children > 0;
}

static int row_open(const tui_tree *t, int row) {
    return has_children(t, row) && set_find(&t->open, t->rows[row].id) != NULL;
}

/* The item showing @p row, or -1 when it is hidden. */
static int item_of(const tui_tree *t, int row) {
    for (int i = 0; i < t->list.count; i++)
        if (t->visible[i] == row) return i;
    return -1;
}

/* Recomputes the visible rows and puts the selection on @p keep (or on its
 * nearest visible ancestor); -1: keep the item number, clamped. */
static void rebuild(tui_tree *t, int keep) {
    int skip = -1; /* rows deeper than this are hidden; -1: none */
    int n = 0;
    for (int i = 0; i < t->nrows; i++) {
        if (skip >= 0 && t->rows[i].depth > skip) continue;
        skip = -1;
        t->visible[n++] = i;
        if (has_children(t, i) && !row_open(t, i)) skip = t->rows[i].depth;
    }
    int item = t->list.selected;
    tui_listview_set_count(&t->list, n);
    for (int r = keep; r >= 0; r = t->parent[r]) {
        int at = item_of(t, r);
        if (at >= 0) {
            item = at;
            break;
        }
    }
    tui_listview_select(&t->list, item);
}

int tui_tree_set_rows(tui_tree *t, const tui_treerow *rows, int n) {
    if (!t || n < 0 || (n > 0 && !rows)) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < n; i++) {
        int prev = i ? rows[i - 1].depth : -1;
        if (!rows[i].id || rows[i].depth < 0 || rows[i].depth > prev + 1) {
            errno = EINVAL;
            return -1;
        }
    }
    int *mem = malloc((size_t)(n ? 2 * n : 1) * sizeof(int));
    if (!mem) {
        errno = ENOMEM;
        return -1;
    }
    /* The selection by identity, while the old rows are still valid. */
    int sel_row = tui_tree_selected_row(t);
    const char *sel_id = sel_row >= 0 ? t->rows[sel_row].id : NULL;
    int keep = -1;
    int *vis = mem, *par = mem + n;
    for (int i = 0; i < n; i++) { /* vis doubles as the last row seen at each depth */
        int d = rows[i].depth;
        par[i] = d ? vis[d - 1] : -1;
        vis[d] = i;
        if (sel_id && keep < 0 && strcmp(rows[i].id, sel_id) == 0) keep = i;
    }
    free(t->visible);
    t->visible = vis;
    t->parent = par;
    t->rows = rows;
    t->nrows = n;
    t->list.count = 0; /* the old items are gone; rebuild() re-counts */
    rebuild(t, keep);
    return 0;
}

int tui_tree_row_at(const tui_tree *t, int item) {
    if (!t || item < 0 || item >= t->list.count) return -1;
    return t->visible[item];
}

int tui_tree_selected_row(const tui_tree *t) {
    return t ? tui_tree_row_at(t, t->list.selected) : -1;
}

int tui_tree_is_open(const tui_tree *t, int row) {
    return t && row >= 0 && row < t->nrows && row_open(t, row);
}

int tui_tree_set_open(tui_tree *t, int row, int open) {
    if (!t || row < 0 || row >= t->nrows) {
        errno = EINVAL;
        return -1;
    }
    if (!has_children(t, row)) return 0;
    if (open) {
        if (set_add(&t->open, t->rows[row].id) != 0) return -1;
    } else {
        set_remove(&t->open, t->rows[row].id);
    }
    rebuild(t, tui_tree_selected_row(t));
    return 0;
}

int tui_tree_open_all(tui_tree *t) {
    if (!t) {
        errno = EINVAL;
        return -1;
    }
    for (int i = 0; i < t->nrows; i++)
        if (has_children(t, i) && set_add(&t->open, t->rows[i].id) != 0) return -1;
    rebuild(t, tui_tree_selected_row(t));
    return 0;
}

void tui_tree_close_all(tui_tree *t) {
    if (!t) return;
    t->open.len = 0;
    rebuild(t, tui_tree_selected_row(t));
}

/* Opens the branches above @p row. */
static int open_above(tui_tree *t, int row) {
    for (int p = t->parent[row]; p >= 0; p = t->parent[p])
        if (set_add(&t->open, t->rows[p].id) != 0) return -1;
    return 0;
}

int tui_tree_select_row(tui_tree *t, int row) {
    if (!t || row < 0 || row >= t->nrows) {
        errno = EINVAL;
        return -1;
    }
    int rc = open_above(t, row);
    rebuild(t, row); /* even after a failure: the ancestors that did open show */
    return rc;
}

int tui_tree_show_matches(tui_tree *t, const unsigned char *match) {
    if (!t) {
        errno = EINVAL;
        return -1;
    }
    int keep = tui_tree_selected_row(t);
    if (!match) {
        if (t->filtering) {
            set_restore(&t->open, &t->saved);
            t->filtering = 0;
            rebuild(t, keep);
        }
        return 0;
    }
    if (!t->filtering) {
        if (set_copy(&t->saved, &t->open) != 0) return -1;
        t->filtering = 1;
    } else {
        set_restore(&t->open, &t->saved);
    }
    int rc = 0;
    for (int i = 0; i < t->nrows && rc == 0; i++)
        if (match[i]) rc = open_above(t, i);
    rebuild(t, keep);
    return rc;
}

void tui_tree_show_counts(tui_tree *t, int on) {
    if (t) t->counts = on != 0;
}

const char *tui_tree_marker(const tui_tree *t, int row) {
    if (!t || row < 0 || row >= t->nrows || !has_children(t, row)) return "  ";
    return row_open(t, row) ? "- " : "+ ";
}

/* ── Keys ────────────────────────────────────────────────────────────── */

static int plain_key(const tui_event *ev, tui_key key, uint32_t ch) {
    return ev->type == TUI_EV_KEY && ev->mods == 0 &&
           (ev->key == key || (ev->key == TUI_KEY_CHAR && ev->cp == ch));
}

static void key_open(tui_tree *t, int row) {
    if (!has_children(t, row)) return;
    if (!row_open(t, row)) {
        tui_tree_set_open(t, row, 1); /* ENOMEM: stays closed */
    } else if (t->list.selected + 1 < t->list.count) {
        tui_listview_move_down(&t->list); /* the next item is the first child */
    }
}

static void key_close(tui_tree *t, int row) {
    if (row_open(t, row)) {
        tui_tree_set_open(t, row, 0);
        return;
    }
    int p = t->parent[row];
    if (p >= 0) tui_listview_select(&t->list, item_of(t, p));
}

int tui_tree_handle(tui_tree *t, const tui_event *ev) {
    if (!t || !ev) return 0;
    int row = tui_tree_selected_row(t);
    if (plain_key(ev, TUI_KEY_RIGHT, 'l')) {
        if (row >= 0) key_open(t, row);
        return 1;
    }
    if (plain_key(ev, TUI_KEY_LEFT, 'h')) {
        if (row >= 0) key_close(t, row);
        return 1;
    }
    return tui_listview_handle(&t->list, ev);
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

typedef struct {
    tui_tree        *t;
    tui_style        blank, selected;
    tui_tree_draw_fn draw;
    void            *ctx;
} draw_ctx;

static void draw_item(void *p, tui_canvas row, int item, int selected) {
    draw_ctx *d = p;
    const tui_tree *t = d->t;
    int r = t->visible[item];
    tui_style st = selected ? d->selected : d->blank;
    tui_canvas_clear(row, st);
    tui_canvas_put(row, 0, 0, tui_tree_marker(t, r), st);
    int indent = 2 + 2 * t->rows[r].depth;
    int cols = tui_canvas_cols(row);
    if (indent >= cols) return;
    tui_canvas label = tui_canvas_sub(row, tui_rect_make(0, indent, 1, cols - indent));
    int used = d->draw ? d->draw(d->ctx, label, r, selected, st) : 0;
    if (used < 0) used = 0;
    if (t->counts && has_children(t, r) && !row_open(t, r)) {
        char cnt[32];
        snprintf(cnt, sizeof cnt, " (%d)", t->rows[r].children);
        tui_canvas_put(label, 0, used, cnt, st);
    }
}

void tui_tree_render(tui_tree *t, tui_canvas c, tui_style blank, tui_style selected,
                     tui_tree_draw_fn draw, void *ctx) {
    if (!t) return;
    draw_ctx d = { t, blank, selected, draw, ctx };
    tui_listview_render(&t->list, c, blank, draw_item, &d);
}
