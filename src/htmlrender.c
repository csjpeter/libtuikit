/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* The HTML renderer: a bounded HTML subset to reflowed text with SGR runs (TUI-124). */

#include "tuikit/htmlrender.h"
#include "internal.h"
#include "tuikit/text.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * The renderer is one pass over the markup.  Inline text collects in a
 * buffer with style markers (MK plus two bytes naming an entry of the style
 * table); a block end wraps it into the output, which still has the markers;
 * the last step turns the markers into SGR or drops them.  An allocation
 * failure is the sticky `failed` of tui_buf: helpers append without checking
 * and the entry point looks at the flags once.  Nothing here reads a file,
 * runs anything or opens a URL.
 */

#define MK       '\x01'
#define ST_MAX   512
#define STK_MAX  64
#define CTX_MAX  8
#define TB_ROWS  128
#define TB_COLS  16
#define URL_MAX  500
#define PFX_MAX  256

#define F_BOLD   1
#define F_ITAL   2
#define F_UL     4
#define F_STRIKE 8
#define F_CODE   16

enum {
    T_UNKNOWN, T_B, T_I, T_U, T_S, T_A, T_CODE, T_FONT, T_BR, T_HR, T_IMG,
    T_P, T_H, T_DIV, T_UL, T_OL, T_LI, T_BQ, T_PRE, T_TABLE, T_NTABLE, T_TR,
    T_TD, T_TH, T_SKIP, T_RAW, T_BODY, T_VOID
};

typedef struct { uint8_t flags; int32_t color; } hstyle;
typedef struct { size_t off, len; int th, present; } tcell;
typedef struct {
    int tag;
    hstyle saved;
    int ctx;                /* pushed a prefix context */
    int has_url;
    size_t url_off, txt;
    int resets;
} ent;
typedef struct {
    int quote, ordered, count, pending, mw;
    char mark[16];
} hctx;

typedef struct {
    tui_buf o, inl, cell, urls;
    hstyle st[ST_MAX];
    int nst;
    hstyle cur;
    int width;
    int sp, has_text, pre, skip_nl, resets;
    int in_table, in_cell, ntable, nrows, col;
    tcell tc[TB_ROWS][TB_COLS];
    ent stk[STK_MAX];
    int nstk;
    hctx cx[CTX_MAX];
    int ncx;
    int skip_depth;
    char skipname[16];
} H;

static void end_ent(H *h, ent *e);

static void put(tui_buf *b, char c) { tui_buf_append(b, &c, 1); }

static int cp_cols(uint32_t cp) {
    int w = tui_cp_width(cp);
    return w < 0 ? 1 : w;
}

/* Columns the text takes: a style marker has none, a wide character two. */
static int vis_width(const char *s, size_t n) {
    int w = 0;
    for (size_t i = 0; i < n;) {
        if (s[i] == MK) {
            i += 3;
            continue;
        }
        uint32_t cp;
        i += (size_t)tui_utf8_decode(s + i, n - i, &cp);
        w += cp_cols(cp);
    }
    return w;
}

/* ---- styles ---------------------------------------------------------- */

static int style_id(H *h, hstyle s) {
    if (s.flags == 0 && s.color < 0)
        return 0;
    for (int i = 1; i < h->nst; i++)
        if (h->st[i].flags == s.flags && h->st[i].color == s.color)
            return i;
    if (h->nst >= ST_MAX)
        return 0;
    h->st[h->nst] = s;
    return h->nst++;
}

static tui_buf *tgt(H *h) { return h->in_cell ? &h->cell : &h->inl; }

static void put_marker(tui_buf *b, int id) {
    char m[3] = { MK, (char)(0x40 + (id >> 6)), (char)(0x40 + (id & 63)) };
    tui_buf_append(b, m, 3);
}

static void emit_style(H *h) {
    if (h->in_table && !h->in_cell)
        return;
    put_marker(tgt(h), style_id(h, h->cur));
}

static void reset_target(H *h) {
    h->resets++;
    h->inl.len = 0;
    h->sp = 1;
    h->has_text = 0;
    emit_style(h);
}

/* ---- text in ---------------------------------------------------------- */

static void add_cp(H *h, uint32_t cp) {
    if (h->in_table && !h->in_cell)
        return;
    tui_buf *t = tgt(h);
    int pre = h->pre && !h->in_cell;
    if (pre && h->skip_nl) {
        h->skip_nl = 0;
        if (cp == '\n')
            return;
    }
    if (cp == '\r')
        return;
    if (cp == '\n' && pre) {
        put(t, '\n');
        return;
    }
    if (cp == ' ' || cp == '\t' || cp == '\n' || cp == '\f' || cp < 0x20 || cp == 0x7f ||
        (cp >= 0x80 && cp < 0xa0)) {
        if (pre)
            put(t, ' ');
        else if (!h->sp) {
            put(t, ' ');
            h->sp = 1;
        }
        return;
    }
    if ((cp >= 0x202a && cp <= 0x202e) || (cp >= 0x2066 && cp <= 0x2069) || cp == 0xfeff ||
        cp == 0x200e || cp == 0x200f)
        return;
    char b[4];
    tui_buf_append(t, b, (size_t)tui_utf8_encode(cp, b));
    h->sp = 0;
    h->has_text = 1;
}

static void add_str(H *h, const char *s, size_t n) {
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        i += (size_t)tui_utf8_decode(s + i, n - i, &cp);
        add_cp(h, cp);
    }
}

static const struct { const char *n; uint32_t cp; } ENT[] = {
    { "amp", '&' }, { "lt", '<' }, { "gt", '>' }, { "quot", '"' }, { "apos", '\'' },
    { "nbsp", 0xA0 }, { "copy", 0xA9 }, { "reg", 0xAE }, { "hellip", 0x2026 },
    { "mdash", 0x2014 }, { "ndash", 0x2013 }, { "lsquo", 0x2018 }, { "rsquo", 0x2019 },
    { "ldquo", 0x201C }, { "rdquo", 0x201D }, { "bull", 0x2022 }, { "middot", 0xB7 },
    { "euro", 0x20AC }, { "laquo", 0xAB }, { "raquo", 0xBB }, { "times", 0xD7 },
    { "deg", 0xB0 }, { "trade", 0x2122 }, { "pound", 0xA3 }, { "yen", 0xA5 },
    { "cent", 0xA2 }, { "sect", 0xA7 }, { "para", 0xB6 }, { "plusmn", 0xB1 },
    { "larr", 0x2190 }, { "rarr", 0x2192 }, { "szlig", 0xDF },
    { "aacute", 0xE1 }, { "Aacute", 0xC1 }, { "eacute", 0xE9 }, { "Eacute", 0xC9 },
    { "iacute", 0xED }, { "Iacute", 0xCD }, { "oacute", 0xF3 }, { "Oacute", 0xD3 },
    { "ouml", 0xF6 }, { "Ouml", 0xD6 }, { "odblac", 0x151 }, { "Odblac", 0x150 },
    { "uacute", 0xFA }, { "Uacute", 0xDA }, { "uuml", 0xFC }, { "Uuml", 0xDC },
    { "udblac", 0x171 }, { "Udblac", 0x170 }, { "auml", 0xE4 }, { "Auml", 0xC4 },
    { "agrave", 0xE0 }, { "egrave", 0xE8 }, { "ccedil", 0xE7 }, { "ntilde", 0xF1 },
};

/* The entity at s[0] == '&': 1 with its code point and length, or 0. */
static int entity(const char *s, size_t n, uint32_t *cp, size_t *len) {
    size_t j = 1;
    if (j < n && s[j] == '#') {
        int hex = ++j < n && (s[j] == 'x' || s[j] == 'X');
        if (hex)
            j++;
        uint32_t v = 0;
        size_t d = 0;
        while (j < n && d < 8) {
            char c = s[j];
            int dv = c >= '0' && c <= '9' ? c - '0'
                   : hex && c >= 'a' && c <= 'f' ? c - 'a' + 10
                   : hex && c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (dv < 0)
                break;
            v = v * (hex ? 16 : 10) + (uint32_t)dv;
            j++;
            d++;
        }
        if (!d || j >= n || s[j] != ';')
            return 0;
        *cp = v == 0 || v > 0x10FFFF || (v >= 0xD800 && v <= 0xDFFF) ? 0xFFFD : v;
        *len = j + 1;
        return 1;
    }
    while (j < n && j < 12 && ((s[j] >= 'a' && s[j] <= 'z') || (s[j] >= 'A' && s[j] <= 'Z') ||
                               (s[j] >= '0' && s[j] <= '9')))
        j++;
    if (j >= n || s[j] != ';')
        return 0;
    for (size_t k = 0; k < sizeof ENT / sizeof ENT[0]; k++)
        if (strlen(ENT[k].n) == j - 1 && memcmp(ENT[k].n, s + 1, j - 1) == 0) {
            *cp = ENT[k].cp;
            *len = j + 1;
            return 1;
        }
    return 0;
}

/* ---- prefixes ---------------------------------------------------------- */

static int list_depth(const H *h) {
    int d = 0;
    for (int k = 0; k < h->ncx; k++)
        d += !h->cx[k].quote;
    return d;
}

/* The line prefixes of the open quotes and lists: p1 for the first line, p2
 * for the others.  `take` spends the bullets that wait for their first line. */
static void build_prefix(H *h, char *p1, char *p2, int take) {
    p1[0] = p2[0] = '\0';
    size_t l1 = 0, l2 = 0;
    for (int k = 0; k < h->ncx; k++) {
        hctx *c = &h->cx[k];
        if (c->quote) {
            memcpy(p1 + l1, "> ", 3);
            memcpy(p2 + l2, "> ", 3);
            l1 += 2;
            l2 += 2;
            continue;
        }
        size_t ml = strlen(c->mark);
        if (c->pending) {
            memcpy(p1 + l1, c->mark, ml + 1);
            l1 += ml;
            if (take)
                c->pending = 0;
        } else {
            memset(p1 + l1, ' ', (size_t)c->mw);
            l1 += (size_t)c->mw;
        }
        memset(p2 + l2, ' ', (size_t)c->mw);
        l2 += (size_t)c->mw;
        p1[l1] = p2[l2] = '\0';
    }
}

/* ---- wrapping ---------------------------------------------------------- */

typedef struct {
    tui_buf *o;
    int width, plen, plen2, open, col, hascur, haspm, fresh, havesp;
    const char *prefix, *p2;
    char cur[3], pm[3], sp[3];
} W;

static int zero_marker(const char *m) { return m[1] == '@' && m[2] == '@'; }

static void w_open(W *w) {
    tui_buf_puts(w->o, w->prefix);
    w->col = w->plen;
    w->open = 1;
    w->fresh = 1;       /* the style open at the break is opened again by the first character */
}

static void w_nl(W *w) {
    put(w->o, '\n');
    w->open = 0;
    w->prefix = w->p2;
    w->plen = w->plen2;
}

static void w_pm(W *w, const char *t) {
    memcpy(w->pm, t, 3);
    w->haspm = 1;
}

static void w_flush_pm(W *w) {
    if (w->haspm) {
        if (w->fresh || !w->hascur || memcmp(w->cur, w->pm, 3) != 0)
            tui_buf_append(w->o, w->pm, 3);
        memcpy(w->cur, w->pm, 3);
        w->hascur = 1;
        w->haspm = 0;
    } else if (w->fresh && w->hascur && !zero_marker(w->cur)) {
        tui_buf_append(w->o, w->cur, 3);
    }
    w->fresh = 0;
}

/* Wraps `t` (with markers) to `width` columns; the first line gets `p1`, the
 * others `p2`.  A word wider than a line is split at the column.  A newline
 * in the text is a forced break (a <br>, a line of <pre>). */
static void wrap(tui_buf *o, const char *t, size_t n, int width, const char *p1,
                 const char *p2, int pre) {
    W w;
    memset(&w, 0, sizeof w);
    w.o = o;
    w.width = width;
    w.prefix = p1;
    w.p2 = p2;
    w.plen = vis_width(p1, strlen(p1));
    w.plen2 = vis_width(p2, strlen(p2));
    size_t i = 0;
    while (i < n) {
        if (t[i] == MK) {
            w_pm(&w, t + i);
            i += 3;
            continue;
        }
        if (t[i] == '\n') {
            w_nl(&w);
            w.havesp = 0;
            i++;
            continue;
        }
        if (pre) {
            uint32_t cp;
            int k = tui_utf8_decode(t + i, n - i, &cp);
            int cw = cp_cols(cp);
            if (!w.open)
                w_open(&w);
            else if (w.col + cw > width && w.col > w.plen) {
                w_nl(&w);
                w_open(&w);
            }
            w_flush_pm(&w);
            tui_buf_append(o, t + i, (size_t)k);
            w.col += cw;
            i += (size_t)k;
            continue;
        }
        if (t[i] == ' ') {
            /* the blank takes the style it stands in */
            if (!w.havesp) {
                memcpy(w.sp, w.haspm ? w.pm : w.hascur ? w.cur : "\x01@@", 3);
                w.havesp = 1;
            }
            i++;
            continue;
        }
        size_t j = i;
        int wv = 0;
        while (j < n && t[j] != ' ' && t[j] != '\n') {
            if (t[j] == MK) {
                j += 3;
                continue;
            }
            uint32_t cp;
            j += (size_t)tui_utf8_decode(t + j, n - j, &cp);
            wv += cp_cols(cp);
        }
        if (w.open && w.col + 1 + wv > width)
            w_nl(&w);
        if (!w.open)
            w_open(&w);
        else {
            char keep[3];
            int kept = w.haspm;
            memcpy(keep, w.pm, 3);
            memcpy(w.pm, w.sp, 3);
            w.haspm = 1;
            w_flush_pm(&w);
            memcpy(w.pm, keep, 3);
            w.haspm = kept;
            put(o, ' ');
            w.col++;
        }
        w.havesp = 0;
        int fits = w.col + wv <= width || w.plen >= width;
        while (i < j) {
            if (t[i] == MK) {
                w_pm(&w, t + i);
                i += 3;
                continue;
            }
            uint32_t cp;
            int k = tui_utf8_decode(t + i, j - i, &cp);
            int cw = cp_cols(cp);
            w_flush_pm(&w);
            if (!fits && w.col + cw > width && w.col > w.plen) {
                w_nl(&w);
                w_open(&w);
            }
            tui_buf_append(o, t + i, (size_t)k);
            w.col += cw;
            i += (size_t)k;
        }
    }
    if (w.open)
        put(o, '\n');
}

/* ---- output blocks ------------------------------------------------------ */

static void blank_line(tui_buf *o) {
    if (o->len == 0 || (o->len >= 2 && o->data[o->len - 1] == '\n' &&
                        o->data[o->len - 2] == '\n'))
        return;
    put(o, '\n');
}

/* The text collected so far goes to the output; inside a cell it is a space. */
static void brk(H *h) {
    if (h->in_cell) {
        add_cp(h, ' ');
        return;
    }
    if (h->in_table)
        return;
    if (h->has_text) {
        size_t n = h->inl.len;
        while (n > 0 && (h->inl.data[n - 1] == '\n' || (!h->pre && h->inl.data[n - 1] == ' ')))
            n--;
        char p1[PFX_MAX], p2[PFX_MAX];
        build_prefix(h, p1, p2, 1);
        if (h->pre) {
            strcat(p1, "  ");
            strcat(p2, "  ");
        }
        wrap(&h->o, h->inl.data, n, h->width, p1, p2, h->pre);
    }
    reset_target(h);
}

static void gap(H *h) {
    brk(h);
    if (!h->in_cell && !h->in_table)
        blank_line(&h->o);
}

/* ---- tables ------------------------------------------------------------- */

static void cell_open(H *h, int th) {
    if (h->nrows < 0)
        h->nrows = 0;
    h->col++;
    h->in_cell = 1;
    h->sp = 1;
    if (h->nrows < TB_ROWS && h->col < TB_COLS) {
        tcell *c = &h->tc[h->nrows][h->col];
        c->off = h->cell.len;
        c->th = th;
        c->present = 1;
    }
    emit_style(h);
}

static void cell_close(H *h) {
    if (!h->in_cell)
        return;
    while (h->cell.len > 0 && h->cell.data[h->cell.len - 1] == ' ')
        h->cell.len--;
    put_marker(&h->cell, 0);
    if (h->nrows < TB_ROWS && h->col < TB_COLS) {
        tcell *c = &h->tc[h->nrows][h->col];
        c->len = h->cell.len - c->off;
    }
    h->in_cell = 0;
}

static void table_out(H *h) {
    static const char *ell = "\xE2\x80\xA6";
    int nr = h->nrows + 1 > TB_ROWS ? TB_ROWS : h->nrows + 1, nc = 0;
    for (int r = 0; r < nr; r++)
        for (int c = 0; c < TB_COLS; c++)
            if (h->tc[r][c].present && c + 1 > nc)
                nc = c + 1;
    if (nc == 0)
        return;
    char p1[PFX_MAX], p2[PFX_MAX];
    build_prefix(h, p1, p2, 1);
    int avail = h->width - vis_width(p2, strlen(p2)) - 2;
    int wid[TB_COLS] = { 0 };
    for (int r = 0; r < nr; r++)
        for (int c = 0; c < nc; c++) {
            const tcell *tc = &h->tc[r][c];
            int w = tc->present ? vis_width(h->cell.data + tc->off, tc->len) : 0;
            if (w > wid[c])
                wid[c] = w;
        }
    for (;;) {
        int total = 0, widest = 0;
        for (int c = 0; c < nc; c++) {
            total += wid[c] + 3;
            if (wid[c] > wid[widest])
                widest = c;
        }
        if (total <= avail || wid[widest] <= 6)
            break;
        wid[widest]--;
    }
    int header = nr > 1, any0 = 0;
    for (int c = 0; c < TB_COLS; c++) {
        any0 |= h->tc[0][c].present;
        if (h->tc[0][c].present && !h->tc[0][c].th)
            header = 0;
    }
    header = header && any0;
    int first = 1;
    for (int r = 0; r < nr; r++) {
        int any = 0;
        for (int c = 0; c < nc; c++)
            any |= h->tc[r][c].present;
        if (!any)
            continue;
        tui_buf_puts(&h->o, first ? p1 : p2);
        tui_buf_puts(&h->o, "  ");
        first = 0;
        int last = 0;
        for (int c = 0; c < nc; c++)
            if (h->tc[r][c].present)
                last = c;
        for (int c = 0; c <= last; c++) {
            const tcell *tc = &h->tc[r][c];
            const char *s = tc->present ? h->cell.data + tc->off : "";
            size_t n = tc->present ? tc->len : 0;
            int w = vis_width(s, n), shown = w;
            size_t cut = n;
            if (w > wid[c]) {
                int seen = 0;
                cut = 0;
                while (cut < n) {
                    if (s[cut] == MK) {
                        cut += 3;
                        continue;
                    }
                    uint32_t cp;
                    int k = tui_utf8_decode(s + cut, n - cut, &cp);
                    if (seen + cp_cols(cp) > wid[c] - 1)
                        break;
                    seen += cp_cols(cp);
                    cut += (size_t)k;
                }
                shown = seen + 1;
            }
            tui_buf_append(&h->o, s, cut);
            if (cut < n) {
                put_marker(&h->o, 0);
                tui_buf_puts(&h->o, ell);
            }
            if (c < last) {
                for (int p = shown; p < wid[c]; p++)
                    put(&h->o, ' ');
                tui_buf_puts(&h->o, " \xE2\x94\x82 ");
            }
        }
        put(&h->o, '\n');
        if (r == 0 && header) {
            tui_buf_puts(&h->o, p2);
            tui_buf_puts(&h->o, "  ");
            for (int c = 0; c < nc; c++) {
                for (int p = 0; p < wid[c]; p++)
                    tui_buf_puts(&h->o, "\xE2\x94\x80");
                if (c + 1 < nc)
                    tui_buf_puts(&h->o, "\xE2\x94\x80\xE2\x94\xBC\xE2\x94\x80");
            }
            put(&h->o, '\n');
        }
    }
}

/* ---- css and attributes ---------------------------------------------------- */

static int ci_eq(const char *a, size_t n, const char *lit) {
    size_t l = strlen(lit);
    if (n != l)
        return 0;
    for (size_t i = 0; i < n; i++) {
        char c = a[i] >= 'A' && a[i] <= 'Z' ? (char)(a[i] + 32) : a[i];
        if (c != lit[i])
            return 0;
    }
    return 1;
}

static int ci_has(const char *a, size_t n, const char *lit) {
    size_t l = strlen(lit);
    for (size_t i = 0; i + l <= n; i++)
        if (ci_eq(a + i, l, lit))
            return 1;
    return 0;
}

static const struct { const char *n; int32_t rgb; } COLORS[] = {
    { "black", 0x000000 }, { "white", 0xffffff }, { "red", 0xff0000 },
    { "green", 0x008000 }, { "blue", 0x0000ff }, { "yellow", 0xffff00 },
    { "cyan", 0x00ffff }, { "aqua", 0x00ffff }, { "magenta", 0xff00ff },
    { "fuchsia", 0xff00ff }, { "gray", 0x808080 }, { "grey", 0x808080 },
    { "silver", 0xc0c0c0 }, { "maroon", 0x800000 }, { "olive", 0x808000 },
    { "navy", 0x000080 }, { "purple", 0x800080 }, { "teal", 0x008080 },
    { "lime", 0x00ff00 }, { "orange", 0xffa500 },
};

static int hexv(char c) {
    return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10
         : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

/* A CSS colour (#rgb, #rrggbb, rgb(r,g,b), a basic name) into *rgb. */
static int parse_color(const char *v, size_t n, int32_t *rgb) {
    while (n && *v == ' ') {
        v++;
        n--;
    }
    while (n && v[n - 1] == ' ')
        n--;
    if (n && v[0] == '#' && (n == 4 || n == 7)) {
        int32_t r = 0;
        for (size_t i = 1; i < n; i++) {
            int d = hexv(v[i]);
            if (d < 0)
                return 0;
            r = n == 4 ? (r << 8) | (d * 17) : (r << 4) | d;
        }
        *rgb = r;
        return 1;
    }
    if (n > 5 && ci_eq(v, 4, "rgb(") && v[n - 1] == ')') {
        int32_t r = 0;
        const char *p = v + 4, *e = v + n - 1;
        for (int k = 0; k < 3; k++) {
            while (p < e && *p == ' ')
                p++;
            int x = 0, d = 0;
            while (p < e && *p >= '0' && *p <= '9' && d < 4) {
                x = x * 10 + (*p++ - '0');
                d++;
            }
            while (p < e && *p == ' ')
                p++;
            if (!d || (k < 2 ? (p >= e || *p != ',') : p != e))
                return 0;
            p++;
            r = (r << 8) | (x > 255 ? 255 : x);
        }
        *rgb = r;
        return 1;
    }
    for (size_t k = 0; k < sizeof COLORS / sizeof COLORS[0]; k++)
        if (ci_eq(v, n, COLORS[k].n)) {
            *rgb = COLORS[k].rgb;
            return 1;
        }
    return 0;
}

static void parse_css(const char *s, hstyle *st) {
    size_t n = strlen(s), i = 0;
    while (i < n) {
        size_t e = i;
        while (e < n && s[e] != ';')
            e++;
        const char *colon = memchr(s + i, ':', e - i);
        if (colon) {
            const char *k = s + i;
            size_t kl = (size_t)(colon - k);
            const char *v = colon + 1;
            size_t vl = (size_t)(s + e - v);
            while (kl && k[kl - 1] == ' ')
                kl--;
            while (kl && *k == ' ') {
                k++;
                kl--;
            }
            if (ci_eq(k, kl, "color"))
                parse_color(v, vl, &st->color);
            else if (ci_eq(k, kl, "font-weight")) {
                if (ci_has(v, vl, "bold") || ci_has(v, vl, "600") || ci_has(v, vl, "700") ||
                    ci_has(v, vl, "800") || ci_has(v, vl, "900"))
                    st->flags |= F_BOLD;
                else if (ci_has(v, vl, "normal"))
                    st->flags &= (uint8_t)~F_BOLD;
            } else if (ci_eq(k, kl, "font-style")) {
                if (ci_has(v, vl, "italic") || ci_has(v, vl, "oblique"))
                    st->flags |= F_ITAL;
                else if (ci_has(v, vl, "normal"))
                    st->flags &= (uint8_t)~F_ITAL;
            } else if (ci_eq(k, kl, "text-decoration")) {
                if (ci_has(v, vl, "none"))
                    st->flags &= (uint8_t)~(F_UL | F_STRIKE);
                if (ci_has(v, vl, "underline"))
                    st->flags |= F_UL;
                if (ci_has(v, vl, "line-through"))
                    st->flags |= F_STRIKE;
            }
        }
        i = e + 1;
    }
}

typedef struct {
    char href[URL_MAX + 1], alt[256], style[256], color[32], start[16];
    int bad_href;
} attrs;

static void set_attr(char *dst, size_t cap, const char *v, size_t n, int *over) {
    size_t d = 0;
    for (size_t i = 0; i < n;) {
        uint32_t cp;
        size_t len = 0;
        if (v[i] == '&' && entity(v + i, n - i, &cp, &len)) {
            i += len;
        } else {
            i += (size_t)tui_utf8_decode(v + i, n - i, &cp);
        }
        char b[4];
        int k = tui_utf8_encode(cp, b);
        if (d + (size_t)k >= cap) {
            if (over)
                *over = 1;
            break;
        }
        memcpy(dst + d, b, (size_t)k);
        d += (size_t)k;
    }
    dst[d] = '\0';
}

static int safe_url(const char *u) {
    size_t n = strlen(u);
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)u[i] <= 0x20 || (unsigned char)u[i] >= 0x7f)
            return 0;
    return (n > 7 && ci_eq(u, 7, "http://")) || (n > 8 && ci_eq(u, 8, "https://")) ||
           (n > 7 && ci_eq(u, 7, "mailto:"));
}

/* ---- tags ----------------------------------------------------------------- */

static const struct { const char *n; int tag; } TAGS[] = {
    { "b", T_B }, { "strong", T_B }, { "i", T_I }, { "em", T_I }, { "cite", T_I },
    { "dfn", T_I }, { "var", T_I }, { "u", T_U }, { "ins", T_U }, { "s", T_S },
    { "strike", T_S }, { "del", T_S }, { "a", T_A }, { "code", T_CODE }, { "tt", T_CODE },
    { "kbd", T_CODE }, { "samp", T_CODE }, { "font", T_FONT }, { "br", T_BR },
    { "hr", T_HR }, { "img", T_IMG }, { "p", T_P }, { "h1", T_H }, { "h2", T_H },
    { "h3", T_H }, { "h4", T_H }, { "h5", T_H }, { "h6", T_H }, { "div", T_DIV },
    { "section", T_DIV }, { "article", T_DIV }, { "header", T_DIV }, { "footer", T_DIV },
    { "main", T_DIV }, { "nav", T_DIV }, { "aside", T_DIV }, { "address", T_DIV },
    { "figure", T_DIV }, { "figcaption", T_DIV }, { "center", T_DIV }, { "form", T_DIV },
    { "dl", T_DIV }, { "dt", T_DIV }, { "dd", T_DIV }, { "caption", T_DIV },
    { "ul", T_UL }, { "ol", T_OL }, { "li", T_LI }, { "blockquote", T_BQ },
    { "pre", T_PRE }, { "table", T_TABLE }, { "tr", T_TR }, { "td", T_TD }, { "th", T_TH },
    { "head", T_SKIP }, { "title", T_SKIP }, { "template", T_SKIP }, { "object", T_SKIP },
    { "iframe", T_SKIP }, { "applet", T_SKIP }, { "svg", T_SKIP }, { "math", T_SKIP },
    { "select", T_SKIP }, { "textarea", T_SKIP }, { "script", T_RAW }, { "style", T_RAW },
    { "body", T_BODY }, { "meta", T_VOID }, { "link", T_VOID }, { "input", T_VOID },
    { "wbr", T_VOID }, { "area", T_VOID }, { "base", T_VOID }, { "col", T_VOID },
    { "source", T_VOID }, { "param", T_VOID }, { "track", T_VOID }, { "embed", T_VOID },
};

static int tag_id(const char *name) {
    for (size_t k = 0; k < sizeof TAGS / sizeof TAGS[0]; k++)
        if (strcmp(TAGS[k].n, name) == 0)
            return TAGS[k].tag;
    return T_UNKNOWN;
}

static void ctx_push(H *h, ent *e, int quote, int ordered, int start) {
    if (h->in_table || h->ncx >= CTX_MAX)
        return;
    hctx *c = &h->cx[h->ncx++];
    memset(c, 0, sizeof *c);
    c->quote = quote;
    c->ordered = ordered;
    c->count = start - 1;
    e->ctx = 1;
}

static int start_of(const attrs *a) {
    if (!a->start[0])
        return 1;
    char *end;
    long v = strtol(a->start, &end, 10);
    return end == a->start || v < 1 ? 1 : v > 99999 ? 99999 : (int)v;
}

static void li_open(H *h, ent *e) {
    if (h->in_table) {
        brk(h);
        if (h->in_cell) {
            add_cp(h, 0x2022);
            add_cp(h, ' ');
        }
        return;
    }
    brk(h);
    int k = h->ncx - 1;
    while (k >= 0 && h->cx[k].quote)
        k--;
    if (k < 0) {
        ctx_push(h, e, 0, 0, 1);
        k = h->ncx - 1;
        if (k < 0 || h->cx[k].quote || !e->ctx)
            return;
    }
    hctx *c = &h->cx[k];
    if (c->ordered) {
        c->count++;
        snprintf(c->mark, sizeof c->mark, " %d. ", c->count);
        c->mw = (int)strlen(c->mark);
    } else {
        int depth = 0;
        for (int q = 0; q < k; q++)
            depth += !h->cx[q].quote;
        strcpy(c->mark, depth % 2 ? " \xE2\x97\xA6 " : " \xE2\x80\xA2 ");
        c->mw = 3;
    }
    c->pending = 1;
}

static void rule_out(H *h) {
    char p1[PFX_MAX], p2[PFX_MAX];
    build_prefix(h, p1, p2, 0);
    int room = h->width - vis_width(p1, strlen(p1));
    if (room > 60)
        room = 60;
    if (room < 1)
        room = 1;
    tui_buf_puts(&h->o, p1);
    for (int i = 0; i < room; i++)
        tui_buf_puts(&h->o, "\xE2\x94\x80");
    put(&h->o, '\n');
}

/* Starts an element: style first, then what its kind does at its start. */
static void open_tag(H *h, int tag, const attrs *a, int selfclose) {
    ent e;
    memset(&e, 0, sizeof e);
    e.tag = tag;

    switch (tag) {
    case T_BR:
        if (h->in_cell)
            add_cp(h, ' ');
        else if (!(h->in_table)) {
            put(&h->inl, '\n');
            h->sp = 1;
        }
        return;
    case T_HR:
        gap(h);
        if (!h->in_table)
            rule_out(h);
        if (!h->in_table)
            blank_line(&h->o);
        return;
    case T_IMG:
        if (a->alt[0]) {
            add_cp(h, '[');
            add_str(h, a->alt, strlen(a->alt));
            add_cp(h, ']');
        }
        return;
    case T_VOID:
        return;
    default:
        break;
    }

    /* Optional end tags: a new item or cell ends the one before. */
    int stop = -1;
    for (int k = h->nstk - 1; k >= 0 && stop < 0; k--) {
        int t = h->stk[k].tag;
        if (tag == T_LI && t == T_LI)
            stop = k;
        else if ((tag == T_TD || tag == T_TH) && (t == T_TD || t == T_TH))
            stop = k;
        else if (tag == T_TR && t == T_TR)
            stop = k;
        else if (tag == T_P && t == T_P)
            stop = k;
        if (stop < 0 && (t == T_UL || t == T_OL || t == T_TABLE || t == T_NTABLE ||
                         (tag == T_P && (t == T_LI || t == T_TD || t == T_TH || t == T_BQ ||
                                         t == T_DIV)) ||
                         ((tag == T_TD || tag == T_TH) && t == T_TR)))
            break;
    }
    while (stop >= 0 && h->nstk > stop) {
        ent top = h->stk[--h->nstk];
        end_ent(h, &top);
    }

    if (h->nstk >= STK_MAX)
        return;
    if (tag == T_TABLE && h->in_table)
        tag = T_NTABLE;
    if (h->ntable > 0 && (tag == T_TR || tag == T_TD || tag == T_TH))
        tag = T_DIV;
    e.tag = tag;
    e.saved = h->cur;
    hstyle ns = h->cur;
    switch (tag) {
    case T_B: case T_H: case T_TH: ns.flags |= F_BOLD; break;
    case T_I: ns.flags |= F_ITAL; break;
    case T_U: case T_A: ns.flags |= F_UL; break;
    case T_S: ns.flags |= F_STRIKE; break;
    case T_CODE: ns.flags |= F_CODE; break;
    default: break;
    }
    if (a->color[0])
        parse_color(a->color, strlen(a->color), &ns.color);
    if (a->style[0])
        parse_css(a->style, &ns);
    if (ns.flags != h->cur.flags || ns.color != h->cur.color) {
        h->cur = ns;
        emit_style(h);
    }

    switch (tag) {
    case T_A:
        e.txt = tgt(h)->len;
        e.resets = h->resets;
        if (a->href[0] && !a->bad_href && safe_url(a->href)) {
            e.has_url = 1;
            e.url_off = h->urls.len;
            tui_buf_append(&h->urls, a->href, strlen(a->href) + 1);
        }
        break;
    case T_P: case T_H: case T_BQ:
        gap(h);
        if (tag == T_BQ)
            ctx_push(h, &e, 1, 0, 1);
        break;
    case T_UL: case T_OL:
        if (list_depth(h))
            brk(h);
        else
            gap(h);
        ctx_push(h, &e, 0, tag == T_OL, start_of(a));
        break;
    case T_LI:
        li_open(h, &e);
        break;
    case T_PRE:
        gap(h);
        h->pre = 1;
        h->skip_nl = 1;
        break;
    case T_TABLE:
        gap(h);
        memset(h->tc, 0, sizeof h->tc);
        h->cell.len = 0;
        h->in_table = 1;
        h->nrows = -1;
        h->col = -1;
        break;
    case T_NTABLE:
        h->ntable++;
        brk(h);
        break;
    case T_TR:
        if (h->in_table) {
            cell_close(h);
            h->nrows++;
            h->col = -1;
        }
        break;
    case T_TD: case T_TH:
        if (h->in_table) {
            cell_close(h);
            cell_open(h, tag == T_TH);
        }
        break;
    case T_DIV:
        brk(h);
        break;
    default:
        break;
    }
    h->stk[h->nstk++] = e;
    if (selfclose) {
        ent top = h->stk[--h->nstk];
        end_ent(h, &top);
    }
}

/* The visible text of marked text, for comparing a link's text with its URL. */
static void visible(const char *s, size_t n, tui_buf *out) {
    for (size_t i = 0; i < n;) {
        if (s[i] == MK) {
            i += 3;
            continue;
        }
        put(out, s[i++]);
    }
}

static void end_ent(H *h, ent *e) {
    int restyle = e->saved.flags != h->cur.flags || e->saved.color != h->cur.color;
    h->cur = e->saved;
    if (restyle)
        emit_style(h);
    switch (e->tag) {
    case T_A:
        if (e->has_url && h->urls.data) {
            const char *url = h->urls.data + e->url_off;
            tui_buf *t = tgt(h);
            tui_buf vis;
            tui_buf_init(&vis);
            int moved = h->resets != e->resets;    /* a block inside the link flushed its text */
            if (!moved)
                visible(t->data + e->txt, t->len - e->txt, &vis);
            size_t vn = vis.data ? vis.len : 0;
            while (vn && vis.data[vn - 1] == ' ')
                vn--;
            size_t vs = 0;
            while (vs < vn && vis.data[vs] == ' ')
                vs++;
            int same = !moved && vn - vs == strlen(url) && memcmp(vis.data + vs, url, vn - vs) == 0;
            int text = moved || vn > vs;
            if (!same) {
                if (text) {
                    add_cp(h, ' ');
                    add_cp(h, '(');
                }
                add_str(h, url, strlen(url));
                if (text)
                    add_cp(h, ')');
            }
            tui_buf_free(&vis);
            h->urls.len = e->url_off;
        }
        break;
    case T_P: case T_H:
        gap(h);
        break;
    case T_BQ: case T_UL: case T_OL:
        brk(h);
        if (e->ctx)
            h->ncx--;
        if (e->tag == T_BQ || !list_depth(h))
            if (!h->in_cell && !h->in_table)
                blank_line(&h->o);
        break;
    case T_LI:
        brk(h);
        if (e->ctx)
            h->ncx--;
        break;
    case T_PRE:
        brk(h);
        h->pre = 0;
        gap(h);
        break;
    case T_DIV:
        brk(h);
        break;
    case T_NTABLE:
        h->ntable--;
        brk(h);
        break;
    case T_TD: case T_TH:
        if (h->in_table)
            cell_close(h);
        break;
    case T_TABLE:
        cell_close(h);
        h->in_table = 0;
        table_out(h);
        h->cell.len = 0;
        h->sp = 1;
        gap(h);
        break;
    default:
        break;
    }
}

/* ---- the pass ---------------------------------------------------------------- */

static void pop_to(H *h, int k) {
    while (h->nstk > k) {
        ent top = h->stk[--h->nstk];
        end_ent(h, &top);
    }
}

static void close_tag(H *h, int tag) {
    for (int k = h->nstk - 1; k >= 0; k--)
        if (h->stk[k].tag == tag) {
            pop_to(h, k);
            return;
        }
}

static int is_namechar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

static void parse(H *h, const char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        if (s[i] != '<') {
            if (h->skip_depth) {
                i++;
                continue;
            }
            uint32_t cp;
            size_t len = 0;
            if (s[i] == '&' && entity(s + i, n - i, &cp, &len)) {
                add_cp(h, cp);
                i += len;
            } else {
                i += (size_t)tui_utf8_decode(s + i, n - i, &cp);
                add_cp(h, cp);
            }
            continue;
        }
        /* a comment, a declaration or a processing instruction */
        if (i + 1 < n && (s[i + 1] == '!' || s[i + 1] == '?')) {
            const char *end;
            if (n - i >= 4 && memcmp(s + i, "<!--", 4) == 0) {
                end = NULL;
                for (size_t q = i + 4; q + 3 <= n; q++)
                    if (memcmp(s + q, "-->", 3) == 0) {
                        end = s + q + 3;
                        break;
                    }
            } else {
                end = memchr(s + i, '>', n - i);
                if (end)
                    end++;
            }
            i = end ? (size_t)(end - s) : n;
            continue;
        }
        int closing = i + 1 < n && s[i + 1] == '/';
        size_t j = i + 1 + (size_t)closing;
        if (j >= n || !is_namechar(s[j]) || (s[j] >= '0' && s[j] <= '9')) {
            if (!h->skip_depth)
                add_cp(h, '<');
            i++;
            continue;
        }
        char name[16];
        size_t nl = 0;
        while (j < n && is_namechar(s[j])) {
            if (nl < sizeof name - 1)
                name[nl++] = s[j] >= 'A' && s[j] <= 'Z' ? (char)(s[j] + 32) : s[j];
            j++;
        }
        name[nl] = '\0';
        attrs ab, *a = &ab;
        memset(&ab, 0, sizeof ab);
        int selfclose = 0, done = 0;
        while (!done) {
            while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n' || s[j] == '\r' ||
                             s[j] == '\f'))
                j++;
            if (j >= n)
                break;
            if (s[j] == '>') {
                j++;
                done = 1;
                break;
            }
            if (s[j] == '/') {
                selfclose = 1;
                j++;
                continue;
            }
            selfclose = 0;
            size_t ks = j;
            while (j < n && s[j] != '=' && s[j] != '>' && s[j] != '/' && s[j] != ' ' &&
                   s[j] != '\t' && s[j] != '\n' && s[j] != '\r' && s[j] != '\f')
                j++;
            char an[16];
            size_t al = j - ks < sizeof an - 1 ? j - ks : sizeof an - 1;
            for (size_t q = 0; q < al; q++)
                an[q] = s[ks + q] >= 'A' && s[ks + q] <= 'Z' ? (char)(s[ks + q] + 32) : s[ks + q];
            an[al] = '\0';
            while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n' || s[j] == '\r'))
                j++;
            size_t vs = j, ve = j;
            if (j < n && s[j] == '=') {
                j++;
                while (j < n && (s[j] == ' ' || s[j] == '\t' || s[j] == '\n' || s[j] == '\r'))
                    j++;
                if (j < n && (s[j] == '"' || s[j] == '\'')) {
                    char q = s[j++];
                    vs = j;
                    while (j < n && s[j] != q)
                        j++;
                    ve = j;
                    if (j < n)
                        j++;
                } else {
                    vs = j;
                    while (j < n && s[j] != '>' && s[j] != ' ' && s[j] != '\t' &&
                           s[j] != '\n' && s[j] != '\r')
                        j++;
                    ve = j;
                }
            }
            if (strcmp(an, "href") == 0)
                set_attr(a->href, sizeof a->href, s + vs, ve - vs, &a->bad_href);
            else if (strcmp(an, "alt") == 0)
                set_attr(a->alt, sizeof a->alt, s + vs, ve - vs, NULL);
            else if (strcmp(an, "style") == 0)
                set_attr(a->style, sizeof a->style, s + vs, ve - vs, NULL);
            else if (strcmp(an, "color") == 0)
                set_attr(a->color, sizeof a->color, s + vs, ve - vs, NULL);
            else if (strcmp(an, "start") == 0)
                set_attr(a->start, sizeof a->start, s + vs, ve - vs, NULL);
        }
        if (!done) {
            /* an unfinished tag at the end of the text is dropped */
            return;
        }
        i = j;
        int tag = tag_id(name);

        if (h->skip_depth) {
            if (strcmp(name, h->skipname) == 0 && !selfclose)
                h->skip_depth += closing ? -1 : 1;
            else if (tag == T_BODY && !closing && strcmp(h->skipname, "head") == 0)
                h->skip_depth = 0;
            if (h->skip_depth == 0 && tag == T_BODY && !closing)
                open_tag(h, tag, a, 0);
            continue;
        }
        if (closing && h->ntable > 0 && (tag == T_TR || tag == T_TD || tag == T_TH)) {
            continue;       /* a nested table is flat: its cells end with it */
        }
        if (closing) {
            if (tag == T_TABLE && h->ntable > 0)
                tag = T_NTABLE;
            if (tag != T_BR && tag != T_HR && tag != T_IMG && tag != T_VOID)
                close_tag(h, tag);
            continue;
        }
        if (tag == T_RAW) {
            size_t l = strlen(name), q = i;
            for (; q + l + 2 <= n; q++)
                if (s[q] == '<' && s[q + 1] == '/' && ci_eq(s + q + 2, l, name))
                    break;
            const char *gt = q + l + 2 <= n ? memchr(s + q, '>', n - q) : NULL;
            i = gt ? (size_t)(gt - s) + 1 : n;
        } else if (tag == T_SKIP) {
            if (!selfclose) {
                h->skip_depth = 1;
                strcpy(h->skipname, name);
            }
        } else {
            open_tag(h, tag, a, selfclose);
        }
    }
}

/* ---- markers to SGR or nothing --------------------------------------------- */

static void sgr_on(const hstyle *s, tui_buf *out) {
    char b[64];
    size_t l = 0;
    b[l++] = '\x1b';
    b[l++] = '[';
    if (s->flags & F_BOLD)
        l += (size_t)snprintf(b + l, sizeof b - l, "1;");
    if (s->flags & F_ITAL)
        l += (size_t)snprintf(b + l, sizeof b - l, "3;");
    if (s->flags & F_UL)
        l += (size_t)snprintf(b + l, sizeof b - l, "4;");
    if (s->flags & F_STRIKE)
        l += (size_t)snprintf(b + l, sizeof b - l, "9;");
    if (s->color >= 0)
        l += (size_t)snprintf(b + l, sizeof b - l, "38;2;%d;%d;%d;", (int)(s->color >> 16) & 255,
                              (int)(s->color >> 8) & 255, (int)s->color & 255);
    else if (s->flags & F_CODE)
        l += (size_t)snprintf(b + l, sizeof b - l, "36;");
    b[l - 1] = 'm';
    tui_buf_append(out, b, l);
}

static void finish(const H *h, int ansi, tui_buf *out) {
    const tui_buf *in = &h->o;
    const char *s = in->data ? in->data : "";
    size_t base = out->len;
    int curid = 0, want = 0;
    for (size_t i = 0; i < in->len;) {
        if (s[i] == MK) {
            want = ((s[i + 1] - 0x40) << 6) | (s[i + 2] - 0x40);
            i += 3;
            continue;
        }
        if (s[i] == '\n') {
            if (ansi && curid)
                tui_buf_puts(out, "\x1b[0m");
            curid = want = 0;       /* the wrapper opens a style again after the prefix */
        } else if (ansi && want != curid) {
            /* a style is written when something is drawn in it */
            if (curid)
                tui_buf_puts(out, "\x1b[0m");
            if (want)
                sgr_on(&h->st[want], out);
            curid = want;
        }
        put(out, s[i++]);
    }
    while (out->len > base && out->data[out->len - 1] == '\n')
        out->len--;
    if (out->data)
        out->data[out->len] = '\0';
}

int tui_html_render(const char *html, int width, int ansi, tui_buf *out) {
    if (!out) {
        errno = EINVAL;
        return -1;
    }
    if (!html || !html[0])
        return 0;
    H *h = calloc(1, sizeof *h);
    if (!h) {
        errno = ENOMEM;
        return -1;
    }
    tui_buf_init(&h->o);
    tui_buf_init(&h->inl);
    tui_buf_init(&h->cell);
    tui_buf_init(&h->urls);
    h->width = width < 20 ? 20 : width;
    h->nst = 1;
    h->cur.color = -1;
    h->nrows = -1;
    h->col = -1;
    h->sp = 1;
    emit_style(h);
    parse(h, html, strlen(html));
    pop_to(h, 0);
    cell_close(h);
    brk(h);
    finish(h, ansi, out);
    int failed = h->o.failed || h->inl.failed || h->cell.failed || h->urls.failed || out->failed;
    tui_buf_free(&h->o);
    tui_buf_free(&h->inl);
    tui_buf_free(&h->cell);
    tui_buf_free(&h->urls);
    free(h);
    if (failed) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}
