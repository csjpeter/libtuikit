/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-13 */
/* Unit tests of tui_table: layout, cuts, header, selection, rows and plain text. */

/*
 * What the table draws is rendered, fed to libptytest's emulator and
 * compared cell by cell: the characters, and the colours and attributes of
 * the selection (policies/tui-interface-model.md).
 */

#include "test_helpers.h"
#include "tuikit/table.h"

#include <stdlib.h>

int g_tests_run = 0;
int g_tests_failed = 0;

typedef struct {
    tui_screen *scr;
    tui_buf     buf;
    PtySession *s;
} Fx;

static int fx_open(Fx *f, int rows, int cols) {
    f->scr = tui_screen_new(rows, cols);
    tui_buf_init(&f->buf);
    f->s = pty_open(cols, rows);
    return (f->scr && f->s) ? 0 : -1;
}

static void fx_show(Fx *f) {
    tui_buf_reset(&f->buf);
    tui_screen_render(f->scr, &f->buf);
    pty_feed(f->s, f->buf.data, f->buf.len);
}

static void fx_close(Fx *f) {
    tui_screen_free(f->scr);
    tui_buf_free(&f->buf);
    if (f->s) pty_close(f->s);
}

/* ── The program's data ─────────────────────────────────────────────── */

typedef struct {
    const char *key, *title, *status;
    int         n;
    int         section;   /* a section row with this text in title */
    const char *mark;
    tui_style   rowst;
} Item;

static const Item g_items[] = {
    { "A-1", "Short",                         "NEW",  5,    0, NULL, { 0, 0, 0 } },
    { "A-2", "A title that is much too long to fit", "DONE", 1234, 0, NULL, { 0, 0, 0 } },
    { "",    "Group",                         "",     0,    1, NULL, { 0, 0, 0 } },
    { "A-4", "tail",                          "-",    7,    0, "*",  { 0, 0, 0 } },
};
#define NITEMS 4

static void model_row(void *ctx, int index, tui_table_row *r) {
    const Item *it = ctx;
    it += index;
    if (it->section) {
        tui_table_row_kind(r, TUI_ROW_SECTION);
        tui_table_row_text(r, it->title);
    }
    if (it->mark) tui_table_row_mark(r, it->mark, TUI_STYLE_ERROR);
    if (it->rowst.fg || it->rowst.bg || it->rowst.attrs) tui_table_row_style(r, it->rowst);
}

static void model_cell(void *ctx, int index, int col, tui_table_cell *c) {
    const Item *it = ctx;
    it += index;
    char num[16];
    switch (col) {
    case 0: tui_table_cell_put(c, it->key, TUI_STYLE_DEFAULT); break;
    case 1: tui_table_cell_put(c, it->title, TUI_STYLE_DEFAULT); break;
    case 2:
        snprintf(num, sizeof(num), "%d", it->n);
        tui_table_cell_put(c, num, TUI_STYLE_DEFAULT);
        break;
    default:
        tui_table_cell_put(c, it->status, tui_style_make(TUI_GREEN, TUI_COLOR_DEFAULT, 0));
        break;
    }
}

static const tui_table_col g_cols[] = {
    { "KEY",   6, 0, TUI_ALIGN_LEFT,  0,  0, 0 },
    { "TITLE", 8, 1, TUI_ALIGN_LEFT,  0,  0, 0 },
    { "N",     4, 0, TUI_ALIGN_RIGHT, 0,  0, 0 },
    { "ST",    5, 0, TUI_ALIGN_LEFT,  30, 0, 0 },
};

static void make_table(tui_table *t, int colors) {
    tui_table_model m = { model_row, model_cell, (void *)g_items };
    tui_table_init(t, g_cols, 4, &m);
    tui_table_set_colors(t, colors);
    tui_table_set_count(t, NITEMS);
}

/* The expected text of a row: the cells laid out as the 40-column table
 * does (gutter 2, KEY 6, TITLE 20, N 4, ST 5, one blank between). */
static void want(char *out, size_t sz, const char *gut, const char *k, const char *ti,
                 const char *n, const char *st) {
    snprintf(out, sz, "%-2s%-6s %-20s %4s %s", gut, k, ti, n, st);
    size_t l = strlen(out);
    while (l > 0 && out[l - 1] == ' ') out[--l] = '\0';
}

#define LEGEND_SEL_BG PTY_COLOR_IDX(17)

/* A row of the emulated screen as text. */
static const char *rowtext(PtySession *s, int row) {
    static char buf[512];
    return pty_row_text(s, row, buf, sizeof(buf));
}

/* ── Layout ─────────────────────────────────────────────────────────── */

static void test_table_widths(void) {
    tui_table t;
    make_table(&t, 256);
    int w[4];
    ASSERT_EQ_INT(tui_table_widths(&t, 40, w), 4, "all four columns");
    ASSERT_EQ_INT(w[0], 6, "fixed");
    ASSERT_EQ_INT(w[1], 20, "the flexible one takes what is left");
    ASSERT_EQ_INT(w[2], 4, "fixed right");
    ASSERT_EQ_INT(w[3], 5, "fixed last");
    ASSERT_EQ_INT(tui_table_widths(&t, 30, w), 4, "hide_below: 30 still shows it");
    ASSERT_EQ_INT(w[1], 10, "30 columns");
    ASSERT_EQ_INT(tui_table_widths(&t, 29, w), 3, "a column hides below its width");
    ASSERT_EQ_INT(w[3], 0, "hidden: width 0");
    ASSERT_EQ_INT(w[1], 15, "the others take its place");
    tui_table_widths(&t, 25, w);
    ASSERT_EQ_INT(w[1], 11, "25 columns");
    tui_table_widths(&t, 10, w);
    ASSERT_EQ_INT(w[1], 8, "narrow: the minimum, the canvas clips");
    ASSERT_EQ_INT(tui_table_widths(NULL, 40, w), 0, "NULL table");
    ASSERT_EQ_INT(tui_table_widths(&t, 40, NULL), 0, "NULL widths");

    /* Two flexible columns share by weight; the rounding goes to the last. */
    static const tui_table_col two[] = {
        { "A", 4, 3, TUI_ALIGN_LEFT, 0, 0, 0 },
        { "B", 4, 2, TUI_ALIGN_LEFT, 0, 0, 0 },
    };
    tui_table u;
    tui_table_init(&u, two, 2, NULL);
    int v[2];
    ASSERT_EQ_INT(tui_table_widths(&u, 40, v), 2, "two columns");
    ASSERT_EQ_INT(v[0], 21, "weight 3: 4 + 17");
    ASSERT_EQ_INT(v[1], 16, "weight 2: 4 + 11 + the rounding");
    ASSERT_EQ_INT(2 + v[0] + 1 + v[1], 40, "the whole width");
    /* A zero width is one cell. */
    static const tui_table_col zero[] = { { "Z", 0, 0, TUI_ALIGN_LEFT, 0, 0, 0 } };
    tui_table z;
    tui_table_init(&z, zero, 1, NULL);
    int zw[1];
    tui_table_widths(&z, 40, zw);
    ASSERT_EQ_INT(zw[0], 1, "width of at least one cell");
}

/* ── The picture ────────────────────────────────────────────────────── */

static void test_table_render_256(void) {
    Fx f;
    ASSERT(fx_open(&f, 6, 40) == 0, "fixtures");
    tui_table t;
    make_table(&t, 256);
    tui_table_select(&t, 1);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(t.lv.rows_visible, 5, "the viewport is the canvas less the header");
    fx_show(&f);
    char r0[64], r1[64], r2[64], r4[64], sect[160];
    want(r0, sizeof(r0), "", "KEY", "TITLE", "N", "ST");
    want(r1, sizeof(r1), "", "A-1", "Short", "5", "NEW");
    /* The title is 20 cells: 19 characters and the ellipsis. */
    snprintf(r2, sizeof(r2), "> A-2    A title that is muc\xE2\x80\xA6 1234 DONE");
    snprintf(sect, sizeof(sect), "  Group ");
    for (int i = 8; i < 40; i++) strcat(sect, "\xE2\x94\x80");
    want(r4, sizeof(r4), " *", "A-4", "tail", "7", "-");
    const char *const expect[] = { r0, r1, r2, sect, r4, "", NULL };
    ASSERT_SCREEN_EQ(f.s, expect);

    /* Header: bold, no background.  Selected row: dark blue 17 all along,
     * white text, ">" in bold; the others untouched. */
    ASSERT_EQ_INT(pty_cell_attr(f.s, 0, 2) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "header bold");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 0, 2), 0, "header: no background");
    ASSERT_EQ_STR(pty_cell_text(f.s, 2, 0), ">", "the cursor glyph");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 2, 0) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "'>' bold");
    for (int col = 0; col < 40; col++) {
        char what[48];
        snprintf(what, sizeof(what), "selected row, column %d: background", col);
        ASSERT_EQ_INT(pty_cell_bg(f.s, 2, col), LEGEND_SEL_BG, what);
    }
    ASSERT_EQ_INT(pty_cell_fg(f.s, 2, 2), 97, "text without a colour: bright white");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 2, 35), 92, "green kept as its bright variant");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 2, 35) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "and bold");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 1, 2), 0, "an unselected row has no background");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 1, 35), 32, "its green is plain green");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 1, 2), 0, "and its text the default colour");
    /* The mark is shown beside the cursor and keeps its own colour. */
    ASSERT_EQ_STR(pty_cell_text(f.s, 4, 1), "*", "mark");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 4, 1), 31, "mark in the error colour");

    /* The same row with the cursor on it: ">" and the mark side by side. */
    tui_table_select(&t, 3);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_EQ_STR(pty_cell_text(f.s, 4, 0), ">", "cursor on the marked row");
    ASSERT_EQ_STR(pty_cell_text(f.s, 4, 1), "*", "the mark survives the selection");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 4, 1), 91, "red as its bright variant");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 4, 1), LEGEND_SEL_BG, "on the selection");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 2, 5), 0, "the row left has none");
    fx_close(&f);
}

static void test_table_render_colour_depths(void) {
    Fx f;
    ASSERT(fx_open(&f, 6, 40) == 0, "fixtures");
    tui_table t;
    make_table(&t, 16);
    ASSERT_EQ_INT(t.colors, 16, "16");
    tui_table_select(&t, 1);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_EQ_INT(pty_cell_bg(f.s, 2, 10), 44, "16 colours: blue");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 2, 35), 92, "bright green");

    tui_table_set_colors(&t, 8);
    ASSERT_EQ_INT(t.colors, 8, "8");
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_EQ_INT(pty_cell_bg(f.s, 2, 10), 0, "8 colours: no background");
    ASSERT_EQ_STR(pty_cell_text(f.s, 2, 0), ">", "the cursor glyph carries it");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 2, 10) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "and bold");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 2, 35), 32, "colours as they are");

    fx_close(&f);
}

typedef struct { tui_style st[4]; } Styles;

static void styles_cell(void *ctx, int index, int col, tui_table_cell *c) {
    (void)index;
    const Styles *s = ctx;
    static const char *const txt[] = { "k", "b", "i", "r" };
    tui_table_cell_put(c, txt[col], s->st[col]);
}

static void test_table_selection_colour_mapping(void) {
    Fx f;
    ASSERT(fx_open(&f, 3, 20) == 0, "fixtures");
    Styles s = { {
        tui_style_make(TUI_BLACK, TUI_COLOR_DEFAULT, 0),
        tui_style_make(TUI_BRIGHT_CYAN, TUI_COLOR_DEFAULT, 0),
        tui_style_make(TUI_COLOR_256(208), TUI_COLOR_DEFAULT, 0),
        tui_style_make(TUI_WHITE, TUI_RED, 0),
    } };
    static const tui_table_col cols[] = {
        { NULL, 2, 0, TUI_ALIGN_LEFT, 0, 0, 0 }, { NULL, 2, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
        { NULL, 2, 0, TUI_ALIGN_LEFT, 0, 0, 0 }, { NULL, 2, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
    };
    tui_table_model m = { NULL, styles_cell, &s };
    tui_table t;
    tui_table_init(&t, cols, 4, &m);
    tui_table_set_colors(&t, 256);
    tui_table_set_count(&t, 1);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_EQ_INT(t.lv.rows_visible, 3, "no titles: no header row");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 2), 37, "black becomes white");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 5), 96, "a bright colour stays");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 8), PTY_COLOR_IDX(208), "a 256 colour stays");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 0, 11), 41, "a cell's own background stays");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 11), 37, "with its text colour");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 0, 2), LEGEND_SEL_BG, "the others on the selection");
    fx_close(&f);
}

/* ── Cuts, alignment, prefix, header markers ────────────────────────── */

typedef struct { const char *prefix, *body; } PB;

static void pb_cell(void *ctx, int index, int col, tui_table_cell *c) {
    const PB *p = ctx;
    (void)index; (void)col;
    if (p->prefix) tui_table_cell_prefix(c, p->prefix, TUI_STYLE_DEFAULT);
    tui_table_cell_put(c, p->body, TUI_STYLE_DEFAULT);
}

static void one_cell(const char *prefix, const char *body, int width, tui_align al,
                     unsigned flags, const char *title, int sort, char *rowout, size_t sz,
                     char *plain, size_t plainsz) {
    Fx f;
    tui_table_col col = { title, width, 0, al, 0, sort, flags };
    PB pb = { prefix, body };
    tui_table_model m = { NULL, pb_cell, &pb };
    tui_table t;
    tui_table_init(&t, &col, 1, &m);
    tui_table_set_colors(&t, 8);
    tui_table_set_count(&t, 1);
    rowout[0] = '\0';
    if (fx_open(&f, 3, width + 2) != 0) return;
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    snprintf(rowout, sz, "%s", rowtext(f.s, 0));
    if (plain) tui_table_format_row(&t, width + 2, 0, plain, plainsz);
    fx_close(&f);
}

static void test_table_cuts_and_alignment(void) {
    char row[128], plain[128];
    one_cell(NULL, "abcdefghijklmnop", 8, TUI_ALIGN_LEFT, 0, NULL, 0, row, sizeof(row),
             plain, sizeof(plain));
    ASSERT_EQ_STR(row, "> abcdefg\xE2\x80\xA6", "cut at the end");
    ASSERT_EQ_STR(plain, "abcdefg\xE2\x80\xA6", "the same in plain text");

    one_cell(NULL, "someone@example.org", 10, TUI_ALIGN_LEFT, TUI_COL_CUT_START, NULL, 0, row,
             sizeof(row), plain, sizeof(plain));
    ASSERT_EQ_STR(row, "> \xE2\x80\xA6" "ample.org", "TUI_COL_CUT_START keeps the tail");
    ASSERT_EQ_STR(plain, "\xE2\x80\xA6" "ample.org", "plain, the tail");

    /* A prefix is never cut; the body is. */
    one_cell("\xE2\x94\x82 \xE2\x94\x94 ", "abcdefghij", 10, TUI_ALIGN_LEFT, 0, NULL, 0, row,
             sizeof(row), plain, sizeof(plain));
    ASSERT_EQ_STR(row, "> \xE2\x94\x82 \xE2\x94\x94 abcde\xE2\x80\xA6", "the prefix stays");
    ASSERT_EQ_STR(plain, "\xE2\x94\x82 \xE2\x94\x94 abcde\xE2\x80\xA6", "plain: the prefix stays");
    one_cell("\xE2\x94\x82 \xE2\x94\x94 ", "abcdefghij", 3, TUI_ALIGN_LEFT, 0, NULL, 0, row,
             sizeof(row), plain, sizeof(plain));
    ASSERT_EQ_STR(row, "> \xE2\x94\x82 \xE2\x94\x94", "a prefix wider than the cell is clipped");
    ASSERT_EQ_STR(plain, "\xE2\x94\x82 \xE2\x94\x94", "plain: clipped");
    one_cell("\xE2\x94\x82 ", "abc", 8, TUI_ALIGN_LEFT, TUI_COL_CUT_START, NULL, 0, row,
             sizeof(row), plain, sizeof(plain));
    ASSERT_EQ_STR(row, "> \xE2\x94\x82 abc", "a short cell with a prefix is whole");
    one_cell("\xE2\x94\x82 ", "abcdefghij", 8, TUI_ALIGN_LEFT, TUI_COL_CUT_START, NULL, 0, row,
             sizeof(row), plain, sizeof(plain));
    ASSERT_EQ_STR(row, "> \xE2\x94\x82 \xE2\x80\xA6" "fghij", "prefix, then the tail");
    ASSERT_EQ_STR(plain, "\xE2\x94\x82 \xE2\x80\xA6" "fghij", "plain: prefix, then the tail");

    /* Wide characters are cut whole. */
    one_cell(NULL, "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E\xE6\x97\xA5\xE6\x9C\xAC", 6,
             TUI_ALIGN_LEFT, 0, NULL, 0, row, sizeof(row), plain, sizeof(plain));
    ASSERT_EQ_STR(row, "> \xE6\x97\xA5\xE6\x9C\xAC\xE2\x80\xA6", "a wide character is not split");
    ASSERT_EQ_STR(plain, "\xE6\x97\xA5\xE6\x9C\xAC\xE2\x80\xA6", "plain: the last column is not padded");

    one_cell(NULL, "12", 6, TUI_ALIGN_RIGHT, 0, NULL, 0, row, sizeof(row), plain, sizeof(plain));
    ASSERT_EQ_STR(row, ">     12", "right aligned");
    ASSERT_EQ_STR(plain, "    12", "plain, right aligned");
    one_cell(NULL, "ab", 6, TUI_ALIGN_CENTER, 0, NULL, 0, row, sizeof(row), plain, sizeof(plain));
    ASSERT_EQ_STR(row, ">   ab", "centred");
    ASSERT_EQ_STR(plain, "  ab", "plain, centred; the last column has no padding");

    /* The header: a sort marker at the column's right edge, the title cut
     * before it. */
    one_cell(NULL, "x", 8, TUI_ALIGN_LEFT, 0, "UPDATED", 1, row, sizeof(row), NULL, 0);
    ASSERT_EQ_STR(row, "  UPDAT\xE2\x80\xA6 \xE2\x96\xB2", "ascending marker, title cut for it");
    one_cell(NULL, "x", 8, TUI_ALIGN_RIGHT, 0, "N", -1, row, sizeof(row), NULL, 0);
    ASSERT_EQ_STR(row, "       N \xE2\x96\xBC", "descending marker, right-aligned title beside it");
}

static void test_table_header_and_ruler(void) {
    Fx f;
    ASSERT(fx_open(&f, 6, 40) == 0, "fixtures");
    tui_table t;
    make_table(&t, 256);
    t.flags = TUI_TABLE_RULER;
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(t.lv.rows_visible, 4, "header and ruler take two rows");
    fx_show(&f);
    ASSERT_EQ_STR(pty_cell_text(f.s, 1, 2), "\xE2\x95\x90", "ruler under the first column");
    ASSERT_EQ_STR(pty_cell_text(f.s, 1, 7), "\xE2\x95\x90", "...to its end");
    ASSERT_EQ_STR(pty_cell_text(f.s, 1, 8), " ", "a blank between the columns");
    ASSERT_EQ_STR(pty_cell_text(f.s, 1, 39), "\xE2\x95\x90", "the last column");
    ASSERT_EQ_STR(pty_cell_text(f.s, 2, 2), "A", "rows follow the ruler");

    t.flags = TUI_TABLE_NO_HEADER;
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(t.lv.rows_visible, 6, "no header");
    fx_show(&f);
    ASSERT_EQ_STR(pty_cell_text(f.s, 0, 2), "A", "the first row on top");
    tui_table_render_header(&t, tui_canvas_of(f.scr));
    tui_table_render_header(NULL, tui_canvas_of(f.scr));
    fx_close(&f);
}

/* ── Rows ───────────────────────────────────────────────────────────── */

static void test_table_row_style_and_sections(void) {
    Fx f;
    ASSERT(fx_open(&f, 6, 40) == 0, "fixtures");
    static Item items[] = {
        { "R-1", "red row", "OK", 1, 0, NULL, { 0, 0, 0 } },
        { "",    "",        "",   0, 1, NULL, { 0, 0, 0 } },
        { "R-3", "dim row", "OK", 3, 0, NULL, { 0, 0, 0 } },
        { "",    "Tail",    "",   0, 1, NULL, { 0, 0, 0 } },
    };
    items[0].rowst = tui_style_make(TUI_RED, TUI_COLOR_DEFAULT, TUI_ATTR_BOLD);
    items[2].rowst = tui_style_make(TUI_COLOR_DEFAULT, TUI_COLOR_256(236), TUI_ATTR_DIM);
    tui_table_model m = { model_row, model_cell, items };
    tui_table t;
    tui_table_init(&t, g_cols, 4, &m);
    tui_table_set_colors(&t, 256);
    tui_table_set_count(&t, 4);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    /* Row 0 is the selected one: its row colour is kept, as the bright
     * variant, on the selection. */
    ASSERT_EQ_INT(pty_cell_fg(f.s, 1, 3), 91, "selected: red as bright red");
    ASSERT_EQ_INT(pty_cell_bg(f.s, 1, 3), LEGEND_SEL_BG, "selected background");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 1, 3) & PTY_ATTR_BOLD, PTY_ATTR_BOLD, "bold");
    /* An empty section is a rule alone, from the gutter on. */
    ASSERT_EQ_STR(pty_cell_text(f.s, 2, 2), "\xE2\x94\x80", "rule alone");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 2, 2) & PTY_ATTR_DIM, PTY_ATTR_DIM, "sections are dim");
    /* A row's own background reaches the whole row. */
    ASSERT_EQ_INT(pty_cell_bg(f.s, 3, 39), PTY_COLOR_IDX(236), "row background");
    ASSERT_EQ_INT(pty_cell_attr(f.s, 3, 5) & PTY_ATTR_DIM, PTY_ATTR_DIM, "row dim");
    /* Selected, a row's own background gives way to the selection's, and
     * the row it left shows its own colour. */
    tui_table_select(&t, 2);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_EQ_INT(pty_cell_bg(f.s, 3, 39), LEGEND_SEL_BG, "selected: the selection's background");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 1, 3), 31, "unselected: plain red");
    fx_close(&f);
}

static void key(tui_event *ev, tui_key k) {
    memset(ev, 0, sizeof(*ev));
    ev->type = TUI_EV_KEY;
    ev->key = k;
}

static void test_table_navigation_skips_sections(void) {
    static const Item items[] = {
        { "",  "head", "", 0, 1, NULL, { 0, 0, 0 } },   /* 0 section */
        { "a", "a", "", 1, 0, NULL, { 0, 0, 0 } },      /* 1 */
        { "b", "b", "", 2, 0, NULL, { 0, 0, 0 } },      /* 2 */
        { "",  "mid", "", 0, 1, NULL, { 0, 0, 0 } },    /* 3 section */
        { "",  "mid2", "", 0, 1, NULL, { 0, 0, 0 } },   /* 4 section */
        { "c", "c", "", 3, 0, NULL, { 0, 0, 0 } },      /* 5 */
        { "",  "end", "", 0, 1, NULL, { 0, 0, 0 } },    /* 6 section */
    };
    tui_table_model m = { model_row, model_cell, (void *)items };
    tui_table t;
    tui_table_init(&t, g_cols, 4, &m);
    tui_table_set_count(&t, 7);
    ASSERT_EQ_INT(tui_table_selected(&t), 1, "the first data row is selected");
    tui_event ev;
    key(&ev, TUI_KEY_DOWN);
    ASSERT_EQ_INT(tui_table_handle(&t, &ev), 1, "handled");
    ASSERT_EQ_INT(tui_table_selected(&t), 2, "down");
    tui_table_handle(&t, &ev);
    ASSERT_EQ_INT(tui_table_selected(&t), 5, "down passes the sections");
    tui_table_handle(&t, &ev);
    ASSERT_EQ_INT(tui_table_selected(&t), 5, "nothing selectable below: stays");
    key(&ev, TUI_KEY_UP);
    tui_table_handle(&t, &ev);
    ASSERT_EQ_INT(tui_table_selected(&t), 2, "up passes the sections");
    tui_table_handle(&t, &ev);
    tui_table_handle(&t, &ev);
    ASSERT_EQ_INT(tui_table_selected(&t), 1, "up stops at the first data row");
    key(&ev, TUI_KEY_END);
    tui_table_handle(&t, &ev);
    ASSERT_EQ_INT(tui_table_selected(&t), 5, "End: the last data row");
    key(&ev, TUI_KEY_HOME);
    tui_table_handle(&t, &ev);
    ASSERT_EQ_INT(tui_table_selected(&t), 1, "Home: the first data row");
    tui_listview_set_viewport(&t.lv, 3);
    key(&ev, TUI_KEY_PGDN);
    tui_table_handle(&t, &ev);
    ASSERT_EQ_INT(tui_table_selected(&t), 5, "PgDn lands on a data row");
    key(&ev, TUI_KEY_PGUP);
    tui_table_handle(&t, &ev);
    ASSERT_EQ_INT(tui_table_selected(&t), 2, "PgUp: a page up, then the nearest data row");
    key(&ev, TUI_KEY_PGUP);
    tui_table_handle(&t, &ev);
    ASSERT_EQ_INT(tui_table_selected(&t), 1, "PgUp at the top");
    /* The workspace's letters work as for the list view. */
    memset(&ev, 0, sizeof(ev));
    ev.type = TUI_EV_KEY;
    ev.key = TUI_KEY_CHAR;
    ev.cp = 'j';
    ASSERT_EQ_INT(tui_table_handle(&t, &ev), 1, "j");
    ASSERT_EQ_INT(tui_table_selected(&t), 2, "j: down");
    ev.cp = 'x';
    ASSERT_EQ_INT(tui_table_handle(&t, &ev), 0, "other keys are the program's");
    ASSERT_EQ_INT(tui_table_handle(&t, NULL), 0, "NULL event");
    ASSERT_EQ_INT(tui_table_handle(NULL, &ev), 0, "NULL table");

    /* select() passes over a section row: below it going down, above it
     * going up. */
    tui_table_select(&t, 3);
    ASSERT_EQ_INT(tui_table_selected(&t), 5, "select a section: the row below");
    tui_table_select(&t, 4);
    ASSERT_EQ_INT(tui_table_selected(&t), 2, "from below a section, up: the row above");
    tui_table_select(&t, 3);
    ASSERT_EQ_INT(tui_table_selected(&t), 5, "from above it, down: the row below");
    tui_table_select(&t, 1);
    tui_table_select(&t, 0);
    ASSERT_EQ_INT(tui_table_selected(&t), 1, "above: the nearest below the head");
    tui_table_select(&t, 99);
    ASSERT_EQ_INT(tui_table_selected(&t), 5, "clamped, then settled");
    tui_table_select(&t, -3);
    ASSERT_EQ_INT(tui_table_selected(&t), 1, "negative: the first data row");

    /* A shrunk list: the selection settles on a data row. */
    tui_table_select(&t, 5);
    tui_table_set_count(&t, 5);
    ASSERT_EQ_INT(tui_table_selected(&t), 2, "a shrunk list settles upward");
    /* Only section rows: nothing is selected, nothing is moved. */
    static const Item heads[] = {
        { "", "x", "", 0, 1, NULL, { 0, 0, 0 } }, { "", "y", "", 0, 1, NULL, { 0, 0, 0 } },
    };
    tui_table_model hm = { model_row, model_cell, (void *)heads };
    tui_table h;
    tui_table_init(&h, g_cols, 4, &hm);
    tui_table_set_count(&h, 2);
    ASSERT_EQ_INT(tui_table_selected(&h), -1, "no data row, no selection");
    key(&ev, TUI_KEY_DOWN);
    ASSERT_EQ_INT(tui_table_handle(&h, &ev), 1, "keys are still handled");
    ASSERT_EQ_INT(tui_table_selected(&h), -1, "and select nothing");
    tui_table_select(&h, 1);
    ASSERT_EQ_INT(tui_table_selected(&h), -1, "select finds nothing");
    /* An empty table. */
    tui_table_set_count(&h, 0);
    tui_table_select(&h, 0);
    ASSERT_EQ_INT(tui_table_handle(&h, &ev), 1, "empty: handled");
    ASSERT_EQ_INT(tui_table_selected(&h), -1, "empty: none");
    ASSERT_EQ_INT(tui_table_selected(NULL), -1, "NULL");
    tui_table_select(NULL, 1);
    tui_table_set_count(NULL, 1);
    tui_table_set_total(NULL, 1);
    tui_table_set_message(NULL, "x", TUI_STYLE_DEFAULT);
    tui_table_set_colors(NULL, 8);
    tui_table_init(NULL, g_cols, 4, &m);
}

/* ── Counters, message, single row ──────────────────────────────────── */

static void test_table_counters_message_and_single_row(void) {
    Fx f;
    ASSERT(fx_open(&f, 4, 40) == 0, "fixtures");
    tui_table t;
    make_table(&t, 256);
    ASSERT_EQ_INT(tui_table_first_visible(&t), 0, "first");
    ASSERT_EQ_INT(tui_table_last_visible(&t), -1, "no viewport yet");
    ASSERT_EQ_INT(t.total, 4, "total follows the count");
    tui_table_set_total(&t, 1000);
    ASSERT_EQ_INT(t.total, 1000, "a larger total");
    tui_table_set_total(&t, 2);
    ASSERT_EQ_INT(t.total, 4, "never below the count");
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_table_first_visible(&t), 0, "first after render");
    ASSERT_EQ_INT(tui_table_last_visible(&t), 2, "three rows under the header");
    tui_table_select(&t, 3);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_table_first_visible(&t), 1, "scrolled");
    ASSERT_EQ_INT(tui_table_last_visible(&t), 3, "to the end");
    tui_table_set_count(&t, 3);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    ASSERT_EQ_INT(tui_table_last_visible(&t), 2, "no row past the count");

    /* One row alone: the cursor moves and only the two rows change. */
    tui_table_select(&t, 0);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_EQ_STR(pty_cell_text(f.s, 1, 0), ">", "cursor on row 0");
    tui_table_select(&t, 1);
    ASSERT_EQ_INT(tui_table_render_row(&t, tui_canvas_of(f.scr), 0), 1, "row 0 redrawn");
    ASSERT_EQ_INT(tui_table_render_row(&t, tui_canvas_of(f.scr), 1), 1, "row 1 redrawn");
    fx_show(&f);
    ASSERT_EQ_STR(pty_cell_text(f.s, 1, 0), " ", "cursor left row 0");
    ASSERT_EQ_STR(pty_cell_text(f.s, 2, 0), ">", "cursor on row 1");
    ASSERT_EQ_INT(tui_table_render_row(&t, tui_canvas_of(f.scr), 7), 0, "not in the table");
    ASSERT_EQ_INT(tui_table_render_row(&t, tui_canvas_of(f.scr), -1), 0, "negative");
    tui_canvas two = tui_canvas_sub(tui_canvas_of(f.scr), tui_rect_make(0, 0, 2, 40));
    ASSERT_EQ_INT(tui_table_render_row(&t, two, 2), 0, "below the canvas");
    ASSERT_EQ_INT(tui_table_render_row(NULL, two, 0), 0, "NULL");

    /* A message instead of the header and the rows. */
    tui_table_set_message(&t, "No issues match.", TUI_STYLE_ERROR);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_EQ_STR(rowtext(f.s, 0), "No issues match.", "the message");
    ASSERT_EQ_STR(rowtext(f.s, 1), "", "no rows");
    ASSERT_EQ_INT(pty_cell_fg(f.s, 0, 0), 31, "in the style given");
    ASSERT_EQ_INT(tui_table_first_visible(&t), -1, "no range with a message");
    ASSERT_EQ_INT(tui_table_last_visible(&t), -1, "no range with a message");
    ASSERT_EQ_INT(tui_table_render_row(&t, tui_canvas_of(f.scr), 0), 0, "no row either");
    tui_table_set_message(&t, NULL, TUI_STYLE_DEFAULT);
    tui_table_set_count(&t, 0);
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_show(&f);
    ASSERT_EQ_STR(rowtext(f.s, 0), "  KEY    TITLE                   N ST", "header over no rows");
    ASSERT_EQ_INT(tui_table_first_visible(&t), -1, "no rows: none");
    tui_table_render(NULL, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_close(&f);
}

/* ── Plain text ─────────────────────────────────────────────────────── */

static void test_table_plain_text(void) {
    tui_table t;
    make_table(&t, 256);
    char out[256];
    char w[64];
    int n = tui_table_format_row(&t, 40, 1, out, sizeof(out));
    snprintf(w, sizeof(w), "%-6s %-20s %4s %s", "A-2", "A title that is muc\xE2\x80\xA6", "1234", "DONE");
    ASSERT_EQ_STR(out, w, "a row as the table lays it out");
    ASSERT_EQ_INT(n, (int)strlen(w), "length");
    n = tui_table_format_row(&t, 0, 1, out, sizeof(out));
    ASSERT_EQ_STR(out, "A-2 A title that is much too long to fit 1234 DONE", "no cut, no padding");
    ASSERT(n > 0, "length");
    n = tui_table_format_row(&t, 29, 0, out, sizeof(out));
    snprintf(w, sizeof(w), "%-6s %-15s %4s", "A-1", "Short", "5");
    ASSERT_EQ_STR(out, w, "the hidden column is left out");
    n = tui_table_format_row(&t, 40, 2, out, sizeof(out));
    ASSERT_EQ_STR(out, "Group", "a section row is its text");
    n = tui_table_format_header(&t, 40, out, sizeof(out));
    snprintf(w, sizeof(w), "%-6s %-20s %4s %s", "KEY", "TITLE", "N", "ST");
    ASSERT_EQ_STR(out, w, "the header");
    n = tui_table_format_header(&t, 0, out, sizeof(out));
    ASSERT_EQ_STR(out, "KEY TITLE N ST", "the header, no padding");
    n = tui_table_format_row(&t, 40, 1, out, 10);
    ASSERT_EQ_INT(n, -1, "too small");
    ASSERT_EQ_STR(out, "A-2    A ", "cut and terminated");
    ASSERT_EQ_INT(tui_table_format_row(&t, 40, 9, out, sizeof(out)), -1, "no such row");
    ASSERT_EQ_INT(tui_table_format_row(&t, 40, -1, out, sizeof(out)), -1, "negative row");
    ASSERT_EQ_INT(tui_table_format_row(&t, 40, 0, out, 0), -1, "no room");
    ASSERT_EQ_INT(tui_table_format_row(NULL, 40, 0, out, sizeof(out)), -1, "NULL table");
    ASSERT_EQ_INT(tui_table_format_header(NULL, 40, out, sizeof(out)), -1, "NULL table");
    ASSERT_EQ_INT(tui_table_format_header(&t, 40, out, 0), -1, "no room");
    ASSERT_EQ_INT(tui_table_format_header(&t, 40, NULL, 4), -1, "NULL out");
    /* A section row that does not fit. */
    n = tui_table_format_row(&t, 40, 2, out, 3);
    ASSERT_EQ_INT(n, -1, "section: too small");
}

/* ── Limits of the cell and the row ─────────────────────────────────── */

static void big_cell(void *ctx, int index, int col, tui_table_cell *c) {
    (void)ctx; (void)index; (void)col;
    static char big[2000];
    memset(big, 'x', sizeof(big) - 1);
    big[sizeof(big) - 1] = '\0';
    tui_table_cell_put(c, big, TUI_STYLE_DEFAULT);
    tui_table_cell_put(c, "\xE6\x97\xA5", TUI_STYLE_DEFAULT);  /* does not fit: dropped whole */
    tui_table_cell_put(c, NULL, TUI_STYLE_DEFAULT);
    tui_table_cell_put(c, "", TUI_STYLE_DEFAULT);
}

static void many_spans(void *ctx, int index, int col, tui_table_cell *c) {
    (void)ctx; (void)index; (void)col;
    for (int i = 0; i < 40; i++) tui_table_cell_put(c, "ab", TUI_STYLE_DEFAULT);
}

static void odd_row(void *ctx, int index, tui_table_row *r) {
    (void)ctx; (void)index;
    static char longtext[400];
    memset(longtext, 'y', sizeof(longtext) - 1);
    /* a wide character across the end of the row's text buffer */
    memcpy(longtext + 253, "\xE6\x97\xA5", 3);
    longtext[sizeof(longtext) - 1] = '\0';
    tui_table_row_kind(r, TUI_ROW_SECTION);
    tui_table_row_text(r, longtext);
    tui_table_row_text(r, NULL);
    tui_table_row_text(r, longtext);
    tui_table_row_mark(r, "", TUI_STYLE_DEFAULT);
    tui_table_row_mark(r, NULL, TUI_STYLE_DEFAULT);
    tui_table_row_mark(r, "\xE2\x98\x85 and more", TUI_STYLE_DEFAULT);
    tui_table_row_mark(r, "\xE2", TUI_STYLE_DEFAULT);
}

static void test_table_limits(void) {
    static const tui_table_col one[] = { { "C", 10, 0, TUI_ALIGN_LEFT, 0, 0, 0 } };
    tui_table_model m = { NULL, big_cell, NULL };
    tui_table t;
    tui_table_init(&t, one, 1, &m);
    tui_table_set_count(&t, 1);
    char out[2200];
    int n = tui_table_format_row(&t, 0, 0, out, sizeof(out));
    ASSERT_EQ_INT(n, 1023, "the cell holds a kilobyte; a wide character past it is dropped");
    m.cell = many_spans;
    tui_table_init(&t, one, 1, &m);
    tui_table_set_count(&t, 1);
    n = tui_table_format_row(&t, 0, 0, out, sizeof(out));
    ASSERT_EQ_INT(n, 48, "24 spans, the rest is dropped");

    m.row = odd_row;
    m.cell = NULL;
    tui_table_init(&t, one, 1, &m);
    tui_table_set_count(&t, 1);
    ASSERT_EQ_INT(tui_table_selected(&t), -1, "a section row is not selected");
    n = tui_table_format_row(&t, 0, 0, out, sizeof(out));
    ASSERT_EQ_INT(n, 253, "the row text is cut before the wide character that would not fit");
    Fx f;
    ASSERT(fx_open(&f, 2, 20) == 0, "fixtures");
    tui_table_render(&t, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_close(&f);
    /* No model at all, no columns at all. */
    tui_table e;
    tui_table_init(&e, NULL, 5, NULL);
    ASSERT_EQ_INT(e.ncols, 0, "no columns");
    tui_table_set_count(&e, 3);
    ASSERT_EQ_INT(tui_table_selected(&e), 0, "rows without a model are data rows");
    n = tui_table_format_row(&e, 20, 0, out, sizeof(out));
    ASSERT_EQ_INT(n, 0, "and empty");
    n = tui_table_format_header(&e, 20, out, sizeof(out));
    ASSERT_EQ_INT(n, 0, "header of nothing");
    ASSERT(fx_open(&f, 3, 20) == 0, "fixtures");
    tui_table_render(&e, tui_canvas_of(f.scr), TUI_STYLE_DEFAULT);
    fx_close(&f);
    /* Cell and row functions do nothing for NULL. */
    tui_table_row_kind(NULL, TUI_ROW_DATA);
    tui_table_row_style(NULL, TUI_STYLE_DEFAULT);
    tui_table_row_mark(NULL, "x", TUI_STYLE_DEFAULT);
    tui_table_row_text(NULL, "x");
    tui_table_cell_prefix(NULL, "x", TUI_STYLE_DEFAULT);
    tui_table_cell_put(NULL, "x", TUI_STYLE_DEFAULT);
}

static void set_env(const char *name, const char *value) {
    if (value) setenv(name, value, 1);
    else unsetenv(name);
}

static void test_table_colour_depth_from_the_environment(void) {
    const char *oterm = getenv("TERM"), *oct = getenv("COLORTERM");
    char saveterm[64] = "", savect[64] = "";
    int hadterm = oterm != NULL, hadct = oct != NULL;
    if (oterm) snprintf(saveterm, sizeof(saveterm), "%s", oterm);
    if (oct) snprintf(savect, sizeof(savect), "%s", oct);
    static const struct { const char *term, *ct; int want; const char *what; } cases[] = {
        { "xterm", "truecolor", 256, "COLORTERM set" },
        { "xterm-256color", NULL, 256, "256color" },
        { "xterm", NULL, 16, "xterm: 16" },
        { "screen", "", 16, "an empty COLORTERM does not count" },
        { "linux", NULL, 8, "the console: 8" },
        { "dumb", NULL, 8, "dumb" },
        { "ansi", NULL, 8, "ansi" },
        { "vt100", NULL, 8, "vt100" },
        { "", NULL, 8, "empty TERM" },
        { NULL, NULL, 8, "no TERM" },
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        set_env("TERM", cases[i].term);
        set_env("COLORTERM", cases[i].ct);
        tui_table t;
        tui_table_init(&t, g_cols, 4, NULL);
        ASSERT_EQ_INT(t.colors, cases[i].want, cases[i].what);
    }
    set_env("TERM", hadterm ? saveterm : NULL);
    set_env("COLORTERM", hadct ? savect : NULL);
}

static const TestCase tests[] = {
    TEST_CASE(test_table_widths),
    TEST_CASE(test_table_render_256),
    TEST_CASE(test_table_render_colour_depths),
    TEST_CASE(test_table_selection_colour_mapping),
    TEST_CASE(test_table_cuts_and_alignment),
    TEST_CASE(test_table_header_and_ruler),
    TEST_CASE(test_table_row_style_and_sections),
    TEST_CASE(test_table_navigation_skips_sections),
    TEST_CASE(test_table_counters_message_and_single_row),
    TEST_CASE(test_table_plain_text),
    TEST_CASE(test_table_limits),
    TEST_CASE(test_table_colour_depth_from_the_environment),
};

int main(int argc, char **argv) {
    return RUN_SUITE("table", tests, 160, 60);
}
