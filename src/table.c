/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-13 */
/* The table: columns, a header and a selection the widget owns. */

#include "tuikit/table.h"
#include "tuikit/text.h"
#include "internal.h"

#include <stdlib.h>
#include <string.h>

#define CELL_TEXT  1024
#define CELL_SPANS 24
#define ROW_TEXT   256
#define GUTTER     2

struct tui_table_cell {
    char      text[CELL_TEXT];
    size_t    len;
    int       nspan;
    int       prefix_spans;   /* the first spans that are never cut */
    int       body_started;
    struct {
        uint16_t  off, end;
        tui_style st;
    } span[CELL_SPANS];
};

struct tui_table_row {
    tui_row_kind kind;
    tui_style    style;
    char         mark[5];
    tui_style    mark_style;
    char         text[ROW_TEXT];
};

/* ── The model's side: cells and rows ──────────────────────────────── */

/* The bytes of @p s that fit in @p cap bytes without splitting a UTF-8
 * sequence. */
static size_t utf8_prefix(const char *s, size_t cap) {
    size_t n = strlen(s);
    if (n <= cap) return n;
    n = cap;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    return n;
}

static void cell_append(tui_table_cell *c, const char *s, tui_style st, int prefix) {
    if (!s || !*s || c->nspan >= CELL_SPANS) return;
    size_t n = utf8_prefix(s, CELL_TEXT - 1 - c->len);
    if (n == 0) return;
    memcpy(c->text + c->len, s, n);
    c->span[c->nspan].off = (uint16_t)c->len;
    c->span[c->nspan].end = (uint16_t)(c->len + n);
    c->span[c->nspan].st = st;
    c->nspan++;
    c->len += n;
    c->text[c->len] = '\0';
    if (prefix && !c->body_started) c->prefix_spans = c->nspan;
    else c->body_started = 1;
}

void tui_table_cell_prefix(tui_table_cell *c, const char *utf8, tui_style st) {
    if (c) cell_append(c, utf8, st, 1);
}

void tui_table_cell_put(tui_table_cell *c, const char *utf8, tui_style st) {
    if (c) cell_append(c, utf8, st, 0);
}

void tui_table_row_kind(tui_table_row *r, tui_row_kind kind) {
    if (r) r->kind = kind;
}

void tui_table_row_style(tui_table_row *r, tui_style st) {
    if (r) r->style = st;
}

void tui_table_row_mark(tui_table_row *r, const char *glyph, tui_style st) {
    if (!r || !glyph || !*glyph) return;
    int k = tui_utf8_seq_len((unsigned char)glyph[0]);
    size_t n = strlen(glyph);
    if (k < 1 || (size_t)k > n) k = 1;
    memcpy(r->mark, glyph, (size_t)k);
    r->mark[k] = '\0';
    r->mark_style = st;
}

void tui_table_row_text(tui_table_row *r, const char *text) {
    if (!r) return;
    r->text[0] = '\0';
    if (!text) return;
    size_t n = utf8_prefix(text, ROW_TEXT - 1);
    memcpy(r->text, text, n);
    r->text[n] = '\0';
}

static void row_describe(const tui_table *t, int index, tui_table_row *r) {
    memset(r, 0, sizeof(*r));
    if (t->model.row) t->model.row(t->model.ctx, index, r);
}

static void cell_describe(const tui_table *t, int index, int col, tui_table_cell *c) {
    c->len = 0;
    c->nspan = 0;
    c->prefix_spans = 0;
    c->body_started = 0;
    c->text[0] = '\0';
    if (t->model.cell) t->model.cell(t->model.ctx, index, col, c);
}

/* ── State ─────────────────────────────────────────────────────────── */

void tui_table_set_colors(tui_table *t, int colors) {
    if (!t) return;
    t->colors = colors >= 256 ? 256 : colors >= 16 ? 16 : 8;
    t->selection = tui__selection_base(t->colors);
}

void tui_table_init(tui_table *t, const tui_table_col *cols, int ncols,
                    const tui_table_model *model) {
    if (!t) return;
    memset(t, 0, sizeof(*t));
    tui_listview_init(&t->lv);
    t->cols = cols;
    t->ncols = cols && ncols > 0 ? ncols : 0;
    if (model) t->model = *model;
    t->header = tui_style_attrs(TUI_ATTR_BOLD);
    t->message_style = tui_style_attrs(TUI_ATTR_DIM);
    tui_table_set_colors(t, tui__colors_from_env());
}

void tui_table_set_total(tui_table *t, int total) {
    if (!t) return;
    t->total = total < t->lv.count ? t->lv.count : total;
}

/* Whether row @p index can hold the selection. */
static int selectable(const tui_table *t, int index) {
    if (!t->model.row) return 1;
    tui_table_row r;
    row_describe(t, index, &r);
    return r.kind == TUI_ROW_DATA;
}

/* The nearest selectable row from @p from in direction @p dir (+1, -1),
 * or -1. */
static int scan(const tui_table *t, int from, int dir) {
    for (int i = from; i >= 0 && i < t->lv.count; i += dir)
        if (selectable(t, i)) return i;
    return -1;
}

/* Settles the selection on a data row: the nearest one in @p dir, else the
 * nearest the other way; -1 when there is none (all section rows). */
static int settle(const tui_table *t, int index, int dir) {
    int r = scan(t, index, dir);
    if (r < 0) r = scan(t, index, -dir);
    return r;
}

void tui_table_set_count(tui_table *t, int count) {
    if (!t) return;
    tui_listview_set_count(&t->lv, count);
    if (t->total < t->lv.count) t->total = t->lv.count;
    if (t->lv.count > 0) {
        int s = settle(t, t->lv.selected, 1);
        t->lv.selected = s;
        if (s >= 0) tui_listview_reveal_selected(&t->lv);
    }
}

void tui_table_select(tui_table *t, int index) {
    if (!t || t->lv.count <= 0) return;
    if (index < 0) index = 0;
    if (index >= t->lv.count) index = t->lv.count - 1;
    int s = settle(t, index, index >= t->lv.selected ? 1 : -1);
    if (s < 0) return;
    tui_listview_select(&t->lv, s);
}

int tui_table_selected(const tui_table *t) {
    return t ? t->lv.selected : -1;
}

void tui_table_set_message(tui_table *t, const char *message, tui_style st) {
    if (!t) return;
    t->message = message;
    t->message_style = st;
}

static int header_rows(const tui_table *t) {
    if (t->flags & TUI_TABLE_NO_HEADER) return 0;
    int titled = 0;
    for (int i = 0; i < t->ncols; i++)
        if (t->cols[i].title) titled = 1;
    if (!titled) return 0;
    return 1 + ((t->flags & TUI_TABLE_RULER) ? 1 : 0);
}

int tui_table_first_visible(const tui_table *t) {
    if (!t || t->lv.count <= 0 || t->message) return -1;
    return t->lv.scroll_top;
}

int tui_table_last_visible(const tui_table *t) {
    if (!t || t->lv.count <= 0 || t->lv.rows_visible <= 0 || t->message) return -1;
    int last = t->lv.scroll_top + t->lv.rows_visible - 1;
    return last < t->lv.count ? last : t->lv.count - 1;
}

int tui_table_handle(tui_table *t, const tui_event *ev) {
    if (!t || !ev) return 0;
    tui_key k = tui__nav_key(ev);
    int cur = t->lv.selected;
    int page = t->lv.rows_visible > 1 ? t->lv.rows_visible : 1;
    int target, dir;
    switch (k) {
    case TUI_KEY_UP:   target = cur - 1;    dir = -1; break;
    case TUI_KEY_DOWN: target = cur + 1;    dir = 1;  break;
    case TUI_KEY_PGUP: target = cur - page; dir = -1; break;
    case TUI_KEY_PGDN: target = cur + page; dir = 1;  break;
    case TUI_KEY_HOME: target = 0;          dir = 1;  break;
    case TUI_KEY_END:  target = t->lv.count - 1; dir = -1; break;
    default:           return 0;
    }
    if (t->lv.count <= 0) return 1;
    if (target < 0) target = 0;
    if (target >= t->lv.count) target = t->lv.count - 1;
    /* Section rows are passed over; when there is nothing selectable that
     * way the search turns back, and ends on the row it started from. */
    int s = settle(t, target, dir);
    if (s >= 0) tui_listview_select(&t->lv, s);
    return 1;
}

/* ── Layout ────────────────────────────────────────────────────────── */

int tui_table_widths(const tui_table *t, int cols, int *widths) {
    if (!t || !widths) return 0;
    int shown = 0, fixed = 0, weights = 0;
    for (int i = 0; i < t->ncols; i++) {
        const tui_table_col *c = &t->cols[i];
        int hide = c->hide_below > 0 && cols < c->hide_below;
        widths[i] = hide ? 0 : (c->width > 0 ? c->width : 1);
        if (hide) continue;
        shown++;
        fixed += widths[i];
        if (c->weight > 0) weights += c->weight;
    }
    int avail = cols - GUTTER - (shown > 1 ? shown - 1 : 0) - fixed;
    if (avail > 0 && weights > 0) {
        int given = 0, last = -1;
        for (int i = 0; i < t->ncols; i++) {
            if (!widths[i] || t->cols[i].weight <= 0) continue;
            int add = avail * t->cols[i].weight / weights;
            widths[i] += add;
            given += add;
            last = i;
        }
        widths[last] += avail - given;  /* the rounding goes to the last */
    }
    return shown;
}

/* ── Styles ────────────────────────────────────────────────────────── */

/* The style of a span: see tui__selection_compose(). */
static tui_style compose(const tui_table *t, tui_style row, tui_style span, int selected) {
    return tui__selection_compose(t->selection, row, span, selected);
}

/* The base of the whole selected row: the background and the attributes. */
static tui_style selected_base(const tui_table *t, tui_style row) {
    return compose(t, row, TUI_STYLE_DEFAULT, 1);
}

/* ── Cells on the screen ───────────────────────────────────────────── */

static int cell_width(const tui_table_cell *c, int from_span) {
    int w = 0;
    for (int i = from_span; i < c->nspan; i++)
        w += tui_strn_width(c->text + c->span[i].off, (size_t)(c->span[i].end - c->span[i].off));
    return w;
}

static void put_span(tui_canvas cv, int row, int col, const tui_table_cell *c, int i,
                     tui_style st) {
    char tmp[CELL_TEXT];
    size_t n = (size_t)(c->span[i].end - c->span[i].off);
    memcpy(tmp, c->text + c->span[i].off, n);
    tmp[n] = '\0';
    tui_canvas_put(cv, row, col, tmp, st);
}

/* Draws the spans from @p from at @p col of @p cv, each in its composed
 * style; @return the columns advanced. */
static int put_spans(const tui_table *t, tui_canvas cv, int row, int col,
                     const tui_table_cell *c, int from, int to, tui_style rowst,
                     int selected, tui_style *last) {
    int x = col;
    for (int i = from; i < to; i++) {
        tui_style st = compose(t, rowst, c->span[i].st, selected);
        put_span(cv, row, x, c, i, st);
        x += tui_strn_width(c->text + c->span[i].off, (size_t)(c->span[i].end - c->span[i].off));
        if (last) *last = st;
    }
    return x - col;
}

/* One cell, @p w wide, in the canvas @p cv (a sub-canvas of the cell). */
static void draw_cell(const tui_table *t, tui_canvas cv, const tui_table_cell *c, int w,
                      const tui_table_col *col, tui_style rowst, int selected) {
    int total = cell_width(c, 0);
    int pre = cell_width(c, 0) - cell_width(c, c->prefix_spans);
    tui_style last = compose(t, rowst, TUI_STYLE_DEFAULT, selected);
    if (total <= w) {
        int x = col->align == TUI_ALIGN_RIGHT ? w - total
              : col->align == TUI_ALIGN_CENTER ? (w - total) / 2 : 0;
        put_spans(t, cv, 0, x, c, 0, c->nspan, rowst, selected, NULL);
        return;
    }
    /* Too long: the prefix stays, the body is cut, and the cut shows. */
    if (pre >= w) {
        put_spans(t, cv, 0, 0, c, 0, c->prefix_spans, rowst, selected, NULL);
        return;
    }
    put_spans(t, cv, 0, 0, c, 0, c->prefix_spans, rowst, selected, &last);
    int room = w - pre - 1;  /* one cell for the ellipsis */
    tui_canvas body = tui_canvas_sub(cv, tui_rect_make(0, pre, 1, room > 0 ? room : 0));
    int used = 0;
    if (col->flags & TUI_COL_CUT_START) {
        /* The tail: draw the body shifted left so that its end is at the
         * right edge of the room, after the ellipsis. */
        int bodyw = total - pre;
        tui_canvas tail = tui_canvas_sub(cv, tui_rect_make(0, pre + 1, 1, room));
        put_spans(t, tail, 0, -(bodyw - room), c, c->prefix_spans, c->nspan, rowst,
                  selected, &last);
        tui_canvas_put(cv, 0, pre, "\xE2\x80\xA6", last);
        return;
    }
    for (int i = c->prefix_spans; i < c->nspan && used < room; i++) {
        tui_style st = compose(t, rowst, c->span[i].st, selected);
        const char *s = c->text + c->span[i].off;
        size_t n = (size_t)(c->span[i].end - c->span[i].off);
        int fit = 0;
        size_t bytes = tui_str_fit(s, room - used, &fit);
        if (bytes > n) bytes = n;
        char tmp[CELL_TEXT];
        memcpy(tmp, s, bytes);
        tmp[bytes] = '\0';
        tui_canvas_put(body, 0, used, tmp, st);
        used += fit;
        last = st;
        if (bytes < n) break;
    }
    tui_canvas_put(cv, 0, pre + used, "\xE2\x80\xA6", last);
}

static void draw_gutter(const tui_table *t, tui_canvas row, tui_style rowst, int selected,
                        const tui_table_row *r) {
    if (selected) {
        tui_style st = compose(t, rowst, tui_style_attrs(TUI_ATTR_BOLD), 1);
        tui_canvas_put(row, 0, 0, ">", st);
    }
    if (r->mark[0]) {
        tui_style st = compose(t, rowst, r->mark_style, selected);
        tui_canvas_put(row, 0, 1, r->mark, st);
    }
}

static void draw_section(const tui_table *t, tui_canvas row, const tui_table_row *r) {
    tui_style st = compose(t, r->style, tui_style_attrs(TUI_ATTR_DIM), 0);
    int w = tui_canvas_cols(row);
    int x = GUTTER;
    if (r->text[0]) {
        x += tui_canvas_put(row, 0, x, r->text, st);
        x += 1;
    }
    if (x < w) tui_canvas_hline(row, 0, x, w - x, st);
}

static tui_canvas row_canvas(tui_canvas c, int r) {
    return tui_canvas_sub(c, tui_rect_make(r, 0, 1, tui_canvas_cols(c)));
}

static void draw_row(const tui_table *t, tui_canvas row, int index, int selected,
                     tui_style blank) {
    tui_table_row r;
    row_describe(t, index, &r);
    int sel = selected && r.kind == TUI_ROW_DATA;
    tui_style base = compose(t, r.style, TUI_STYLE_DEFAULT, 0);
    tui_canvas_clear(row, sel ? selected_base(t, r.style)
                              : (r.style.bg != TUI_COLOR_DEFAULT ? base : blank));
    if (r.kind == TUI_ROW_SECTION) {
        draw_section(t, row, &r);
        return;
    }
    draw_gutter(t, row, r.style, sel, &r);
    int widths[64];
    int n = t->ncols < 64 ? t->ncols : 64;
    tui_table tt = *t;
    tt.ncols = n;
    tui_table_widths(&tt, tui_canvas_cols(row), widths);
    tui_table_cell cell;
    int x = GUTTER;
    for (int i = 0; i < n && x < tui_canvas_cols(row); i++) {
        if (widths[i] == 0) continue;
        cell_describe(t, index, i, &cell);
        tui_canvas cv = tui_canvas_sub(row, tui_rect_make(0, x, 1, widths[i]));
        draw_cell(t, cv, &cell, widths[i], &t->cols[i], r.style, sel);
        x += widths[i] + 1;
    }
}

void tui_table_render_header(tui_table *t, tui_canvas c) {
    if (!t || header_rows(t) == 0) return;
    int widths[64];
    int n = t->ncols < 64 ? t->ncols : 64;
    tui_table tt = *t;
    tt.ncols = n;
    tui_table_widths(&tt, tui_canvas_cols(c), widths);
    tui_canvas hdr = row_canvas(c, 0);
    tui_canvas_clear(hdr, t->header);
    int x = GUTTER;
    for (int i = 0; i < n; i++) {
        if (widths[i] == 0) continue;
        const tui_table_col *col = &t->cols[i];
        tui_table_cell cell;
        cell.len = 0;
        cell.nspan = 0;
        cell.prefix_spans = 0;
        cell.body_started = 0;
        cell.text[0] = '\0';
        if (col->title) cell_append(&cell, col->title, TUI_STYLE_DEFAULT, 0);
        int mark = col->sort != 0 && widths[i] >= 2;
        int tw = widths[i] - (mark ? 2 : 0);
        tui_canvas cv = tui_canvas_sub(hdr, tui_rect_make(0, x, 1, tw));
        draw_cell(t, cv, &cell, tw, col, t->header, 0);
        if (mark)
            tui_canvas_put(hdr, 0, x + widths[i] - 1,
                           col->sort > 0 ? "\xE2\x96\xB2" : "\xE2\x96\xBC", t->header);
        x += widths[i] + 1;
    }
    if (t->flags & TUI_TABLE_RULER) {
        tui_canvas rule = row_canvas(c, 1);
        tui_canvas_clear(rule, TUI_STYLE_DEFAULT);
        x = GUTTER;
        for (int i = 0; i < n; i++) {
            if (widths[i] == 0) continue;
            tui_canvas seg = tui_canvas_sub(rule, tui_rect_make(0, x, 1, widths[i]));
            if (!tui_rect_is_empty(seg.clip))
                tui_screen_fill(seg.screen, seg.clip.row, seg.clip.col, seg.clip.cols,
                                0x2550, TUI_STYLE_DEFAULT);
            x += widths[i] + 1;
        }
    }
}

int tui_table_render_row(tui_table *t, tui_canvas c, int index) {
    if (!t || t->message) return 0;
    if (index < t->lv.scroll_top || index >= t->lv.count ||
        index >= t->lv.scroll_top + t->lv.rows_visible)
        return 0;
    int line = header_rows(t) + (index - t->lv.scroll_top);
    if (line >= tui_canvas_rows(c)) return 0;
    draw_row(t, row_canvas(c, line), index, index == t->lv.selected, TUI_STYLE_DEFAULT);
    return 1;
}

void tui_table_render(tui_table *t, tui_canvas c, tui_style blank) {
    if (!t) return;
    int rows = tui_canvas_rows(c);
    if (t->message) {
        tui_canvas_clear(c, blank);
        tui_canvas_put_aligned(row_canvas(c, 0), 0, t->message, TUI_ALIGN_LEFT, t->message_style);
        return;
    }
    int hdr = header_rows(t);
    tui_listview_set_viewport(&t->lv, rows > hdr ? rows - hdr : 0);
    tui_canvas_clear(c, blank);
    tui_table_render_header(t, c);
    for (int i = 0; i < t->lv.rows_visible; i++) {
        int index = t->lv.scroll_top + i;
        if (index >= t->lv.count) break;
        draw_row(t, row_canvas(c, hdr + i), index, index == t->lv.selected, blank);
    }
}

/* ── Plain text ────────────────────────────────────────────────────── */

typedef struct { char *p; size_t cap, len; int cut; } plain_out;

static void plain_add(plain_out *o, const char *s, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (o->len + 1 >= o->cap) {
            o->cut = 1;
            return;
        }
        o->p[o->len++] = s[i];
    }
}

static void plain_pad(plain_out *o, int n) {
    for (int i = 0; i < n; i++) plain_add(o, " ", 1);
}

/* The suffix of @p s that fits in @p cols columns. */
static const char *tail_fit(const char *s, int cols) {
    int w = tui_str_width(s);
    const char *p = s;
    while (*p && w > cols) {
        uint32_t cp;
        int k = tui_utf8_decode(p, strlen(p), &cp);
        w -= tui_cp_width(tui_cp_sanitize(cp));
        p += k;
    }
    return p;
}

static void plain_cell(plain_out *o, const tui_table_cell *c, int w,
                       const tui_table_col *col, int last) {
    const char *all = c->text;
    int total = tui_str_width(all);
    if (w <= 0) {
        plain_add(o, all, c->len);
        return;
    }
    if (total <= w) {
        int left = col->align == TUI_ALIGN_RIGHT ? w - total
                 : col->align == TUI_ALIGN_CENTER ? (w - total) / 2 : 0;
        int right = w - total - left;
        plain_pad(o, left);
        plain_add(o, all, c->len);
        if (!last) plain_pad(o, right);
        return;
    }
    size_t plen = c->prefix_spans > 0 ? c->span[c->prefix_spans - 1].end : 0;
    int pre = tui_strn_width(all, plen);
    if (pre >= w) {
        int used = 0;
        size_t n = tui_str_fit(all, w, &used);
        plain_add(o, all, n);
        if (!last) plain_pad(o, w - used);
        return;
    }
    plain_add(o, all, plen);
    int room = w - pre - 1;
    if (col->flags & TUI_COL_CUT_START) {
        const char *tail = tail_fit(all + plen, room);
        plain_add(o, "\xE2\x80\xA6", 3);
        plain_add(o, tail, strlen(tail));
        if (!last) plain_pad(o, room - tui_str_width(tail));
    } else {
        int used = 0;
        size_t n = tui_str_fit(all + plen, room, &used);
        plain_add(o, all + plen, n);
        plain_add(o, "\xE2\x80\xA6", 3);
        if (!last) plain_pad(o, room - used);
    }
}

static int plain_finish(plain_out *o) {
    o->p[o->len] = '\0';
    return o->cut ? -1 : (int)o->len;
}

static int format_cells(const tui_table *t, int cols, int index, const tui_table_row *r,
                        char *out, size_t outsz) {
    plain_out o = { out, outsz, 0, 0 };
    if (outsz == 0) return -1;
    if (r && r->kind == TUI_ROW_SECTION) {
        plain_add(&o, r->text, strlen(r->text));
        return plain_finish(&o);
    }
    int widths[64];
    int n = t->ncols < 64 ? t->ncols : 64;
    tui_table tt = *t;
    tt.ncols = n;
    if (cols > 0) tui_table_widths(&tt, cols, widths);
    else for (int i = 0; i < n; i++) widths[i] = t->cols[i].width;
    int lastcol = -1;
    for (int i = 0; i < n; i++)
        if (cols <= 0 || widths[i] > 0) lastcol = i;
    int first = 1;
    tui_table_cell cell;
    for (int i = 0; i < n; i++) {
        if (cols > 0 && widths[i] == 0) continue;
        if (!first) plain_add(&o, " ", 1);
        first = 0;
        if (index >= 0) {
            cell_describe(t, index, i, &cell);
        } else {
            cell.len = 0;
            cell.nspan = 0;
            cell.prefix_spans = 0;
            cell.body_started = 0;
            cell.text[0] = '\0';
            if (t->cols[i].title) cell_append(&cell, t->cols[i].title, TUI_STYLE_DEFAULT, 0);
        }
        plain_cell(&o, &cell, cols > 0 ? widths[i] : 0, &t->cols[i], i == lastcol);
    }
    return plain_finish(&o);
}

int tui_table_format_row(const tui_table *t, int cols, int index, char *out, size_t outsz) {
    if (!t || !out || outsz == 0 || index < 0 || index >= t->lv.count) return -1;
    tui_table_row r;
    row_describe(t, index, &r);
    return format_cells(t, cols, index, &r, out, outsz);
}

int tui_table_format_header(const tui_table *t, int cols, char *out, size_t outsz) {
    if (!t || !out || outsz == 0) return -1;
    return format_cells(t, cols, -1, NULL, out, outsz);
}
