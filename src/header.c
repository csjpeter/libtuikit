/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-09 */
/* The header bar: left text, notice area, program name; clipping order left, notice, never the name. */

#include "tuikit/header.h"
#include "tuikit/text.h"

#include <string.h>

#define LEAD 1   /* a blank before the left text and after the name */
#define GAP  2   /* blanks between the left text and the notice */

typedef struct {
    int       col;
    char      text[512];
    tui_style st;
} piece;

typedef struct {
    piece piece[3];
    int   n;
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

static void add(line *ln, int col, const char *src, int width, tui_style st) {
    piece *p = &ln->piece[ln->n++];
    p->col = col;
    p->st = st;
    tui_str_fit_ellipsis(src, width, p->text, sizeof(p->text));
}

static void layout(const tui_header *h, int cols, line *ln) {
    ln->n = 0;
    if (cols <= 2 * LEAD) return;

    /* The name first: it is never cut, unless alone wider than the row. */
    int rw = h->right[0] ? tui_str_width(h->right) : 0;
    if (rw > cols - 2 * LEAD) rw = cols - 2 * LEAD;
    int avail = cols - 2 * LEAD - (rw > 0 ? rw + 1 : 0);

    int lw = h->left[0] ? tui_str_width(h->left) : 0;
    int nw = h->notice[0] ? tui_str_width(h->notice) : 0;
    int ncol = LEAD;
    if (avail > 0) {
        if (nw > 0) {
            int need = nw + GAP;
            if (lw + need > avail) lw = avail - need;   /* the left side gives way first */
            if (lw < 1) { lw = 0; need = nw; }
            if (nw > avail) nw = avail;
            if (lw > 0) add(ln, LEAD, h->left, lw, h->bar);
            ncol = LEAD + (lw > 0 ? lw + GAP : 0);
            if (ncol + nw > LEAD + avail) nw = LEAD + avail - ncol;
            if (nw > 0) add(ln, ncol, h->notice, nw, h->notice_style);
        } else if (lw > 0) {
            add(ln, LEAD, h->left, lw > avail ? avail : lw, h->bar);
        }
    }
    if (rw > 0) add(ln, cols - LEAD - rw, h->right, rw, h->bar);
}

/* The notice on the bar: the role's colour becomes the background, so that a
 * red or green notice reads on the blue bar; the role's attributes stay.  A
 * role without a colour (plain information) is the bar plus its attributes. */
static tui_style on_bar(const tui_header *h, tui_style role) {
    tui_style st = h->bar;
    if (role.fg != TUI_COLOR_DEFAULT) st.bg = role.fg;
    st.attrs = (uint16_t)(st.attrs | role.attrs);
    return st;
}

void tui_header_init(tui_header *h) {
    if (!h) return;
    memset(h, 0, sizeof(*h));
    h->bar = tui_scheme_style(TUI_ROLE_HEADER);
    h->notice_style = h->bar;
}

void tui_header_set_left(tui_header *h, const char *text) {
    if (h) clean(h->left, sizeof(h->left), text ? text : "");
}

void tui_header_set_right(tui_header *h, const char *text) {
    if (h) clean(h->right, sizeof(h->right), text ? text : "");
}

void tui_header_set_notice(tui_header *h, const char *text, tui_style style) {
    if (!h || h->standing) return;
    clean(h->notice, sizeof(h->notice), text ? text : "");
    h->notice_style = on_bar(h, style);
}

void tui_header_set_warning(tui_header *h, const char *text) {
    if (!h) return;
    clean(h->notice, sizeof(h->notice), text ? text : "");
    h->notice_style = on_bar(h, tui_scheme_style(TUI_ROLE_ERROR));
    h->standing = h->notice[0] != '\0';
}

void tui_header_clear_warning(tui_header *h) {
    if (!h || !h->standing) return;
    h->notice[0] = '\0';
    h->standing = 0;
}

const char *tui_header_notice(const tui_header *h) {
    return h ? h->notice : "";
}

int tui_header_warning_active(const tui_header *h) {
    return h && h->standing;
}

tui_header_result tui_header_handle(tui_header *h, const tui_event *ev) {
    if (!h || !ev || h->standing || !h->notice[0]) return TUI_HEADER_IGNORED;
    if (ev->type != TUI_EV_KEY && ev->type != TUI_EV_PASTE) return TUI_HEADER_IGNORED;
    h->notice[0] = '\0';
    return TUI_HEADER_CHANGED;
}

void tui_header_render(tui_header *h, tui_canvas c) {
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    if (!h || rows <= 0 || cols <= 0) return;
    line ln;
    layout(h, cols, &ln);
    tui_canvas_fill(c, tui_rect_make(0, 0, 1, cols), h->bar);
    for (int i = 0; i < ln.n; i++) {
        tui_canvas_put(c, 0, ln.piece[i].col, ln.piece[i].text, ln.piece[i].st);
    }
}

int tui_header_format(const tui_header *h, int cols, char *out, size_t outsz) {
    if (!h || !out || outsz == 0) return -1;
    line ln;
    layout(h, cols, &ln);
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
    out[n] = '\0';
    return rc == 0 ? (int)n : -1;
}
