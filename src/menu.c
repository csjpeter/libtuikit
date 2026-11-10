/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-09 */
/* The main menu: numbered entries, counts, the description of the entry under the cursor. */

#include "tuikit/menu.h"
#include "tuikit/text.h"
#include "internal.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

#define LABEL_MAX 128
#define DESC_MAX  512
#define DESC_LINES 3
#define DESC_WIDTH 60      /* widest description line */
#define MENU_GAP_ROWS 3    /* blank rows between the last entry and the description */
#define GAP        2       /* blanks between the columns */
#define PREFIX     5       /* "> 1  " */

typedef struct {
    int       row, col;
    char      text[DESC_MAX + 8];
    tui_style st;
    int       fill_to;     /* the selected row: filled from the block's left to here */
} piece;

typedef struct {
    piece piece[TUI_MENU_MAX * 4 + DESC_LINES];
    int   n;
    int   sel_row, sel_col, sel_w;   /* the cursor row's fill, sel_w 0: none */
    int   rows;                      /* the canvas: a piece below it is not made */
} layout_t;

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

static void add(layout_t *ln, int row, int col, const char *text, tui_style st) {
    if (row >= ln->rows) return;
    piece *p = &ln->piece[ln->n++];
    p->row = row;
    p->col = col;
    snprintf(p->text, sizeof p->text, "%s", text);
    p->st = st;
}

/* The style of a piece on the cursor row: its own colour over the selection's background. */
static tui_style on_selection(tui_style st, tui_style sel) {
    st.bg = sel.bg;
    st.attrs = (uint16_t)(st.attrs | sel.attrs);
    return st;
}

/* "N" or "N (M)"; the two parts separately so that (M) can have its own style. */
static void count_parts(const tui_menu_entry *e, char *n, size_t nsz, char *att, size_t asz) {
    n[0] = att[0] = '\0';
    if (e->count < 0) return;
    snprintf(n, nsz, "%d", e->count);
    if (e->attention > 0) snprintf(att, asz, "(%d)", e->attention);
}

/* Greedy wrap of @p text into at most DESC_LINES lines of @p width columns,
 * the last one cut with an ellipsis; returns the number of lines. */
static int wrap(const char *text, int width, char lines[DESC_LINES][DESC_MAX]) {
    int nl = 0;
    const char *p = text;
    while (*p == ' ') p++;
    while (*p && nl < DESC_LINES) {
        if (nl == DESC_LINES - 1) {   /* the rest goes on the last line */
            tui_str_fit_ellipsis(p, width, lines[nl], DESC_MAX);
            nl++;
            break;
        }
        char cur[DESC_MAX] = "";
        const char *q = p;
        for (;;) {
            const char *e = q;
            while (*e && *e != ' ') e++;
            size_t cl = strlen(cur), wl = (size_t)(e - q);
            char trial[DESC_MAX];
            memcpy(trial, cur, cl);
            if (cl) trial[cl++] = ' ';
            memcpy(trial + cl, q, wl);   /* the text is at most DESC_MAX - 1 bytes: it fits */
            trial[cl + wl] = '\0';
            if (cur[0] && tui_str_width(trial) > width) break;
            memcpy(cur, trial, cl + wl + 1);
            q = e;
            while (*q == ' ') q++;
            if (!*q) break;
        }
        tui_str_fit_ellipsis(cur, width, lines[nl], DESC_MAX);
        nl++;
        p = q;
    }
    return nl;
}

static void layout(const tui_menu *m, int rows, int cols, layout_t *ln) {
    ln->n = 0;
    ln->sel_w = 0;
    ln->rows = rows;
    if (rows <= 0 || cols <= 0 || m->n <= 0) return;

    int lw = 0, cw = 0, nmax = 0, amax = 0;
    char cn[16], ca[16];
    char lab[TUI_MENU_MAX][LABEL_MAX];
    for (int i = 0; i < m->n; i++) {
        clean(lab[i], LABEL_MAX, m->entries[i].label);
        int w = tui_str_width(lab[i]);
        if (w > lw) lw = w;
        count_parts(&m->entries[i], cn, sizeof cn, ca, sizeof ca);
        if (tui_str_width(cn) > nmax) nmax = tui_str_width(cn);
        if (tui_str_width(ca) > amax) amax = tui_str_width(ca);
    }
    /* N is right-aligned in its column, so that the (M) parts line up. */
    cw = nmax + (amax > 0 ? 1 + amax : 0);
    int room = cols - PREFIX - (cw > 0 ? GAP + cw : 0);
    if (room < 1) room = 1;
    if (lw > room) lw = room;
    int blockw = PREFIX + lw + (cw > 0 ? GAP + cw : 0);

    /* The block and its description together are centred. */
    /* The block is sized for the longest description (DESC_LINES), so the entries keep
     * their rows whatever the cursor is on; on a short canvas the gap shrinks to what
     * fits, never below one row, and the rows that do not fit are the description's. */
    int gap = rows - m->n - DESC_LINES;
    if (gap > MENU_GAP_ROWS) gap = MENU_GAP_ROWS;
    if (gap < 1) gap = 1;
    int total = m->n + gap + DESC_LINES;
    int top = (rows - total) / 2;
    if (top < 0) top = 0;
    int left = (cols - blockw) / 2;
    if (left < 0) left = 0;

    tui_style text = tui_scheme_style(TUI_ROLE_TEXT);
    tui_style warn = tui__warning_style();
    tui_style sel = tui_selection_style();
    for (int i = 0; i < m->n; i++) {
        int row = top + i;
        int cur = i == m->selected;
        tui_style t = cur ? on_selection(text, sel) : text;
        tui_style w = cur ? on_selection(warn, sel) : warn;
        char key[16], fit[LABEL_MAX];
        snprintf(key, sizeof key, "%c %d", cur ? '>' : ' ', i + 1);
        add(ln, row, left, key, t);
        tui_str_fit_ellipsis(lab[i], lw, fit, sizeof fit);
        add(ln, row, left + PREFIX, fit, t);
        count_parts(&m->entries[i], cn, sizeof cn, ca, sizeof ca);
        int ccol = left + PREFIX + lw + GAP;
        if (cn[0]) add(ln, row, ccol + nmax - tui_str_width(cn), cn, t);
        if (ca[0]) add(ln, row, ccol + nmax + 1, ca, w);
        if (cur) {
            ln->sel_row = row;
            ln->sel_col = left;
            ln->sel_w = blockw;
        }
    }

    const char *d = m->entries[m->selected].description;
    if (d && d[0]) {
        char clean_d[DESC_MAX];
        clean(clean_d, sizeof clean_d, d);
        int dw = cols - 2 < DESC_WIDTH ? cols - 2 : DESC_WIDTH;
        if (dw >= 1) {
            char lines[DESC_LINES][DESC_MAX];
            int nl = wrap(clean_d, dw, lines);
            for (int i = 0; i < nl; i++) {
                int w = tui_str_width(lines[i]);
                add(ln, top + m->n + gap + i, (cols - w) / 2, lines[i], text);
            }
        }
    }
}

/* ── The model ─────────────────────────────────────────────────────── */

void tui_menu_init(tui_menu *m) {
    if (m) memset(m, 0, sizeof *m);
}

int tui_menu_set(tui_menu *m, const tui_menu_entry *entries, int n, int selected) {
    if (!m || n < 0 || n > TUI_MENU_MAX || (n > 0 && !entries)) { errno = EINVAL; return -1; }
    for (int i = 0; i < n; i++)
        if (!entries[i].label) { errno = EINVAL; return -1; }
    m->entries = entries;
    m->n = n;
    m->selected = 0;
    tui_menu_select(m, selected);
    return 0;
}

void tui_menu_select(tui_menu *m, int i) {
    if (!m) return;
    if (i >= m->n) i = m->n - 1;
    if (i < 0) i = 0;
    m->selected = i;
}

int tui_menu_selected(const tui_menu *m) {
    return m ? m->selected : 0;
}

int tui_menu_count(const tui_menu *m) {
    return m ? m->n : 0;
}

tui_menu_result tui_menu_handle(tui_menu *m, const tui_event *ev) {
    if (!m || !ev || ev->type != TUI_EV_KEY || m->n <= 0) return TUI_MENU_IGNORED;
    int to = m->selected;
    if (ev->key == TUI_KEY_ENTER) return TUI_MENU_ACTIVATED;
    if (ev->key == TUI_KEY_UP) to--;
    else if (ev->key == TUI_KEY_DOWN) to++;
    else if (ev->key == TUI_KEY_HOME) to = 0;
    else if (ev->key == TUI_KEY_END) to = m->n - 1;
    else if (ev->key == TUI_KEY_CHAR && ev->mods == 0 && ev->cp == 'k') to--;
    else if (ev->key == TUI_KEY_CHAR && ev->mods == 0 && ev->cp == 'j') to++;
    else if (ev->key == TUI_KEY_CHAR && ev->mods == 0 && ev->cp >= '1' && ev->cp <= '9') {
        int d = (int)(ev->cp - '1');
        if (d >= m->n) return TUI_MENU_IGNORED;
        m->selected = d;
        return TUI_MENU_ACTIVATED;
    } else {
        return TUI_MENU_IGNORED;
    }
    if (to < 0 || to >= m->n || to == m->selected) return TUI_MENU_IGNORED;
    m->selected = to;
    return TUI_MENU_MOVED;
}

/* ── Drawing and plain text ────────────────────────────────────────── */

void tui_menu_render(const tui_menu *m, tui_canvas c) {
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    if (!m || rows <= 0 || cols <= 0) return;
    layout_t ln;
    layout(m, rows, cols, &ln);
    if (ln.sel_w > 0)
        tui_canvas_fill(c, tui_rect_make(ln.sel_row, ln.sel_col, 1, ln.sel_w), tui_selection_style());
    for (int i = 0; i < ln.n; i++)
        tui_canvas_put(c, ln.piece[i].row, ln.piece[i].col, ln.piece[i].text, ln.piece[i].st);
}

/* The length of @p out[0..n) without a multi-byte character cut at its end. */
static size_t whole_chars(const char *out, size_t n) {
    size_t i = n;
    while (i > 0 && ((unsigned char)out[i - 1] & 0xC0) == 0x80) i--;
    if (i > 0 && (unsigned char)out[i - 1] >= 0xC0) {
        unsigned char lead = (unsigned char)out[i - 1];
        size_t need = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
        if (n - (i - 1) < need) return i - 1;
    }
    return n;
}

int tui_menu_format(const tui_menu *m, int rows, int cols, char *out, size_t outsz) {
    if (!m || !out || outsz == 0) return -1;
    layout_t ln;
    layout(m, rows, cols, &ln);
    size_t n = 0;
    int rc = 0, row = 0, col = 0;
    int last = ln.n ? ln.piece[ln.n - 1].row : -1;
#define PUT(ch) do { if (n + 2 > outsz) rc = -1; else out[n++] = (ch); } while (0)
    for (int i = 0; i < ln.n && rc == 0; i++) {
        const piece *p = &ln.piece[i];
        while (row < p->row) { PUT('\n'); row++; col = 0; }
        for (; col < p->col && rc == 0; col++) PUT(' ');
        for (const char *s = p->text; *s && rc == 0; s++) PUT(*s);
        col = p->col + tui_str_width(p->text);
    }
    if (rc == 0 && last >= 0) PUT('\n');
#undef PUT
    if (rc != 0) n = whole_chars(out, n);
    out[n] = '\0';
    return rc == 0 ? (int)n : -1;
}
