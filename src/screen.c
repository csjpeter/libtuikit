/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* The cell grid and its renderer. */

/*
 * The cell grid and its renderer.
 *
 * Output uses only sequences every xterm-compatible terminal has had for
 * decades: CUP (CSI row ; col H), SGR (CSI ... m), ED 2 (CSI 2 J), DECTCEM
 * (CSI ? 25 h/l), and the synchronized-update brackets CSI ? 2026 h/l that
 * terminals without them ignore.  No relative cursor movement, no scrolling
 * regions, no erase-to-end-of-line: every changed cell is written, so the
 * result does not depend on how the terminal treats the space around it.
 */

#include "internal.h"
#include "tuikit/screen.h"
#include "tuikit/text.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#define COMB_MAX 2

typedef struct {
    uint32_t  cp;              /* the character; 0 in a wide trailer */
    uint32_t  comb[COMB_MAX];  /* combining marks drawn with it */
    uint8_t   width;           /* 1, 2, or 0 for the trailer of a wide one */
    tui_style style;
} cell;

struct tui_screen {
    int   rows, cols;
    cell *back, *front;
    int   full;                      /* the terminal's content is unknown */
    int   sync;
    int   cur_row, cur_col, cur_visible;
    int   shown_cursor_visible;      /* -1: unknown */
    int   shown_row, shown_col;      /* where the cursor was left */
};

static cell blank(tui_style st) {
    cell c;
    memset(&c, 0, sizeof(c));
    c.cp = ' ';
    c.width = 1;
    c.style = st;
    return c;
}

static int cell_eq(const cell *a, const cell *b) {
    return a->cp == b->cp && a->width == b->width &&
           a->comb[0] == b->comb[0] && a->comb[1] == b->comb[1] &&
           tui_style_eq(a->style, b->style);
}

static int alloc_grids(int rows, int cols, cell **back, cell **front) {
    if (rows <= 0 || cols <= 0 || (size_t)rows > ((size_t)-1 / sizeof(cell)) / (size_t)cols) {
        errno = EINVAL;
        return -1;
    }
    size_t n = (size_t)rows * (size_t)cols;
    *back = malloc(n * sizeof(cell));
    *front = malloc(n * sizeof(cell));
    if (!*back || !*front) {
        free(*back);
        free(*front);
        errno = ENOMEM;
        return -1;
    }
    cell b = blank(TUI_STYLE_DEFAULT);
    for (size_t i = 0; i < n; i++) (*back)[i] = (*front)[i] = b;
    return 0;
}

tui_screen *tui_screen_new(int rows, int cols) {
    tui_screen *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    if (alloc_grids(rows, cols, &s->back, &s->front) != 0) {
        int e = errno;
        free(s);
        errno = e;
        return NULL;
    }
    s->rows = rows;
    s->cols = cols;
    s->full = 1;
    s->sync = 1;
    s->shown_cursor_visible = -1;
    return s;
}

void tui_screen_free(tui_screen *s) {
    if (!s) return;
    free(s->back);
    free(s->front);
    free(s);
}

int tui_screen_resize(tui_screen *s, int rows, int cols) {
    if (!s) {
        errno = EINVAL;
        return -1;
    }
    cell *back, *front;
    if (alloc_grids(rows, cols, &back, &front) != 0) return -1;
    free(s->back);
    free(s->front);
    s->back = back;
    s->front = front;
    s->rows = rows;
    s->cols = cols;
    s->full = 1;
    s->shown_cursor_visible = -1;
    if (s->cur_row >= rows) s->cur_row = rows - 1;
    if (s->cur_col >= cols) s->cur_col = cols - 1;
    return 0;
}

int tui_screen_rows(const tui_screen *s) { return s ? s->rows : 0; }
int tui_screen_cols(const tui_screen *s) { return s ? s->cols : 0; }

static cell *at(tui_screen *s, int r, int c) {
    return &s->back[(size_t)r * (size_t)s->cols + (size_t)c];
}

void tui_screen_clear(tui_screen *s, tui_style style) {
    if (!s) return;
    size_t n = (size_t)s->rows * (size_t)s->cols;
    cell b = blank(style);
    for (size_t i = 0; i < n; i++) s->back[i] = b;
}

/* Before a cell is overwritten: a wide character it is half of loses its
 * other half too, which becomes a blank in that character's style. */
static void break_wide(tui_screen *s, int r, int c) {
    cell *x = at(s, r, c);
    if (x->width == 0 && c > 0) {
        cell *lead = at(s, r, c - 1);
        *lead = blank(lead->style);
    } else if (x->width == 2 && c + 1 < s->cols) {
        cell *tr = at(s, r, c + 1);
        *tr = blank(x->style);
    }
}

static void attach_mark(cell *x, uint32_t cp) {
    for (int i = 0; i < COMB_MAX; i++) {
        if (x->comb[i] == 0) {
            x->comb[i] = cp;
            return;
        }
    }
    /* More marks than a cell keeps: the rest are not drawn. */
}

int tui_screen_put(tui_screen *s, int row, int col, int max_cols,
                   const char *utf8, tui_style style) {
    if (!s || !utf8 || row < 0 || row >= s->rows || col >= s->cols) return 0;
    if (col < 0) return 0;
    int limit = s->cols;
    if (max_cols > 0 && col + max_cols < limit) limit = col + max_cols;
    int start = col;
    int last = -1;                    /* column of the last character drawn */
    size_t n = strlen(utf8);
    size_t i = 0;
    while (i < n) {
        uint32_t cp;
        int k = tui_utf8_decode(utf8 + i, n - i, &cp);
        i += (size_t)k;
        if (tui__cp_is_bidi_control(cp)) continue;   /* never drawn */
        cp = tui_cp_sanitize(cp);
        int w = tui_cp_width(cp);
        if (w == 0) {
            /* A mark joins the character before it in the same text; with
             * none drawn yet it has nothing to join and is not drawn, so
             * a clipped call never touches a cell outside its columns. */
            if (last >= 0) attach_mark(at(s, row, last), cp);
            continue;
        }
        if (col + w > limit) break;
        break_wide(s, row, col);
        if (w == 2) break_wide(s, row, col + 1);
        cell *x = at(s, row, col);
        memset(x, 0, sizeof(*x));
        x->cp = cp;
        x->width = (uint8_t)w;
        x->style = style;
        if (w == 2) {
            cell *tr = at(s, row, col + 1);
            memset(tr, 0, sizeof(*tr));
            tr->width = 0;
            tr->style = style;
        }
        last = col;
        col += w;
    }
    return col - start;
}

void tui_screen_fill(tui_screen *s, int row, int col, int n, uint32_t cp,
                     tui_style style) {
    if (!s || row < 0 || row >= s->rows || n <= 0) return;
    if (col < 0) {
        n += col;
        col = 0;
    }
    if (col >= s->cols || n <= 0) return;
    if (col + n > s->cols) n = s->cols - col;
    cp = tui_cp_sanitize(cp);
    if (tui_cp_width(cp) != 1) cp = ' ';
    for (int c = col; c < col + n; c++) {
        break_wide(s, row, c);
        cell *x = at(s, row, c);
        *x = blank(style);
        x->cp = cp;
    }
}

void tui_screen_set_cursor(tui_screen *s, int row, int col, int visible) {
    if (!s) return;
    if (row < 0) row = 0;
    if (col < 0) col = 0;
    if (row >= s->rows) row = s->rows - 1;
    if (col >= s->cols) col = s->cols - 1;
    s->cur_row = row;
    s->cur_col = col;
    s->cur_visible = visible ? 1 : 0;
}

void tui_screen_invalidate(tui_screen *s) {
    if (!s) return;
    s->full = 1;
    s->shown_cursor_visible = -1;
}

void tui_screen_set_sync(tui_screen *s, int on) {
    if (s) s->sync = on ? 1 : 0;
}

/* ── Rendering ──────────────────────────────────────────────────────── */

static void sgr_color(tui_buf *out, tui_color c, int bg) {
    uint32_t kind = c >> 24;
    uint32_t v = c & 0xFFFFFFu;
    if (kind == 1) {
        if (v < 8) tui_buf_printf(out, ";%u", (bg ? 40u : 30u) + v);
        else       tui_buf_printf(out, ";%u", (bg ? 100u : 90u) + v - 8);
    } else if (kind == 2) {
        tui_buf_printf(out, ";%u;5;%u", bg ? 48u : 38u, v);
    } else if (kind == 3) {
        tui_buf_printf(out, ";%u;2;%u;%u;%u", bg ? 48u : 38u,
                       v >> 16, (v >> 8) & 0xFFu, v & 0xFFu);
    }
}

/* SGR always starts from a reset, so no attribute of the previous cell
 * can survive into this one. */
static void sgr(tui_buf *out, tui_style st) {
    tui_buf_puts(out, "\033[0");
    if (st.attrs & TUI_ATTR_BOLD)      tui_buf_puts(out, ";1");
    if (st.attrs & TUI_ATTR_DIM)       tui_buf_puts(out, ";2");
    if (st.attrs & TUI_ATTR_ITALIC)    tui_buf_puts(out, ";3");
    if (st.attrs & TUI_ATTR_UNDERLINE) tui_buf_puts(out, ";4");
    if (st.attrs & TUI_ATTR_REVERSE)   tui_buf_puts(out, ";7");
    if (st.attrs & TUI_ATTR_BLINK)     tui_buf_puts(out, ";5");
    if (st.attrs & TUI_ATTR_STRIKE)    tui_buf_puts(out, ";9");
    sgr_color(out, st.fg, 0);
    sgr_color(out, st.bg, 1);
    tui_buf_puts(out, "m");
}

static void put_cp(tui_buf *out, uint32_t cp) {
    char b[4];
    int k = tui_utf8_encode(cp, b);
    tui_buf_append(out, b, (size_t)k);
}

static int is_default_blank(const cell *c) {
    return c->cp == ' ' && c->width == 1 && c->comb[0] == 0 &&
           tui_style_eq(c->style, TUI_STYLE_DEFAULT);
}

long tui_screen_render(tui_screen *s, tui_buf *out) {
    if (!s || !out) {
        errno = EINVAL;
        return -1;
    }
    if (out->failed) {
        errno = ENOMEM;
        return -1;
    }
    size_t start = out->len;
    int full = s->full;
    int pos_r = -1, pos_c = -1;         /* where the terminal's cursor is */
    /* Every frame ends with the default style (or with a reset), so the
     * terminal starts each one there; after foreign output the program
     * invalidates, and the full frame begins with a reset of its own. */
    tui_style cur = TUI_STYLE_DEFAULT;
    int style_known = 1;
    int opened = 0;

#define OPEN_FRAME() do { \
        if (!opened) { \
            opened = 1; \
            if (s->sync) tui_buf_puts(out, "\033[?2026h"); \
        } \
    } while (0)

    if (full) {
        /* Start from a known state: no attributes, an empty screen.  Then
         * only the cells that are not default blanks need writing. */
        OPEN_FRAME();
        tui_buf_puts(out, "\033[0m\033[H\033[2J");
        style_known = 1;
        pos_r = 0;
        pos_c = 0;
    }

    for (int r = 0; r < s->rows; r++) {
        for (int c = 0; c < s->cols; c++) {
            size_t idx = (size_t)r * (size_t)s->cols + (size_t)c;
            cell *b = &s->back[idx];
            cell *f = &s->front[idx];
            if (b->width == 0) continue;          /* drawn with its lead */
            int changed = full ? !is_default_blank(b) : !cell_eq(b, f);
            if (!changed && b->width == 2 && c + 1 < s->cols && !full)
                changed = !cell_eq(&s->back[idx + 1], &s->front[idx + 1]);
            if (!changed) continue;
            OPEN_FRAME();
            if (r != pos_r || c != pos_c) tui_buf_printf(out, "\033[%d;%dH", r + 1, c + 1);
            if (!style_known || !tui_style_eq(cur, b->style)) {
                sgr(out, b->style);
                cur = b->style;
                style_known = 1;
            }
            put_cp(out, b->cp);
            for (int m = 0; m < COMB_MAX && b->comb[m]; m++) put_cp(out, b->comb[m]);
            pos_r = r;
            pos_c = c + b->width;
            /* After the last column the terminal's cursor position is not
             * certain (pending wrap): position explicitly next time. */
            if (pos_c >= s->cols) pos_r = -1;
        }
    }

    if (opened && style_known && !tui_style_eq(cur, TUI_STYLE_DEFAULT))
        tui_buf_puts(out, "\033[0m");
    if (s->cur_visible) {
        /* Drawing moves the cursor; put it where the program wants it. */
        if (opened || s->shown_cursor_visible != 1 ||
            s->shown_row != s->cur_row || s->shown_col != s->cur_col) {
            OPEN_FRAME();
            tui_buf_printf(out, "\033[%d;%dH", s->cur_row + 1, s->cur_col + 1);
        }
        if (s->shown_cursor_visible != 1) tui_buf_puts(out, "\033[?25h");
    } else if (s->shown_cursor_visible != 0) {
        OPEN_FRAME();
        tui_buf_puts(out, "\033[?25l");
    }
    if (opened && s->sync) tui_buf_puts(out, "\033[?2026l");
#undef OPEN_FRAME

    if (out->failed) {
        /* The bytes may never reach the terminal: assume nothing. */
        s->full = 1;
        s->shown_cursor_visible = -1;
        errno = ENOMEM;
        return -1;
    }
    memcpy(s->front, s->back, (size_t)s->rows * (size_t)s->cols * sizeof(cell));
    s->full = 0;
    s->shown_cursor_visible = s->cur_visible;
    s->shown_row = s->cur_row;
    s->shown_col = s->cur_col;
    return (long)(out->len - start);
}
