/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-24 */
/* The tab row: labels on a row, the active one bold and underlined, scrolled into view. */

#include "tuikit/tabrow.h"
#include "tuikit/text.h"

#include <errno.h>
#include <string.h>

#define LABEL_MAX 256
#define LEFT_MORE  "\xE2\x80\xB9"   /* U+2039 */
#define RIGHT_MORE "\xE2\x80\xBA"   /* U+203A */

typedef struct {
    int       col;
    char      text[LABEL_MAX * 2];
    tui_style st;
} piece;

typedef struct {
    piece piece[TUI_TABROW_MAX + 2];
    int   n;
    int   first;
} line;

static void clean(char *dst, size_t size, const char *src) {
    size_t len = strlen(src);
    if (len > size - 1) {
        len = size - 1;
        while (len > 0 && ((unsigned char)src[len] & 0xC0) == 0x80) len--;
    }
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)src[i];
        dst[i] = (c < 0x20 || c == 0x7F) ? ' ' : (char)c;
    }
    dst[len] = '\0';
}

/* " label ", controls blanked. */
static void tab_text(const tui_tabrow *tr, int i, char *out) {
    char lab[LABEL_MAX];
    clean(lab, sizeof lab, tr->labels[i]);
    out[0] = ' ';
    strcpy(out + 1, lab);
    strcat(out, " ");
}

static void add(line *ln, int col, const char *text, tui_style st) {
    piece *p = &ln->piece[ln->n++];
    p->col = col;
    strcpy(p->text, text);
    p->st = st;
}

static tui_style active_style(void) {
    tui_style st = tui_scheme_style(TUI_ROLE_TAB_ACTIVE);
    st.attrs = (uint16_t)(st.attrs | TUI_ATTR_BOLD | TUI_ATTR_UNDERLINE);
    return st;
}

/* Lays the row out for @p cols columns, starting from the window @p first. */
static void layout(const tui_tabrow *tr, int cols, int first, line *ln) {
    char t[LABEL_MAX * 2];
    int w[TUI_TABROW_MAX];
    int total = 0;
    ln->n = 0;
    ln->first = 0;
    if (cols <= 0 || tr->n <= 0) return;
    for (int i = 0; i < tr->n; i++) {
        tab_text(tr, i, t);
        w[i] = tui_str_width(t);
        total += w[i] + (i > 0 ? 1 : 0);
    }
    tui_style act = active_style();
    tui_style inact = tui_scheme_style(TUI_ROLE_TAB_INACTIVE);

    if (total <= cols) {
        int col = 0;
        for (int i = 0; i < tr->n; i++) {
            tab_text(tr, i, t);
            add(ln, col, t, i == tr->active ? act : inact);
            col += w[i] + 1;
        }
        return;
    }

    /* Too wide: the window is columns 1 .. cols-2, the ends are the marks. */
    int area = cols - 2;
    if (area < 1) return;
    if (first < 0 || first >= tr->n) first = 0;
    if (tr->active < first) first = tr->active;
    for (;;) {
        int end = 0;
        for (int i = first; i <= tr->active; i++) end += w[i] + (i > first ? 1 : 0);
        if (end <= area || first >= tr->active) break;
        first++;
    }
    ln->first = first;

    if (first > 0) add(ln, 0, LEFT_MORE, inact);
    int col = 1, last = first - 1;
    for (int i = first; i < tr->n; i++) {
        int room = cols - 1 - col;
        if (room <= 0) break;
        tab_text(tr, i, t);
        if (w[i] > room) {
            if (i != tr->active) break;   /* only the active one is cut */
            char cut[LABEL_MAX * 2];
            tui_str_fit_ellipsis(t, room, cut, sizeof cut);
            add(ln, col, cut, act);
            last = i;
            break;
        }
        add(ln, col, t, i == tr->active ? act : inact);
        col += w[i] + 1;
        last = i;
    }
    if (last < tr->n - 1) add(ln, cols - 1, RIGHT_MORE, inact);
}

/* ── The model ─────────────────────────────────────────────────────── */

void tui_tabrow_init(tui_tabrow *tr) {
    if (tr) memset(tr, 0, sizeof *tr);
}

int tui_tabrow_set(tui_tabrow *tr, const char *const *labels, int n, int active) {
    if (!tr || n < 0 || n > TUI_TABROW_MAX || (n > 0 && !labels)) { errno = EINVAL; return -1; }
    for (int i = 0; i < n; i++)
        if (!labels[i]) { errno = EINVAL; return -1; }
    tr->labels = labels;
    tr->n = n;
    tr->first = 0;
    tr->active = 0;
    tui_tabrow_set_active(tr, active);
    return 0;
}

void tui_tabrow_set_active(tui_tabrow *tr, int i) {
    if (!tr) return;
    if (i >= tr->n) i = tr->n - 1;
    if (i < 0) i = 0;
    tr->active = i;
}

int tui_tabrow_active(const tui_tabrow *tr) {
    return tr ? tr->active : 0;
}

int tui_tabrow_count(const tui_tabrow *tr) {
    return tr ? tr->n : 0;
}

tui_tabrow_result tui_tabrow_handle(tui_tabrow *tr, const tui_event *ev) {
    if (!tr || !ev || ev->type != TUI_EV_KEY || tr->n <= 0) return TUI_TABROW_IGNORED;
    int to = tr->active;
    if (ev->key == TUI_KEY_TAB) {
        to = (ev->mods & TUI_MOD_SHIFT) ? (tr->active + tr->n - 1) % tr->n
                                        : (tr->active + 1) % tr->n;
    } else if (ev->key == TUI_KEY_CHAR && ev->mods == 0 && ev->cp >= '1' && ev->cp <= '9') {
        int d = (int)(ev->cp - '1');
        if (d >= tr->n) return TUI_TABROW_IGNORED;
        to = d;
    } else {
        return TUI_TABROW_IGNORED;
    }
    if (to == tr->active) return TUI_TABROW_IGNORED;
    tr->active = to;
    return TUI_TABROW_CHANGED;
}

/* ── Drawing and plain text ────────────────────────────────────────── */

void tui_tabrow_render(tui_tabrow *tr, tui_canvas c) {
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    if (!tr || rows <= 0 || cols <= 0) return;
    line ln;
    layout(tr, cols, tr->first, &ln);
    tr->first = ln.first;
    tui_canvas_fill(c, tui_rect_make(0, 0, 1, cols), tui_scheme_style(TUI_ROLE_TAB_INACTIVE));
    for (int i = 0; i < ln.n; i++)
        tui_canvas_put(c, 0, ln.piece[i].col, ln.piece[i].text, ln.piece[i].st);
}

int tui_tabrow_format(const tui_tabrow *tr, int cols, char *out, size_t outsz) {
    if (!tr || !out || outsz == 0) return -1;
    line ln;
    layout(tr, cols, tr->first, &ln);
    size_t n = 0;
    int col = 0, rc = 0;
    for (int i = 0; i < ln.n && rc == 0; i++) {
        const piece *p = &ln.piece[i];
        size_t pad = p->col > col ? (size_t)(p->col - col) : 0;
        size_t need = pad + strlen(p->text);
        if (n + need + 1 > outsz) {
            rc = -1;
            need = outsz - 1 - n;
            if (pad > need) pad = need;
        }
        memset(out + n, ' ', pad);
        n += pad;
        size_t room = need - pad;
        while (rc != 0 && room > 0 && ((unsigned char)p->text[room] & 0xC0) == 0x80) room--;
        if (room > 0) memcpy(out + n, p->text, room);
        n += room;
        col = p->col + tui_str_width(p->text);
    }
    while (n > 0 && out[n - 1] == ' ') n--;   /* the last tab's own blank */
    out[n] = '\0';
    return rc == 0 ? (int)n : -1;
}
