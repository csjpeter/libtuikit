/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* The SGR view: a scrolling read-only text whose ANSI SGR runs are drawn as styles. */

#include "tuikit/sgrview.h"
#include "internal.h"
#include "tuikit/text.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

void tui_sgrview_init(tui_sgrview *sv) {
    if (!sv) return;
    memset(sv, 0, sizeof(*sv));
    sv->dirty = 1;
}

void tui_sgrview_free(tui_sgrview *sv) {
    if (!sv) return;
    free(sv->text);
    free(sv->lines);
    tui_sgrview_init(sv);
}

int tui_sgrview_set_text(tui_sgrview *sv, const char *text) {
    if (!sv) {
        errno = EINVAL;
        return -1;
    }
    size_t n = text ? strlen(text) : 0;
    char *copy = malloc(n + 1);
    if (!copy) return -1;
    if (n) memcpy(copy, text, n);
    copy[n] = '\0';
    free(sv->text);
    sv->text = copy;
    sv->len = n;
    sv->top = 0;
    sv->dirty = 1;
    return 0;
}

int tui_sgrview_lines(const tui_sgrview *sv) { return sv ? sv->nlines : 0; }

int tui_sgrview_top(const tui_sgrview *sv) { return sv ? sv->top : 0; }

static int max_top(const tui_sgrview *sv) {
    int m = sv->nlines - (sv->rows > 0 ? sv->rows : 1);
    return m < 0 ? 0 : m;
}

static void clamp_top(tui_sgrview *sv) {
    if (sv->top > max_top(sv)) sv->top = max_top(sv);
    if (sv->top < 0) sv->top = 0;
}

void tui_sgrview_scroll(tui_sgrview *sv, int delta) {
    if (!sv) return;
    sv->top += delta;
    clamp_top(sv);
}

void tui_sgrview_scroll_to(tui_sgrview *sv, int line) {
    if (!sv) return;
    sv->top = line;
    clamp_top(sv);
}

void tui_sgrview_scroll_end(tui_sgrview *sv) {
    if (!sv) return;
    sv->top = max_top(sv);
}

int tui_sgrview_handle(tui_sgrview *sv, const tui_event *ev) {
    if (!sv || !ev) return 0;
    int page = sv->rows > 1 ? sv->rows - 1 : 1;
    switch (tui__nav_key(ev)) {
    case TUI_KEY_UP:   tui_sgrview_scroll(sv, -1); return 1;
    case TUI_KEY_DOWN: tui_sgrview_scroll(sv, 1); return 1;
    case TUI_KEY_PGUP: tui_sgrview_scroll(sv, -page); return 1;
    case TUI_KEY_PGDN: tui_sgrview_scroll(sv, page); return 1;
    case TUI_KEY_HOME: tui_sgrview_scroll_to(sv, 0); return 1;
    case TUI_KEY_END:  tui_sgrview_scroll_end(sv); return 1;
    default:           return 0;
    }
}

/* ---- escape sequences ------------------------------------------------ */

/* The length of the CSI sequence at s (ESC [ parameters intermediates
 * final); an unterminated one runs to the end.  0 when s is no CSI. */
static size_t csi_len(const char *s, size_t n) {
    if (n < 2 || s[0] != '\x1b' || s[1] != '[') return 0;
    size_t i = 2;
    while (i < n && (unsigned char)s[i] >= 0x20 && (unsigned char)s[i] <= 0x3F) i++;
    if (i < n && (unsigned char)s[i] >= 0x40 && (unsigned char)s[i] <= 0x7E) return i + 1;
    return n;
}

/* Reads the next parameter of an SGR list (digits up to ';' or ':'). */
static int next_param(const char *p, size_t n, size_t *i, int *out) {
    if (*i > n) return 0;
    int v = 0;
    while (*i < n && p[*i] >= '0' && p[*i] <= '9') {
        if (v < 100000) v = v * 10 + (p[*i] - '0');
        (*i)++;
    }
    if (*i < n && (p[*i] == ';' || p[*i] == ':')) (*i)++;
    else *i = n + 1;
    *out = v;
    return 1;
}

/* An extended colour (38/48) after its code: 5;n or 2;r;g;b. */
static int extended(const char *p, size_t n, size_t *i, tui_color *col) {
    int mode, a, b, c;
    if (!next_param(p, n, i, &mode)) return 0;
    if (mode == 5) {
        if (!next_param(p, n, i, &a)) return 0;
        *col = TUI_COLOR_256(a);
        return 1;
    }
    if (mode == 2) {
        if (!next_param(p, n, i, &a) || !next_param(p, n, i, &b) || !next_param(p, n, i, &c))
            return 0;
        *col = TUI_COLOR_RGB(a, b, c);
        return 1;
    }
    return 0;
}

/* Applies the parameters of one SGR sequence (without ESC [ and m) to st. */
static void sgr_apply(tui_style *st, const char *p, size_t n) {
    size_t i = 0;
    int code;
    if (n == 0) {
        *st = TUI_STYLE_DEFAULT;
        return;
    }
    while (next_param(p, n, &i, &code)) {
        tui_color col;
        switch (code) {
        case 0:  *st = TUI_STYLE_DEFAULT; break;
        case 1:  st->attrs |= TUI_ATTR_BOLD; break;
        case 2:  st->attrs |= TUI_ATTR_DIM; break;
        case 3:  st->attrs |= TUI_ATTR_ITALIC; break;
        case 4:  st->attrs |= TUI_ATTR_UNDERLINE; break;
        case 5:
        case 6:  st->attrs |= TUI_ATTR_BLINK; break;
        case 7:  st->attrs |= TUI_ATTR_REVERSE; break;
        case 9:  st->attrs |= TUI_ATTR_STRIKE; break;
        case 22: st->attrs &= (uint16_t)~(TUI_ATTR_BOLD | TUI_ATTR_DIM); break;
        case 23: st->attrs &= (uint16_t)~TUI_ATTR_ITALIC; break;
        case 24: st->attrs &= (uint16_t)~TUI_ATTR_UNDERLINE; break;
        case 25: st->attrs &= (uint16_t)~TUI_ATTR_BLINK; break;
        case 27: st->attrs &= (uint16_t)~TUI_ATTR_REVERSE; break;
        case 29: st->attrs &= (uint16_t)~TUI_ATTR_STRIKE; break;
        case 38:
            if (extended(p, n, &i, &col)) st->fg = col;
            break;
        case 48:
            if (extended(p, n, &i, &col)) st->bg = col;
            break;
        case 39: st->fg = TUI_COLOR_DEFAULT; break;
        case 49: st->bg = TUI_COLOR_DEFAULT; break;
        default:
            if (code >= 30 && code <= 37) st->fg = TUI_COLOR_16(code - 30);
            else if (code >= 90 && code <= 97) st->fg = TUI_COLOR_16(code - 90 + 8);
            else if (code >= 40 && code <= 47) st->bg = TUI_COLOR_16(code - 40);
            else if (code >= 100 && code <= 107) st->bg = TUI_COLOR_16(code - 100 + 8);
            break;
        }
    }
}

/* Applies the CSI sequence s[0..n) to st when it is an SGR one. */
static void csi_apply(tui_style *st, const char *s, size_t n) {
    if (n >= 3 && s[n - 1] == 'm')
        sgr_apply(st, s + 2, n - 3);
}

/* ---- wrapping -------------------------------------------------------- */

/* The scan of the text for the SGR state: `st` holds it at offset `pos`. */
typedef struct scan {
    tui_style st;
    size_t    pos;
} scan;

static void scan_to(scan *sc, const char *t, size_t to) {
    while (sc->pos < to) {
        size_t k = csi_len(t + sc->pos, to - sc->pos);
        if (k) {
            csi_apply(&sc->st, t + sc->pos, k);
            sc->pos += k;
        } else {
            sc->pos++;
        }
    }
}

static int add_line(tui_sgrview *sv, scan *sc, size_t start, size_t len) {
    if (sv->nlines == sv->cap) {
        int cap = sv->cap ? sv->cap * 2 : 64;
        tui_sgrline *l = realloc(sv->lines, (size_t)cap * sizeof *l);
        if (!l) return -1;
        sv->lines = l;
        sv->cap = cap;
    }
    scan_to(sc, sv->text, start);
    sv->lines[sv->nlines].start = start;
    sv->lines[sv->nlines].len = len;
    sv->lines[sv->nlines].style = sc->st;
    sv->nlines++;
    return 0;
}

static int char_width(uint32_t cp, int col) {
    if (cp == '\t') return 8 - col % 8;
    if (cp < 0x20 || cp == 0x7F) return 1;      /* drawn as U+00B7 */
    int w = tui_cp_width(cp);
    return w < 0 ? 1 : w;
}

/* Wraps the logical line [b, e) into sv's line index. */
static int wrap_line(tui_sgrview *sv, scan *sc, size_t b, size_t e, int cols) {
    const char *t = sv->text;
    if (b == e) return add_line(sv, sc, b, 0);
    size_t pos = b;
    while (pos < e) {
        int col = 0;
        size_t j = pos;
        size_t space_at = 0;
        int have_space = 0;
        while (j < e) {
            size_t k = csi_len(t + j, e - j);
            if (k) {                    /* a style: no column, never a break */
                j += k;
                continue;
            }
            uint32_t cp;
            k = (size_t)tui_utf8_decode(t + j, e - j, &cp);
            int w = char_width(cp, col);
            if ((cp == ' ' || cp == '\t') && j > pos) {
                space_at = j;
                have_space = 1;
            }
            if (col > 0 && col + w > cols) break;
            col += w;
            j += k;
        }
        if (j >= e) return add_line(sv, sc, pos, e - pos);
        if (have_space) {
            if (add_line(sv, sc, pos, space_at - pos) != 0) return -1;
            pos = space_at;
            while (pos < e && (t[pos] == ' ' || t[pos] == '\t')) pos++;
        } else {
            if (add_line(sv, sc, pos, j - pos) != 0) return -1;
            pos = j;
        }
    }
    return 0;
}

static int layout(tui_sgrview *sv, int cols) {
    if (!sv->dirty && sv->width == cols) return sv->nlines;
    if (cols < 1) cols = 1;
    sv->nlines = 0;
    sv->dirty = 1;
    scan sc = { TUI_STYLE_DEFAULT, 0 };
    size_t i = 0;
    while (i < sv->len) {
        const char *nl = memchr(sv->text + i, '\n', sv->len - i);
        size_t e = nl ? (size_t)(nl - sv->text) : sv->len;
        size_t le = (e > i && sv->text[e - 1] == '\r') ? e - 1 : e;
        if (wrap_line(sv, &sc, i, le, cols) != 0) {
            sv->nlines = 0;
            errno = ENOMEM;
            return -1;
        }
        i = e + 1;
    }
    sv->width = cols;
    sv->dirty = 0;
    return sv->nlines;
}

/* ---- drawing --------------------------------------------------------- */

/* The style of the text laid over the base style. */
static tui_style overlay(tui_style base, tui_style sgr) {
    tui_style s = base;
    if (sgr.fg != TUI_COLOR_DEFAULT) s.fg = sgr.fg;
    if (sgr.bg != TUI_COLOR_DEFAULT) s.bg = sgr.bg;
    s.attrs |= sgr.attrs;
    return s;
}

static void draw_line(tui_canvas c, int row, const char *s, size_t n, tui_style sgr,
                      tui_style base) {
    int col = 0;
    size_t i = 0;
    while (i < n) {
        size_t k = csi_len(s + i, n - i);
        if (k) {
            csi_apply(&sgr, s + i, k);
            i += k;
            continue;
        }
        uint32_t cp;
        k = (size_t)tui_utf8_decode(s + i, n - i, &cp);
        tui_style cur = overlay(base, sgr);
        if (cp == '\t') {
            int w = 8 - col % 8;
            for (int x = 0; x < w; x++) tui_canvas_put(c, row, col + x, " ", cur);
            col += w;
        } else if (cp < 0x20 || cp == 0x7F) {
            tui_canvas_put(c, row, col, "\xC2\xB7", cur);
            col++;
        } else {
            char one[8];
            memcpy(one, s + i, k);
            one[k] = '\0';
            tui_canvas_put(c, row, col, one, cur);
            col += char_width(cp, col);
        }
        i += k;
    }
}

void tui_sgrview_render(tui_sgrview *sv, tui_canvas c, tui_style st) {
    if (!sv) return;
    tui_canvas_clear(c, st);
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    if (rows <= 0 || cols <= 0) return;
    sv->rows = rows;
    /* The scroll bar takes the last column while the text is longer than
     * the area: wrap at the full width first, and again one narrower when
     * it does not fit (a narrower wrap is never shorter). */
    int text_cols = cols;
    if (cols > 1 && sv->width == cols - 1 && !sv->dirty && sv->nlines > rows) {
        text_cols = cols - 1;
    } else {
        if (layout(sv, cols) < 0) return;
        if (sv->nlines > rows && cols > 1) {
            text_cols = cols - 1;
            sv->dirty = 1;
            if (layout(sv, text_cols) < 0) return;
        }
    }
    clamp_top(sv);
    tui_canvas body = tui_canvas_sub(c, tui_rect_make(0, 0, rows, text_cols));
    for (int r = 0; r < rows && sv->top + r < sv->nlines; r++) {
        const tui_sgrline *l = &sv->lines[sv->top + r];
        draw_line(body, r, sv->text + l->start, l->len, l->style, st);
    }
    if (text_cols < cols) tui__scrollbar(c, sv->nlines, sv->top, rows);
}
