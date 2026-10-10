/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-05 */
/* The multi-line editor. */

/*
 * The multi-line editor.  Positions are byte offsets kept on character
 * boundaries; a combining mark moves and is deleted with the character it
 * belongs to, as in tui_lineedit.
 */

#include "tuikit/textedit.h"
#include "tuikit/buf.h"
#include "tuikit/text.h"

#include <stdio.h>
#include <string.h>

#define TAB_STOP 8

void tui_textedit_set_text(tui_textedit *te, const char *text) {
    if (!te || !te->buf) return;
    te->len = te->cur = 0;
    te->buf[0] = '\0';
    if (text) tui_textedit_insert(te, text, strlen(text));
    te->cur = 0;
    te->want_col = -1;
    te->top = te->left = 0;
    te->dirty = 0;
}

void tui_textedit_init(tui_textedit *te, char *buf, size_t size, const char *initial) {
    if (!te) return;
    memset(te, 0, sizeof(*te));
    te->want_col = -1;
    if (!buf || size == 0) return;
    te->buf = buf;
    te->size = size;
    tui_textedit_set_text(te, initial);
}

/* ── Positions ───────────────────────────────────────────────────────── */

static uint32_t cp_at(const tui_textedit *te, size_t pos, int *bytes) {
    uint32_t cp = 0;
    int k = tui_utf8_decode(te->buf + pos, te->len - pos, &cp);
    if (bytes) *bytes = k;
    return cp;
}

static size_t next_pos(const tui_textedit *te, size_t pos) {
    if (pos >= te->len) return te->len;
    int k;
    cp_at(te, pos, &k);
    pos += (size_t)k;
    while (pos < te->len && te->buf[pos] != '\n' && tui_cp_width(cp_at(te, pos, &k)) == 0)
        pos += (size_t)k;
    return pos;
}

static size_t prev_cp(const tui_textedit *te, size_t pos) {
    if (pos == 0) return 0;
    pos--;
    while (pos > 0 && ((unsigned char)te->buf[pos] & 0xC0) == 0x80) pos--;
    return pos;
}

static size_t prev_pos(const tui_textedit *te, size_t pos) {
    pos = prev_cp(te, pos);
    while (pos > 0 && te->buf[pos] != '\n' && tui_cp_width(cp_at(te, pos, NULL)) == 0)
        pos = prev_cp(te, pos);
    return pos;
}

static size_t line_start(const tui_textedit *te, size_t pos) {
    while (pos > 0 && te->buf[pos - 1] != '\n') pos--;
    return pos;
}

static size_t line_end(const tui_textedit *te, size_t pos) {
    while (pos < te->len && te->buf[pos] != '\n') pos++;
    return pos;
}

/* Width of the character at @p pos when it starts at column @p col. */
static int width_at(const tui_textedit *te, size_t pos, int col, int *bytes) {
    uint32_t cp = cp_at(te, pos, bytes);
    if (cp == '\t') return TAB_STOP - col % TAB_STOP;
    int w = tui_cp_width(cp);
    return w < 0 ? 1 : w;
}

/* Display column of @p pos within its line. */
static int col_of(const tui_textedit *te, size_t pos) {
    int col = 0;
    for (size_t p = line_start(te, pos); p < pos;) {
        int k;
        col += width_at(te, p, col, &k);
        p += (size_t)k;
    }
    return col;
}

/* The last character boundary of the line from @p start whose column is
 * at most @p want. */
static size_t pos_at_col(const tui_textedit *te, size_t start, int want) {
    size_t p = start;
    int col = 0;
    while (p < te->len && te->buf[p] != '\n') {
        int k;
        int w = width_at(te, p, col, &k);
        if (col + w > want) break;
        col += w;
        p = next_pos(te, p);
    }
    return p;
}

static int line_of(const tui_textedit *te, size_t pos) {
    int n = 0;
    for (size_t p = 0; p < pos; p++)
        if (te->buf[p] == '\n') n++;
    return n;
}

/* Start of line @p n (clamped to the last line). */
static size_t start_of_line(const tui_textedit *te, int n) {
    size_t p = 0;
    for (int i = 0; i < n; i++) {
        size_t e = line_end(te, p);
        if (e >= te->len) break;
        p = e + 1;
    }
    return p;
}

int tui_textedit_lines(const tui_textedit *te) {
    if (!te || !te->buf) return 0;
    return line_of(te, te->len) + 1;
}

void tui_textedit_cursor(const tui_textedit *te, int *row, int *col) {
    int r = 0, c = 0;
    if (te && te->buf) {
        r = line_of(te, te->cur);
        c = col_of(te, te->cur);
    }
    if (row) *row = r;
    if (col) *col = c;
}

/* ── Editing ─────────────────────────────────────────────────────────── */

static void erase(tui_textedit *te, size_t a, size_t b) {
    if (b <= a) return;
    memmove(te->buf + a, te->buf + b, te->len - b + 1);
    te->len -= b - a;
    te->cur = a;
    te->dirty = 1;
}

size_t tui_textedit_insert(tui_textedit *te, const char *s, size_t n) {
    if (!te || !te->buf || !s) return 0;
    size_t done = 0;
    te->truncated = 0;
    te->want_col = -1;
    size_t i = 0;
    while (i < n) {
        const char *chunk = s + i;
        size_t clen;
        uint32_t cp;
        if (s[i] == '\r') {
            chunk = "\n";
            clen = 1;
            i += (i + 1 < n && s[i + 1] == '\n') ? 2 : 1;
        } else {
            int k = tui_utf8_decode(s + i, n - i, &cp);
            i += (size_t)k;
            if (cp != '\n' && cp != '\t' && tui_cp_width(cp) < 0) continue;  /* a control */
            if (cp == TUI_CP_REPLACEMENT && !(k == 3 && memcmp(chunk, "\xEF\xBF\xBD", 3) == 0)) {
                chunk = "\xEF\xBF\xBD";              /* malformed input: U+FFFD */
                clen = 3;
            } else {
                clen = (size_t)k;
            }
        }
        if (te->len + clen >= te->size) {
            te->truncated = 1;
            break;
        }
        memmove(te->buf + te->cur + clen, te->buf + te->cur, te->len - te->cur + 1);
        memcpy(te->buf + te->cur, chunk, clen);
        te->cur += clen;
        te->len += clen;
        done += clen;
        te->dirty = 1;
    }
    return done;
}

static void move_vertical(tui_textedit *te, int delta) {
    if (te->want_col < 0) te->want_col = col_of(te, te->cur);
    int line = line_of(te, te->cur) + delta;
    int last = tui_textedit_lines(te) - 1;
    if (line < 0) line = 0;
    if (line > last) line = last;
    te->cur = pos_at_col(te, start_of_line(te, line), te->want_col);
}

int tui_textedit_handle(tui_textedit *te, const tui_event *ev) {
    if (!te || !te->buf || !ev) return 0;
    if (ev->type == TUI_EV_PASTE) {
        tui_textedit_insert(te, ev->paste ? ev->paste : "", ev->paste_len);
        return 1;
    }
    if (ev->type != TUI_EV_KEY) return 0;
    const unsigned C = TUI_MOD_CTRL;
    int page = te->rows > 0 ? te->rows : 1;

    if (ev->key == TUI_KEY_CHAR) {
        if (ev->mods == C) {
            switch (ev->cp) {
            case 'a': te->cur = line_start(te, te->cur); break;
            case 'e': te->cur = line_end(te, te->cur); break;
            case 'k': {
                size_t e = line_end(te, te->cur);
                if (e == te->cur && e < te->len) e++;          /* join the next line */
                erase(te, te->cur, e);
                break;
            }
            default: return 0;
            }
            te->want_col = -1;
            return 1;
        }
        if (ev->mods & (C | TUI_MOD_ALT)) return 0;
        tui_textedit_insert(te, ev->utf8, strlen(ev->utf8));
        return 1;
    }

    int keep_col = 0;
    switch (ev->key) {
    case TUI_KEY_ENTER:
        if (ev->mods) return 0;
        tui_textedit_insert(te, "\n", 1);
        return 1;
    case TUI_KEY_TAB:
        if (ev->mods) return 0;
        tui_textedit_insert(te, "\t", 1);
        return 1;
    case TUI_KEY_BACKSPACE: erase(te, prev_pos(te, te->cur), te->cur); break;
    case TUI_KEY_DELETE:    erase(te, te->cur, next_pos(te, te->cur)); break;
    case TUI_KEY_LEFT:      te->cur = prev_pos(te, te->cur); break;
    case TUI_KEY_RIGHT:     te->cur = next_pos(te, te->cur); break;
    case TUI_KEY_HOME:      te->cur = (ev->mods & C) ? 0 : line_start(te, te->cur); break;
    case TUI_KEY_END:       te->cur = (ev->mods & C) ? te->len : line_end(te, te->cur); break;
    case TUI_KEY_UP:        move_vertical(te, -1); keep_col = 1; break;
    case TUI_KEY_DOWN:      move_vertical(te, 1); keep_col = 1; break;
    case TUI_KEY_PGUP:      move_vertical(te, -page); keep_col = 1; break;
    case TUI_KEY_PGDN:      move_vertical(te, page); keep_col = 1; break;
    default:                return 0;
    }
    if (!keep_col) te->want_col = -1;
    return 1;
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

void tui_textedit_render(tui_textedit *te, tui_canvas c, tui_style st) {
    if (!te || !te->buf) return;
    tui_canvas_clear(c, st);
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    if (rows <= 0 || cols <= 0) return;
    te->rows = rows;

    int crow, ccol;
    tui_textedit_cursor(te, &crow, &ccol);
    if (crow < te->top) te->top = crow;
    if (crow >= te->top + rows) te->top = crow - rows + 1;
    if (ccol < te->left) te->left = ccol;
    if (ccol >= te->left + cols) te->left = ccol - cols + 1;

    /* Each visible line with its tabs expanded, drawn from column -left. */
    tui_buf line;
    tui_buf_init(&line);
    size_t p = start_of_line(te, te->top);
    for (int r = 0; r < rows; r++) {
        if (te->top + r >= tui_textedit_lines(te)) break;
        size_t e = line_end(te, p);
        tui_buf_reset(&line);
        int col = 0;
        while (p < e) {
            int k;
            int w = width_at(te, p, col, &k);
            if (te->buf[p] == '\t') {
                for (int i = 0; i < w; i++) tui_buf_puts(&line, " ");
            } else {
                tui_buf_append(&line, te->buf + p, (size_t)k);
            }
            col += w;
            p += (size_t)k;
        }
        tui_canvas_put(c, r, -te->left, line.data ? line.data : "", st);
        p = e + 1;
    }
    tui_buf_free(&line);
    tui_canvas_set_cursor(c, crow - te->top, ccol - te->left, 1);
}

int tui_textedit_summary(const char *text, int cols, char *out, size_t outsz) {
    if (!out || outsz == 0) return -1;
    out[0] = '\0';
    if (!text || !text[0] || cols <= 0) return 0;
    int lines = 1;
    size_t first = strlen(text);
    for (const char *p = text; *p; p++) {
        if (*p == '\n') {
            if (lines == 1) first = (size_t)(p - text);
            lines++;
        }
    }
    char tail[32] = "";
    if (lines > 1) snprintf(tail, sizeof(tail), " (%d lines)", lines);
    int tail_w = (int)strlen(tail);
    int room = cols - tail_w;
    if (room < 1) {                              /* only the count fits, or not even that */
        return tui_str_fit_ellipsis(tail + 1, cols, out, outsz);
    }
    char head[1024];
    if (first >= sizeof(head)) first = sizeof(head) - 1;
    memcpy(head, text, first);
    head[first] = '\0';
    int w = tui_str_fit_ellipsis(head, room, out, outsz);
    if (w < 0) return -1;
    size_t used = strlen(out);
    if (used + strlen(tail) + 1 > outsz) return -1;
    memcpy(out + used, tail, strlen(tail) + 1);
    return w + tail_w;
}
