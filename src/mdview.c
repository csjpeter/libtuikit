/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-11 */
/* The Markdown view: the renderer of IM-390 moved into the library, reflowing, scrolling. */

#include "tuikit/mdview.h"
#include "internal.h"
#include "tuikit/text.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Inline styling travels through wrapping as zero-width marker bytes
 * (TUI_MD_*), so that the width of a line is the width of what is seen.  An
 * allocation failure is the sticky `failed` of tui_buf: the helpers append
 * without checking and the entry points look at the flag once.
 */

static int is_marker(char c) { return c >= 1 && c <= 6; }

static int is_opener(char c) { return is_marker(c) && c != TUI_MD_OFF; }

static int cp_cols(uint32_t cp) {
    int w = tui_cp_width(cp);
    return w < 0 ? 1 : w;
}

/* Columns the text takes: markers have none, a wide character two. */
static int vis_width(const char *s, size_t n) {
    int w = 0;
    for (size_t i = 0; i < n;) {
        if (is_marker(s[i])) {
            i++;
            continue;
        }
        uint32_t cp;
        i += (size_t)tui_utf8_decode(s + i, n - i, &cp);
        w += cp_cols(cp);
    }
    return w;
}

static int blank(const char *l, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (l[i] != ' ')
            return 0;
    return 1;
}

static void put(tui_buf *o, char c) { tui_buf_append(o, &c, 1); }

/* ---- inline ---------------------------------------------------------- */

static void inline_conv(const char *s, size_t n, tui_buf *o) {
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        if (c == '\\' && i + 1 < n && strchr("\\`*_[]()#|>-+.!", s[i + 1])) {
            put(o, s[++i]);
            continue;
        }
        if (c == '`') {
            size_t j = i + 1;
            while (j < n && s[j] != '`')
                j++;
            if (j < n) {
                put(o, TUI_MD_CODE);
                tui_buf_append(o, s + i + 1, j - i - 1);
                put(o, TUI_MD_OFF);
                i = j;
                continue;
            }
        }
        if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] == c) {
            size_t j = i + 2;
            while (j + 1 < n && !(s[j] == c && s[j + 1] == c))
                j++;
            if (j + 1 < n && j > i + 2) {
                put(o, TUI_MD_BOLD);
                inline_conv(s + i + 2, j - i - 2, o);
                put(o, TUI_MD_OFF);
                i = j + 1;
                continue;
            }
        }
        if ((c == '*' || c == '_') && i + 1 < n && s[i + 1] != ' ' &&
            (c == '*' || i == 0 || s[i - 1] == ' ')) {
            size_t j = i + 1;
            while (j < n && !(s[j] == c && s[j - 1] != ' '))
                j++;
            /* An underscore closes only at a word end: snake_case stays. */
            if (j < n && (c == '*' || j + 1 == n ||
                          !((s[j + 1] >= 'a' && s[j + 1] <= 'z') ||
                            (s[j + 1] >= 'A' && s[j + 1] <= 'Z') ||
                            (s[j + 1] >= '0' && s[j + 1] <= '9')))) {
                put(o, TUI_MD_ITALIC);
                inline_conv(s + i + 1, j - i - 1, o);
                put(o, TUI_MD_OFF);
                i = j;
                continue;
            }
        }
        if (c == '[') {
            const char *rb = memchr(s + i, ']', n - i);
            if (rb && (size_t)(rb - s) + 1 < n && rb[1] == '(') {
                const char *rp = memchr(rb + 2, ')', n - (size_t)(rb + 2 - s));
                if (rp) {
                    inline_conv(s + i + 1, (size_t)(rb - s) - i - 1, o);
                    tui_buf_append(o, " (", 2);
                    tui_buf_append(o, rb + 2, (size_t)(rp - rb - 2));
                    put(o, ')');
                    i = (size_t)(rp - s);
                    continue;
                }
            }
        }
        put(o, c);
    }
}

/* ---- output ---------------------------------------------------------- */

static void blank_line(tui_buf *o) {
    if (o->len == 0 || (o->len >= 2 && o->data[o->len - 1] == '\n' &&
                        o->data[o->len - 2] == '\n'))
        return;
    put(o, '\n');
}

/* Wraps `text` (with markers) to `width` visible columns.  The first line
 * gets `p1`, the others `p2`, both marker-free.  A word wider than a line
 * is split at the column, never left to run off the screen. */
static void wrap_into(tui_buf *o, const char *text, size_t n, int width,
                      const char *p1, const char *p2) {
    const char *prefix = p1;
    int plen = vis_width(p1, strlen(p1));
    int open = 0, col = 0;
    size_t i = 0;
    while (i < n) {
        while (i < n && text[i] == ' ')
            i++;
        if (i >= n)
            break;
        size_t j = i;
        while (j < n && text[j] != ' ')
            j++;
        int w = vis_width(text + i, j - i);
        if (open && col + 1 + w > width) {
            put(o, '\n');
            open = 0;
            prefix = p2;
            plen = vis_width(p2, strlen(p2));
        }
        if (!open) {
            tui_buf_puts(o, prefix);
            col = plen;
            open = 1;
        } else {
            put(o, ' ');
            col++;
        }
        if (col + w <= width || plen >= width) {
            /* fits, or cannot be helped: the prefix leaves no room */
            tui_buf_append(o, text + i, j - i);
            col += w;
            i = j;
            continue;
        }
        /* Too wide for a line of its own: split it by columns. */
        while (i < j) {
            if (is_marker(text[i])) {
                put(o, text[i++]);
                continue;
            }
            uint32_t cp;
            int k = tui_utf8_decode(text + i, j - i, &cp);
            int cw = cp_cols(cp);
            if (col + cw > width && col > plen) {
                put(o, '\n');
                tui_buf_puts(o, p2);
                prefix = p2;
                plen = vis_width(p2, strlen(p2));
                col = plen;
            }
            tui_buf_append(o, text + i, (size_t)k);
            col += cw;
            i += (size_t)k;
        }
    }
    if (open)
        put(o, '\n');
}

/* ---- tables ---------------------------------------------------------- */

#define TBL_MAX_COLS 16
#define TBL_MAX_ROWS 128

static int is_table_sep(const char *l, size_t n) {
    int dash = 0;
    for (size_t i = 0; i < n; i++) {
        if (l[i] == '-')
            dash = 1;
        else if (l[i] != '|' && l[i] != ':' && l[i] != ' ')
            return 0;
    }
    return dash;
}

static void table_out(tui_buf *o, const char *const *ls, const size_t *ns, int nl,
                      int width) {
    static const char *ell = "\xE2\x80\xA6";
    tui_buf cells;
    tui_buf_init(&cells);
    size_t off[TBL_MAX_ROWS][TBL_MAX_COLS];
    size_t len[TBL_MAX_ROWS][TBL_MAX_COLS];
    memset(len, 0, sizeof(len));
    memset(off, 0, sizeof(off));
    int nrows = 0, ncols = 0, wid[TBL_MAX_COLS] = { 0 };
    int sep_after = -1;

    for (int k = 0; k < nl && nrows < TBL_MAX_ROWS; k++) {
        const char *l = ls[k];
        size_t n = ns[k];
        if (is_table_sep(l, n)) {
            if (sep_after < 0)
                sep_after = nrows;
            continue;
        }
        size_t i = 0;
        if (i < n && l[i] == '|')
            i++;
        int c = 0;
        while (i < n && c < TBL_MAX_COLS) {
            size_t j = i;
            while (j < n && l[j] != '|')
                j++;
            size_t a = i, b = j;
            while (a < b && l[a] == ' ')
                a++;
            while (b > a && l[b - 1] == ' ')
                b--;
            if (j >= n && a == b)
                break;
            off[nrows][c] = cells.len;
            inline_conv(l + a, b - a, &cells);
            len[nrows][c] = cells.len - off[nrows][c];
            int w = vis_width(cells.data + off[nrows][c], len[nrows][c]);
            if (w > wid[c])
                wid[c] = w;
            c++;
            i = j + 1;
        }
        if (c > ncols)
            ncols = c;
        nrows++;
    }

    /* Shrink the widest column until the table fits. */
    for (;;) {
        int total = 0, widest = 0;
        for (int c = 0; c < ncols; c++) {
            total += wid[c] + 3;
            if (wid[c] > wid[widest])
                widest = c;
        }
        if (total <= width || wid[widest] <= 6)
            break;
        wid[widest]--;
    }

    for (int r = 0; r < nrows; r++) {
        tui_buf_puts(o, "  ");
        for (int c = 0; c < ncols; c++) {
            const char *s = cells.data ? cells.data + off[r][c] : "";
            size_t n = len[r][c];
            int w = vis_width(s, n);
            size_t cut = n;
            int shown = w;
            if (w > wid[c]) {
                /* Cut at the column width, on a character boundary. */
                int seen = 0;
                cut = 0;
                while (cut < n) {
                    if (is_marker(s[cut])) {
                        cut++;
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
            tui_buf_append(o, s, cut);
            if (cut < n) {
                /* Spans the cut left open are closed: they never cross a line. */
                int depth = 0;
                for (size_t q = 0; q < cut; q++)
                    depth += is_opener(s[q]) ? 1 : s[q] == TUI_MD_OFF ? -1 : 0;
                while (depth-- > 0)
                    put(o, TUI_MD_OFF);
                tui_buf_puts(o, ell);
            }
            for (int p = shown; p < wid[c]; p++)
                put(o, ' ');
            if (c + 1 < ncols)
                tui_buf_puts(o, " \xE2\x94\x82 ");
        }
        put(o, '\n');
        if (r + 1 == sep_after) {
            tui_buf_puts(o, "  ");
            for (int c = 0; c < ncols; c++) {
                for (int p = 0; p < wid[c]; p++)
                    tui_buf_puts(o, "\xE2\x94\x80");
                if (c + 1 < ncols)
                    tui_buf_puts(o, "\xE2\x94\x80\xE2\x94\xBC\xE2\x94\x80");
            }
            put(o, '\n');
        }
    }
    tui_buf_free(&cells);
}

/* ---- blocks ---------------------------------------------------------- */

typedef struct md_st {
    tui_buf o;               /* the output */
    tui_buf para;            /* the paragraph being read, lines joined by a space */
    tui_buf item;            /* the list item or quote being read */
    int     width;
    int     last;            /* kind of the block before: 1 text, 2 list, 3 other */
    int     summary_pending;
    int     item_open, item_quote, item_ind;
    char    p1[96], p2[96];
} md_st;

static void gap(md_st *st, int kind) {
    if (st->last && !(kind == 2 && st->last == 2))
        blank_line(&st->o);
    st->last = kind;
}

static void flush_para(md_st *st) {
    if (!st->para.len)
        return;
    gap(st, 1);
    tui_buf cv;
    tui_buf_init(&cv);
    inline_conv(st->para.data, st->para.len, &cv);
    if (st->summary_pending) {
        /* The summary is set apart: bold, the span closed after the last line. */
        put(&st->o, TUI_MD_SUM);
        size_t mark = st->o.len;
        wrap_into(&st->o, cv.data, cv.len, st->width, "", "");
        if (st->o.len > mark)       /* (nothing was added after an allocation failure) */
            st->o.len--;
        tui_buf_puts(&st->o, "\x04\n");
    } else {
        wrap_into(&st->o, cv.data, cv.len, st->width, "", "");
    }
    st->summary_pending = 0;
    tui_buf_free(&cv);
    tui_buf_reset(&st->para);
}

static void flush_item(md_st *st) {
    if (!st->item_open)
        return;
    tui_buf cv;
    tui_buf_init(&cv);
    inline_conv(st->item.data, st->item.len, &cv);
    wrap_into(&st->o, cv.data, cv.len, st->width, st->p1, st->p2);
    tui_buf_free(&cv);
    tui_buf_reset(&st->item);
    st->item_open = 0;
}

static void flush_all(md_st *st) {
    flush_para(st);
    flush_item(st);
}

/* Starts a block of another kind than text: what is pending is written. */
static void begin_block(md_st *st, int kind) {
    flush_all(st);
    st->summary_pending = 0;
    gap(st, kind);
}

static int is_fence(const char *t, size_t tn) {
    return tn >= 3 && (!strncmp(t, "```", 3) || !strncmp(t, "~~~", 3));
}

static void code_block(md_st *st, const char **pp) {
    const char *p = *pp;
    int room = st->width - 4 < 1 ? 1 : st->width - 4;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t n = e ? (size_t)(e - p) : strlen(p);
        const char *nx = e ? e + 1 : p + n;
        size_t k = 0;
        while (k < n && p[k] == ' ')
            k++;
        if (is_fence(p + k, n - k)) {
            p = nx;
            break;
        }
        /* A line wider than the screen is split, never cut. */
        size_t off = 0;
        do {
            size_t take = 0;
            int cols = 0;
            while (off + take < n) {
                uint32_t cp;
                int kk = tui_utf8_decode(p + off + take, n - off - take, &cp);
                if (cols > 0 && cols + cp_cols(cp) > room)
                    break;
                cols += cp_cols(cp);
                take += (size_t)kk;
            }
            tui_buf_puts(&st->o, "    \x03");
            tui_buf_append(&st->o, p + off, take);
            tui_buf_puts(&st->o, "\x04\n");
            off += take;
        } while (off < n);
        p = nx;
    }
    *pp = p;
}

static void heading(md_st *st, const char *t, size_t tn, size_t h) {
    begin_block(st, 3);
    size_t a = h + 1, b = tn;
    while (b > a && (t[b - 1] == ' ' || t[b - 1] == '#'))
        b--;
    tui_buf cv;
    tui_buf_init(&cv);
    inline_conv(t + a, b - a, &cv);
    put(&st->o, TUI_MD_HEAD);
    tui_buf_append(&st->o, cv.data, cv.len);
    tui_buf_puts(&st->o, "\x04\n");
    if (h <= 2) {
        int w = vis_width(cv.data, cv.len);
        for (int k = 0; k < w; k++)
            put(&st->o, h == 1 ? '=' : '-');
        put(&st->o, '\n');
    }
    tui_buf_free(&cv);
}

static void block_pass(const char *md, md_st *st) {
    const char *p = md;
    while (*p) {
        const char *eol = strchr(p, '\n');
        size_t n = eol ? (size_t)(eol - p) : strlen(p);
        const char *next = eol ? eol + 1 : p + n;
        size_t ind = 0;
        while (ind < n && p[ind] == ' ')
            ind++;
        const char *t = p + ind;
        size_t tn = n - ind;

        if (blank(p, n)) {
            flush_all(st);
            blank_line(&st->o);
            p = next;
            continue;
        }

        if (is_fence(t, tn)) {
            begin_block(st, 3);
            p = next;
            code_block(st, &p);
            continue;
        }

        if (t[0] == '#') {
            size_t h = 0;
            while (h < tn && t[h] == '#')
                h++;
            if (h <= 6 && h < tn && t[h] == ' ') {
                heading(st, t, tn, h);
                p = next;
                continue;
            }
        }

        if (t[0] == '|') {
            begin_block(st, 3);
            const char *ls[TBL_MAX_ROWS];
            size_t ns[TBL_MAX_ROWS];
            int nl = 0;
            while (*p && nl < TBL_MAX_ROWS) {
                const char *e2 = strchr(p, '\n');
                size_t n2 = e2 ? (size_t)(e2 - p) : strlen(p);
                size_t k = 0;
                while (k < n2 && p[k] == ' ')
                    k++;
                if (k >= n2 || p[k] != '|')
                    break;
                ls[nl] = p + k;
                ns[nl++] = n2 - k;
                p = e2 ? e2 + 1 : p + n2;
            }
            table_out(&st->o, ls, ns, nl, st->width);
            continue;
        }

        size_t k = 0;
        int bullet = 0, numbered = 0;
        if ((t[0] == '-' || t[0] == '*' || t[0] == '+') && tn > 1 && t[1] == ' ') {
            bullet = 1;
            k = 2;
        } else {
            size_t d = 0;
            while (d < tn && t[d] >= '0' && t[d] <= '9')
                d++;
            if (d && d + 1 < tn && t[d] == '.' && t[d + 1] == ' ') {
                numbered = 1;
                k = d + 2;
            }
        }

        if (t[0] == '>') {
            k = tn > 1 && t[1] == ' ' ? 2 : 1;
            if (k >= tn) {                  /* an empty quote line ends the paragraph */
                flush_item(st);
            } else if (st->item_open && st->item_quote) {
                put(&st->item, ' ');        /* soft-wrapped quote: one paragraph */
                tui_buf_append(&st->item, t + k, tn - k);
            } else {
                begin_block(st, 3);
                snprintf(st->p1, sizeof st->p1, "\xE2\x94\x82 ");
                snprintf(st->p2, sizeof st->p2, "\xE2\x94\x82 ");
                tui_buf_append(&st->item, t + k, tn - k);
                st->item_open = 1;
                st->item_quote = 1;
            }
            p = next;
            continue;
        }

        if (bullet || numbered) {
            begin_block(st, 2);
            size_t pad = ind > 40 ? 40 : ind;
            if (bullet) {
                snprintf(st->p1, sizeof st->p1, "%*s\xE2\x80\xA2 ", (int)pad + 1, "");
                snprintf(st->p2, sizeof st->p2, "%*s", (int)pad + 3, "");
            } else {
                int d = (int)(k - 2);
                snprintf(st->p1, sizeof st->p1, "%*s%.*s. ", (int)pad + 1, "", d, t);
                snprintf(st->p2, sizeof st->p2, "%*s", (int)pad + 1 + d + 2, "");
            }
            tui_buf_append(&st->item, t + k, tn - k);
            st->item_open = 1;
            st->item_quote = 0;
            st->item_ind = (int)ind;
            p = next;
            continue;
        }

        /* An indented line after a list item goes on with the item. */
        if (st->item_open && !st->item_quote && (int)ind > st->item_ind) {
            put(&st->item, ' ');
            tui_buf_append(&st->item, t, tn);
            p = next;
            continue;
        }

        /* Paragraph text: the lines join with a space and wrap again. */
        flush_item(st);
        if (st->para.len)
            put(&st->para, ' ');
        tui_buf_append(&st->para, t, tn);
        p = next;
    }
    flush_all(st);
}

/* ---- markers to SGR or nothing ---------------------------------------- */

static const char *sgr_on(char m) {
    return m == TUI_MD_ITALIC ? "\x1b[3m" : m == TUI_MD_CODE ? "\x1b[36m" : "\x1b[1m";
}

/* Appends `in` to `out` with the markers turned into SGR or dropped. */
static void finish(const tui_buf *in, int ansi, tui_buf *out) {
    const char *s = in->data ? in->data : "";
    size_t base = out->len;
    char stack[16];
    int depth = 0;

    for (size_t i = 0; i < in->len; i++) {
        char c = s[i];
        if (c == '\n') {
            if (ansi && depth)
                tui_buf_puts(out, "\x1b[0m");
            put(out, '\n');
            for (int d = 0; ansi && d < depth; d++)
                tui_buf_puts(out, sgr_on(stack[d]));
        } else if (c == TUI_MD_OFF) {
            if (depth > 0)
                depth--;
            if (ansi) {
                tui_buf_puts(out, "\x1b[0m");
                for (int d = 0; d < depth; d++)
                    tui_buf_puts(out, sgr_on(stack[d]));
            }
        } else if (is_marker(c)) {
            if (depth < (int)sizeof(stack))
                stack[depth++] = c;
            if (ansi)
                tui_buf_puts(out, sgr_on(c));
        } else {
            put(out, c);
        }
    }
    if (ansi && depth)
        tui_buf_puts(out, "\x1b[0m");
    while (out->len > base && out->data[out->len - 1] == '\n')
        out->len--;
    if (out->data)
        out->data[out->len] = '\0';
}

/* The marked rendering of `md` at `width` into `out`; 0 or -1/ENOMEM. */
static int render_at(const char *md, int width, tui_buf *out) {
    if (!md || !md[0])
        return 0;
    /* Controls are shown as spaces (a tab too), so that no byte of the text
     * can be taken for a marker; a CR before LF is dropped. */
    md_st st;
    memset(&st, 0, sizeof st);
    tui_buf_init(&st.o);
    tui_buf_init(&st.para);
    tui_buf_init(&st.item);
    st.width = width;
    st.summary_pending = 1;
    tui_buf clean;
    tui_buf_init(&clean);
    for (const char *q = md; *q; q++) {
        unsigned char ch = (unsigned char)*q;
        if (ch == '\r')
            continue;
        put(&clean, ch < 0x20 && ch != '\n' ? ' ' : ch == 0x7f ? ' ' : (char)ch);
    }
    block_pass(clean.data ? clean.data : "", &st);
    while (st.o.len && st.o.data[st.o.len - 1] == '\n')
        st.o.len--;
    tui_buf_append(out, st.o.data, st.o.len);
    int failed = st.o.failed || st.para.failed || st.item.failed || clean.failed ||
                 out->failed;
    tui_buf_free(&st.o);
    tui_buf_free(&st.para);
    tui_buf_free(&st.item);
    tui_buf_free(&clean);
    if (failed) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int tui_md_render_marked(const char *md, int width, tui_buf *out) {
    if (!out) {
        errno = EINVAL;
        return -1;
    }
    return render_at(md, width < 20 ? 20 : width, out);
}

int tui_md_render(const char *md, int width, int ansi, tui_buf *out) {
    if (!out) {
        errno = EINVAL;
        return -1;
    }
    tui_buf mid;
    tui_buf_init(&mid);
    int rc = render_at(md, width < 20 ? 20 : width, &mid);
    if (rc == 0) {
        finish(&mid, ansi, out);
        if (out->failed) {
            errno = ENOMEM;
            rc = -1;
        }
    }
    tui_buf_free(&mid);
    return rc;
}

int tui_md_strip_marks(const char *marked, tui_buf *out) {
    if (!marked || !out) {
        errno = EINVAL;
        return -1;
    }
    tui_buf in;
    tui_buf_init(&in);
    tui_buf_puts(&in, marked);
    finish(&in, 0, out);
    int failed = in.failed || out->failed;
    tui_buf_free(&in);
    if (failed) {
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

/* ---- the view -------------------------------------------------------- */

void tui_mdview_init(tui_mdview *mv) {
    if (!mv) return;
    memset(mv, 0, sizeof(*mv));
    tui_buf_init(&mv->text);
    mv->dirty = 1;
}

void tui_mdview_free(tui_mdview *mv) {
    if (!mv) return;
    free(mv->src);
    free(mv->lines);
    tui_buf_free(&mv->text);
    tui_mdview_init(mv);
}

int tui_mdview_set_text(tui_mdview *mv, const char *md) {
    if (!mv) {
        errno = EINVAL;
        return -1;
    }
    size_t n = md ? strlen(md) : 0;
    char *copy = malloc(n + 1);
    if (!copy) return -1;
    if (n) memcpy(copy, md, n);
    copy[n] = '\0';
    free(mv->src);
    mv->src = copy;
    mv->top = 0;
    mv->dirty = 1;
    return 0;
}

void tui_mdview_set_plain(tui_mdview *mv, int plain) {
    if (mv) mv->plain = plain != 0;
}

int tui_mdview_lines(const tui_mdview *mv) { return mv ? mv->nlines : 0; }

int tui_mdview_top(const tui_mdview *mv) { return mv ? mv->top : 0; }

static int max_top(const tui_mdview *mv) {
    int m = mv->nlines - (mv->rows > 0 ? mv->rows : 1);
    return m < 0 ? 0 : m;
}

static void clamp_top(tui_mdview *mv) {
    if (mv->top > max_top(mv)) mv->top = max_top(mv);
    if (mv->top < 0) mv->top = 0;
}

void tui_mdview_scroll(tui_mdview *mv, int delta) {
    if (!mv) return;
    mv->top += delta;
    clamp_top(mv);
}

void tui_mdview_scroll_to(tui_mdview *mv, int line) {
    if (!mv) return;
    mv->top = line;
    clamp_top(mv);
}

void tui_mdview_scroll_end(tui_mdview *mv) {
    if (!mv) return;
    mv->top = max_top(mv);
}

int tui_mdview_handle(tui_mdview *mv, const tui_event *ev) {
    if (!mv || !ev) return 0;
    int page = mv->rows > 1 ? mv->rows - 1 : 1;
    switch (tui__nav_key(ev)) {
    case TUI_KEY_UP:   tui_mdview_scroll(mv, -1); return 1;
    case TUI_KEY_DOWN: tui_mdview_scroll(mv, 1); return 1;
    case TUI_KEY_PGUP: tui_mdview_scroll(mv, -page); return 1;
    case TUI_KEY_PGDN: tui_mdview_scroll(mv, page); return 1;
    case TUI_KEY_HOME: tui_mdview_scroll_to(mv, 0); return 1;
    case TUI_KEY_END:  tui_mdview_scroll_end(mv); return 1;
    default:           return 0;
    }
}

/* The spans open at a point: up to eight markers, three bits each, the
 * depth in the top four bits. */
static uint32_t spans_track(uint32_t sp, char m) {
    uint32_t depth = sp >> 28;
    if (m == TUI_MD_OFF) {
        if (depth > 0) {
            depth--;
            sp &= ~(7u << (3 * depth));
        }
    } else if (depth < 8) {
        sp |= (uint32_t)m << (3 * depth);
        depth++;
    }
    return (sp & 0x0FFFFFFFu) | (depth << 28);
}

static int add_line(tui_mdview *mv, size_t start, size_t len, uint32_t spans) {
    if (mv->nlines == mv->cap) {
        int cap = mv->cap ? mv->cap * 2 : 64;
        tui_mdline *l = realloc(mv->lines, (size_t)cap * sizeof *l);
        if (!l) return -1;
        mv->lines = l;
        mv->cap = cap;
    }
    mv->lines[mv->nlines].start = start;
    mv->lines[mv->nlines].len = len;
    mv->lines[mv->nlines].spans = spans;
    mv->nlines++;
    return 0;
}

/* Renders the Markdown at `cols` columns and indexes its lines. */
static int layout(tui_mdview *mv, int cols) {
    if (!mv->dirty && mv->width == cols) return mv->nlines;
    tui_buf_reset(&mv->text);
    mv->nlines = 0;
    mv->dirty = 1;
    if (render_at(mv->src, cols < 4 ? 4 : cols, &mv->text) != 0) return -1;
    const char *s = mv->text.data;
    uint32_t sp = 0, line_sp = 0;
    size_t start = 0;
    for (size_t i = 0; i <= mv->text.len; i++) {
        char c = i < mv->text.len ? s[i] : '\n';
        if (c == '\n') {
            if (i == mv->text.len && start == i) break;
            if (add_line(mv, start, i - start, line_sp) != 0) {
                mv->nlines = 0;
                errno = ENOMEM;
                return -1;
            }
            start = i + 1;
            line_sp = sp;
        } else if (is_marker(c)) {
            sp = spans_track(sp, c);
        }
    }
    mv->width = cols;
    mv->dirty = 0;
    return mv->nlines;
}

static tui_style spans_style(uint32_t sp, tui_style base, int plain) {
    if (plain) return base;
    tui_style s = base;
    uint32_t depth = sp >> 28;
    for (uint32_t d = 0; d < depth; d++) {
        switch ((sp >> (3 * d)) & 7u) {
        case TUI_MD_BOLD:
        case TUI_MD_SUM:
            s.attrs |= TUI_ATTR_BOLD;
            break;
        case TUI_MD_ITALIC:
            s.attrs |= TUI_ATTR_ITALIC;
            break;
        case TUI_MD_CODE:
            s.fg = TUI_STYLE_HINT.fg;
            break;
        default: {                      /* TUI_MD_HEAD */
            tui_style a = TUI_STYLE_ACCENT;
            s.fg = a.fg;
            s.attrs |= (uint16_t)(a.attrs | TUI_ATTR_BOLD);
            break;
        }
        }
    }
    return s;
}

typedef struct seg {
    tui_canvas c;
    int        row, col;
    char       buf[256];
    size_t     n;
} seg;

static void seg_flush(seg *g, tui_style st) {
    if (!g->n) return;
    g->buf[g->n] = '\0';
    g->col += tui_canvas_put(g->c, g->row, g->col, g->buf, st);
    g->n = 0;
}

static void draw_line(tui_canvas c, int row, const char *s, size_t n, uint32_t sp,
                      tui_style base, int plain) {
    seg g;
    g.c = c;
    g.row = row;
    g.col = 0;
    g.n = 0;
    tui_style cur = spans_style(sp, base, plain);
    for (size_t i = 0; i < n;) {
        if (is_marker(s[i])) {
            seg_flush(&g, cur);
            sp = spans_track(sp, s[i++]);
            cur = spans_style(sp, base, plain);
            continue;
        }
        uint32_t cp;
        int k = tui_utf8_decode(s + i, n - i, &cp);
        if (g.n + (size_t)k + 1 > sizeof g.buf) seg_flush(&g, cur);
        memcpy(g.buf + g.n, s + i, (size_t)k);
        g.n += (size_t)k;
        i += (size_t)k;
    }
    seg_flush(&g, cur);
}

void tui_mdview_render(tui_mdview *mv, tui_canvas c, tui_style st) {
    if (!mv) return;
    tui_canvas_clear(c, st);
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    if (rows <= 0 || cols <= 0) return;
    mv->rows = rows;
    /* The scroll bar takes the last column while the text is longer than
     * the area: render at the full width first, and again one narrower when
     * it does not fit (a narrower rendering is never shorter). */
    int text_cols = cols;
    if (cols > 1 && mv->width == cols - 1 && !mv->dirty && mv->nlines > rows) {
        text_cols = cols - 1;           /* the narrow rendering of the last frame still holds */
    } else {
        if (layout(mv, cols) < 0) return;
        if (mv->nlines > rows && cols > 1) {
            text_cols = cols - 1;
            mv->dirty = 1;
            if (layout(mv, text_cols) < 0) return;
        }
    }
    clamp_top(mv);
    tui_canvas body = tui_canvas_sub(c, tui_rect_make(0, 0, rows, text_cols));
    for (int r = 0; r < rows && mv->top + r < mv->nlines; r++) {
        const tui_mdline *l = &mv->lines[mv->top + r];
        draw_line(body, r, mv->text.data + l->start, l->len, l->spans, st, mv->plain);
    }
    if (text_cols < cols) tui__scrollbar(c, mv->nlines, mv->top, rows);
}
