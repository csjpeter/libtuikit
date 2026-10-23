/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-23 */
/* Every widget draws with the roles of the active colour scheme: cell colours under a test scheme. */

/*
 * A scheme file gives every role its own colour (colours by index, so that no
 * two roles share one).  Each widget is drawn into a screen, fed to
 * libptytest's emulator and its cells are read: a widget that drew with a
 * fixed colour instead of its role fails here (TUI-65).
 */

#include "test_helpers.h"
#include "tuikit/actions.h"
#include "tuikit/dialog.h"
#include "tuikit/fieldset.h"
#include "tuikit/listview.h"
#include "tuikit/scheme.h"
#include "tuikit/statusline.h"
#include "tuikit/table.h"
#include "../../src/internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

int g_tests_run = 0;
int g_tests_failed = 0;

#define C_TEXT_FG   250
#define C_TEXT_BG   232
#define C_HEAD_FG   16
#define C_HEAD_BG   220
#define C_STAT_FG   17
#define C_STAT_BG   51
#define C_SEL_BG    52
#define C_ACC_FG    201
#define C_FRAME_FG  46
#define C_IN_FG     226
#define C_IN_BG     18
#define C_FOC_FG    16
#define C_FOC_BG    28
#define C_OK_FG     40
#define C_ERR_FG    160
#define C_HINT_FG   178
#define C_PROG_FG   99

#define IDX(n) PTY_COLOR_IDX(n)
#define ANY  (-1)   /* a colour the test does not pin */

static const char g_scheme[] =
    "text         = 250 232 -\n"
    "header       = 16 220 bold\n"
    "status       = 17 51 -\n"
    "selection    = - 52 -\n"
    "cursor       = - - reverse\n"
    "accent       = 201 - bold\n"
    "frame        = 46 - bold\n"
    "input        = 226 18 -\n"
    "input.focus  = 16 28 underline\n"
    "tab.active   = 129 - -\n"
    "tab.inactive = 240 - -\n"
    "ok           = 40 - bold\n"
    "error        = 160 - bold\n"
    "hint         = 178 - -\n"
    "progress     = 99 - italic\n";

static void mkdir_one(const char *path) { (void)mkdir(path, 0700); }

/* Makes the scheme the active one: a tuikit.conf and the file in a user
 * directory, XDG_CONFIG_HOME pointing at it, a colour depth of 256. */
static int use_scheme(const char *text) {
    static char root[600];
    char path[700];
    snprintf(root, sizeof root, "%s/roles-%d", TUIKIT_TEST_TMPDIR, (int)getpid());
    mkdir_one(root);
    snprintf(path, sizeof path, "%s/tuikit", root);
    mkdir_one(path);
    snprintf(path, sizeof path, "%s/tuikit/schemes", root);
    mkdir_one(path);
    snprintf(path, sizeof path, "%s/tuikit/schemes/roles.scheme", root);
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fputs(text, f);
    fclose(f);
    snprintf(path, sizeof path, "%s/tuikit/tuikit.conf", root);
    f = fopen(path, "w");
    if (!f) return -1;
    fputs("scheme = roles\n", f);
    fclose(f);
    setenv("XDG_CONFIG_HOME", root, 1);
    setenv("TERM", "xterm-256color", 1);
    unsetenv("COLORTERM");
    return tui_scheme_init();
}

typedef struct {
    tui_screen *scr;
    tui_buf     buf;
    PtySession *s;
    int         rows, cols;
} Fx;

static int fx_open(Fx *f, int rows, int cols) {
    f->rows = rows;
    f->cols = cols;
    f->scr = tui_screen_new(rows, cols);
    tui_buf_init(&f->buf);
    f->s = pty_open(cols, rows);
    return f->scr && f->s ? 0 : -1;
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

/* The first cell of the text @p what on the screen (ASCII text, one row). */
static int find(Fx *f, const char *what, int *row, int *col) {
    size_t n = strlen(what);
    for (int r = 0; r < f->rows; r++) {
        for (int c = 0; c + (int)n <= f->cols; c++) {
            size_t k = 0;
            while (k < n) {
                const char *t = pty_cell_text(f->s, r, c + (int)k);
                if (!t || t[0] != what[k] || t[1] != '\0') break;
                k++;
            }
            if (k == n) {
                *row = r;
                *col = c;
                return 1;
            }
        }
    }
    return 0;
}

/* The text @p what is on the screen with this foreground, background and
 * (at least) these attributes. */
#define EXPECT_AT(f, what, fg, bg, attrs) do { \
    int _er = 0, _ec = 0; \
    g_tests_run++; \
    if (!find(f, what, &_er, &_ec)) { \
        PTY_ASSERT_FAIL_("\"%s\" is not on the screen", what); \
        return; \
    } \
    if ((fg) != ANY) ASSERT_CELL_FG((f)->s, _er, _ec, fg); \
    if ((bg) != ANY) ASSERT_CELL_BG((f)->s, _er, _ec, bg); \
    ASSERT_CELL_ATTR((f)->s, _er, _ec, attrs); \
} while (0)

/* The same, and none of the attributes in @p not. */
#define EXPECT_NOT_ATTR(f, what, not) do { \
    int _er = 0, _ec = 0; \
    g_tests_run++; \
    if (!find(f, what, &_er, &_ec)) { \
        PTY_ASSERT_FAIL_("\"%s\" is not on the screen", what); \
        return; \
    } \
    if (pty_cell_attr((f)->s, _er, _ec) & (not)) { \
        PTY_ASSERT_FAIL_("\"%s\" has an attribute it should not have", what); \
        return; \
    } \
} while (0)

/* ── The palette macros read the active scheme ──────────────────────── */

static void test_macros_are_roles(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    ASSERT_EQ_INT(TUI_STYLE_HEADER.bg, TUI_COLOR_256(C_HEAD_BG), "TUI_STYLE_HEADER");
    ASSERT_EQ_INT(TUI_STYLE_ACCENT.fg, TUI_COLOR_256(C_ACC_FG), "TUI_STYLE_ACCENT");
    ASSERT_EQ_INT(TUI_STYLE_OK.fg, TUI_COLOR_256(C_OK_FG), "TUI_STYLE_OK");
    ASSERT_EQ_INT(TUI_STYLE_ERROR.fg, TUI_COLOR_256(C_ERR_FG), "TUI_STYLE_ERROR");
    ASSERT_EQ_INT(TUI_STYLE_HINT.fg, TUI_COLOR_256(C_HINT_FG), "TUI_STYLE_HINT");
    ASSERT_EQ_INT(TUI_STYLE_PROGRESS.fg, TUI_COLOR_256(C_PROG_FG), "TUI_STYLE_PROGRESS");
    ASSERT_EQ_INT(TUI_STYLE_SELECTION.bg, TUI_COLOR_256(C_SEL_BG), "TUI_STYLE_SELECTION");
    ASSERT_EQ_INT(tui_selection_style().bg, TUI_COLOR_256(C_SEL_BG), "tui_selection_style()");
}

/* ── Table ──────────────────────────────────────────────────────────── */

static void t_row(void *ctx, int index, tui_table_row *r) {
    (void)ctx;
    (void)index;
    (void)r;
}

static void t_cell(void *ctx, int index, int col, tui_table_cell *c) {
    (void)ctx;
    static const char *const names[] = { "alpha", "bravo", "charlie" };
    tui_table_cell_put(c, col == 0 ? names[index] : "cell", TUI_STYLE_DEFAULT);
}

static void test_table(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    static const tui_table_col cols[] = {
        { "NAME", 8, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
        { "THING", 8, 1, TUI_ALIGN_LEFT, 0, 0, 0 },
    };
    Fx f;
    ASSERT(fx_open(&f, 5, 30) == 0, "fixtures");
    tui_table_model m = { t_row, t_cell, NULL };
    tui_table t;
    tui_table_init(&t, cols, 2, &m);
    tui_table_set_colors(&t, 256);
    tui_table_set_count(&t, 3);
    tui_table_select(&t, 1);
    tui_table_render(&t, tui_canvas_of(f.scr), tui_scheme_style(TUI_ROLE_TEXT));
    fx_show(&f);
    EXPECT_AT(&f, "NAME", IDX(C_TEXT_FG), IDX(C_TEXT_BG), PTY_ATTR_BOLD);
    EXPECT_AT(&f, "alpha", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    EXPECT_AT(&f, "charlie", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    EXPECT_AT(&f, "bravo", IDX(C_TEXT_FG), IDX(C_SEL_BG), 0);
    tui_table_set_message(&t, "nothing here", t.message_style);   /* the one tui_table_init() set */
    tui_table_render(&t, tui_canvas_of(f.scr), tui_scheme_style(TUI_ROLE_TEXT));
    fx_show(&f);
    EXPECT_AT(&f, "nothing here", IDX(C_TEXT_FG), IDX(C_TEXT_BG), PTY_ATTR_DIM);
    tui_table_free(&t);
    fx_close(&f);
}

/* ── List view: a program's rows drawn with the palette macros ──────── */

static void l_draw(void *ctx, tui_canvas row, int index, int selected) {
    (void)ctx;
    tui_style st = selected ? TUI_STYLE_SELECTION : TUI_STYLE_HINT;
    tui_canvas_clear(row, st);
    tui_canvas_put(row, 0, 0, index == 0 ? "first" : "second", st);
}

static void test_list(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    Fx f;
    ASSERT(fx_open(&f, 3, 20) == 0, "fixtures");
    tui_listview lv;
    tui_listview_init(&lv);
    tui_listview_set_count(&lv, 2);
    tui_listview_select(&lv, 1);
    tui_listview_render(&lv, tui_canvas_of(f.scr), tui_scheme_style(TUI_ROLE_TEXT), l_draw, NULL);
    fx_show(&f);
    EXPECT_AT(&f, "first", IDX(C_HINT_FG), ANY, 0);
    EXPECT_AT(&f, "second", ANY, IDX(C_SEL_BG), 0);
    fx_close(&f);
}

/* ── Dialogs ────────────────────────────────────────────────────────── */

static void draw_dialog(Fx *f, tui_dialog *d) {
    tui_canvas all = tui_canvas_of(f->scr);
    tui_canvas_clear(all, tui_scheme_style(TUI_ROLE_TEXT));
    tui_dialog_render(d, all);
    fx_show(f);
}

static void test_dialog_message(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    tui_dialog d;
    ASSERT_EQ_INT(tui_dialog_message(&d, "Note", "Saved."), 0, "create");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    draw_dialog(&f, &d);
    int r, c;
    ASSERT(find(&f, "Note", &r, &c), "the title");
    ASSERT_CELL_FG(f.s, r, c - 1, IDX(C_FRAME_FG));          /* the frame */
    ASSERT_CELL_ATTR(f.s, r, c - 1, PTY_ATTR_BOLD);
    EXPECT_AT(&f, "Saved.", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    /* the button: the selection role, in bold (not by colour alone) */
    EXPECT_AT(&f, "OK", ANY, IDX(C_SEL_BG), PTY_ATTR_BOLD);
    tui_dialog_free(&d);
    fx_close(&f);
}

static void test_dialog_confirm(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    tui_dialog d;
    ASSERT_EQ_INT(tui_dialog_confirm(&d, "Sure", "Delete it?", 0), 0, "create");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    draw_dialog(&f, &d);
    EXPECT_AT(&f, "Delete it?", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    EXPECT_AT(&f, "No", ANY, IDX(C_SEL_BG), PTY_ATTR_BOLD);   /* the focused button */
    EXPECT_AT(&f, "Yes", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    tui_dialog_free(&d);
    fx_close(&f);
}

static void test_dialog_prompt(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    tui_dialog d;
    char buf[32];
    ASSERT_EQ_INT(tui_dialog_prompt(&d, "Name", "Your name?", buf, sizeof buf, "ada"), 0, "create");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    draw_dialog(&f, &d);
    EXPECT_AT(&f, "Your name?", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    EXPECT_AT(&f, "ada", IDX(C_FOC_FG), IDX(C_FOC_BG), PTY_ATTR_UNDERLINE);   /* input.focus over input */
    tui_dialog_free(&d);
    fx_close(&f);
}

static void test_dialog_menu(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    static const char *const items[] = { "red", "green", "blue" };
    tui_dialog d;
    ASSERT_EQ_INT(tui_dialog_menu(&d, "Pick", items, 3, 1), 0, "create");
    Fx f;
    ASSERT(fx_open(&f, 10, 30) == 0, "fixtures");
    draw_dialog(&f, &d);
    EXPECT_AT(&f, "red", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    EXPECT_AT(&f, "green", IDX(C_TEXT_FG), IDX(C_SEL_BG), 0);
    tui_dialog_free(&d);
    fx_close(&f);
}

static void test_dialog_checklist(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    static const char *const items[] = { "one", "two" };
    tui_dialog d;
    ASSERT_EQ_INT(tui_dialog_checklist(&d, "Tick", items, 2, NULL, 0), 0, "create");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    draw_dialog(&f, &d);
    EXPECT_AT(&f, "[ ] one", IDX(C_TEXT_FG), IDX(C_SEL_BG), 0);
    EXPECT_AT(&f, "[ ] two", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    tui_dialog_free(&d);
    fx_close(&f);
}

static void test_dialog_help(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    static const char *const pairs[] = { "j", "down", "k", "up" };
    tui_dialog d;
    ASSERT_EQ_INT(tui_dialog_help(&d, "Keys", pairs, 2), 0, "create");
    Fx f;
    ASSERT(fx_open(&f, 10, 30) == 0, "fixtures");
    draw_dialog(&f, &d);
    int r, c;
    ASSERT(find(&f, "Keys", &r, &c), "the title");
    ASSERT_CELL_FG(f.s, r, c - 1, IDX(C_FRAME_FG));
    EXPECT_AT(&f, "down", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    EXPECT_AT(&f, "up", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    tui_dialog_free(&d);
    fx_close(&f);
}

/* ── Fieldset ───────────────────────────────────────────────────────── */

static void test_fieldset(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    char a[32] = "ann", b[32] = "bob";
    tui_field fields[2];
    tui_field_text(&fields[0], "To:", a, sizeof a, "ann");
    tui_field_text(&fields[1], "Cc:", b, sizeof b, "bob");
    tui_fieldset fs;
    tui_fieldset_init(&fs, fields, 2);
    tui_fieldset_set_error(&fs, "Broken.");
    fs.ncand = 2;
    fs.cand[0].text = "x";
    fs.cand[0].label = "cand-one";
    fs.cand[1].text = "y";
    fs.cand[1].label = "cand-two";
    fs.cand_sel = 1;
    Fx f;
    ASSERT(fx_open(&f, 10, 40) == 0, "fixtures");
    tui_canvas all = tui_canvas_of(f.scr);
    tui_canvas_clear(all, tui_scheme_style(TUI_ROLE_TEXT));
    tui_fieldset_render(&fs, all);
    fx_show(&f);
    EXPECT_AT(&f, "To:", IDX(C_ACC_FG), ANY, PTY_ATTR_BOLD);     /* the focused label */
    EXPECT_AT(&f, "Cc:", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    EXPECT_AT(&f, "ann", IDX(C_FOC_FG), IDX(C_FOC_BG), PTY_ATTR_UNDERLINE); /* the focused input */
    EXPECT_AT(&f, "bob", IDX(C_IN_FG), IDX(C_IN_BG), 0);                    /* an input */
    EXPECT_AT(&f, "cand-one", IDX(C_TEXT_FG), IDX(C_TEXT_BG), 0);
    EXPECT_AT(&f, "cand-two", ANY, IDX(C_SEL_BG), 0);            /* the chosen one */
    EXPECT_AT(&f, "Broken.", IDX(C_ERR_FG), ANY, PTY_ATTR_BOLD);
    fx_close(&f);
    tui_fieldset_free(&fs);
}

/* ── Status line ────────────────────────────────────────────────────── */

static void test_statusline(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    static const tui_statusline_hint hints[] = { { "Enter", "open", 1 }, { "q", "quit", 1 } };
    tui_statusline sl;
    tui_statusline_init(&sl);
    tui_statusline_set_hints(&sl, hints, 2);
    Fx f;
    ASSERT(fx_open(&f, 1, 40) == 0, "fixtures");
    tui_canvas all = tui_canvas_of(f.scr);
    tui_statusline_render(&sl, all);
    fx_show(&f);
    EXPECT_AT(&f, "open", IDX(C_STAT_FG), IDX(C_STAT_BG), 0);   /* the bar */
    EXPECT_NOT_ATTR(&f, "open", PTY_ATTR_REVERSE);
    EXPECT_AT(&f, "Enter", IDX(C_STAT_FG), IDX(C_STAT_BG), PTY_ATTR_BOLD);

    tui_statusline_set_message(&sl, "Failed", TUI_STYLE_ERROR);
    tui_statusline_render(&sl, all);
    fx_show(&f);
    EXPECT_AT(&f, "Failed", IDX(C_ERR_FG), ANY, PTY_ATTR_BOLD | PTY_ATTR_REVERSE);

    tui_statusline_set_message(&sl, "Done", TUI_STYLE_OK);
    tui_statusline_render(&sl, all);
    fx_show(&f);
    EXPECT_AT(&f, "Done", IDX(C_OK_FG), ANY, PTY_ATTR_BOLD | PTY_ATTR_REVERSE);

    tui_statusline_clear_message(&sl);
    tui_statusline_set_wait(&sl, "Working", 1, 4);
    tui_statusline_render(&sl, all);
    fx_show(&f);
    EXPECT_AT(&f, "Working", IDX(C_PROG_FG), ANY, PTY_ATTR_ITALIC | PTY_ATTR_REVERSE);
    tui_statusline_free(&sl);
    fx_close(&f);
}

/* ── Action menu (quick keys in the accent) ─────────────────────────── */

static void test_quickmenu(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    static const char *const items[] = { "open", "delete" };
    tui_dialog d;
    ASSERT_EQ_INT(tui__dialog_quickmenu(&d, "Actions", items, 2, 0), 0, "create");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    draw_dialog(&f, &d);
    int r, c;
    ASSERT(find(&f, "delete", &r, &c), "the second item");
    ASSERT_CELL_FG(f.s, r, c, IDX(C_ACC_FG));               /* its quick key */
    ASSERT_CELL_ATTR(f.s, r, c, PTY_ATTR_BOLD);
    ASSERT_CELL_FG(f.s, r, c + 1, IDX(C_TEXT_FG));
    ASSERT(find(&f, "open", &r, &c), "the first item");
    ASSERT_CELL_BG(f.s, r, c, IDX(C_SEL_BG));               /* the selected one keeps the selection */
    tui_dialog_free(&d);
    fx_close(&f);
}

/* ── The scheme's selection at a lower colour depth ─────────────────── */

static void test_selection_reduced_by_depth(void) {
    ASSERT_EQ_INT(use_scheme(g_scheme), 0, "the test scheme loads");
    ASSERT_EQ_INT(tui__selection_base(256).bg, TUI_COLOR_256(C_SEL_BG), "256 colours: as written");
    tui_style s16 = tui__selection_base(16);
    ASSERT_EQ_INT(s16.bg, tui_color_reduce(TUI_COLOR_256(C_SEL_BG), 16), "16 colours: reduced");
    ASSERT(s16.bg != TUI_COLOR_256(C_SEL_BG), "and no longer the 256-colour index");
    ASSERT_EQ_INT(tui__selection_base(8).bg, tui_color_reduce(TUI_COLOR_256(C_SEL_BG), 8), "8 colours: reduced");
}

/* ── With no scheme the widgets are what they were ──────────────────── */

static void test_no_scheme_is_the_old_look(void) {
    ASSERT_EQ_INT(use_scheme("# nothing: every role built-in\n"), 0, "an empty scheme loads");
    tui_dialog d;
    ASSERT_EQ_INT(tui_dialog_confirm(&d, "Sure", "Delete it?", 1), 0, "create");
    Fx f;
    ASSERT(fx_open(&f, 9, 30) == 0, "fixtures");
    draw_dialog(&f, &d);
    EXPECT_AT(&f, "Yes", ANY, ANY, PTY_ATTR_BOLD | PTY_ATTR_REVERSE);   /* as before */
    int r, c;
    ASSERT(find(&f, "Sure", &r, &c), "the title");
    ASSERT_CELL_FG(f.s, r, c - 1, PTY_FG_CYAN);
    ASSERT_CELL_ATTR(f.s, r, c - 1, PTY_ATTR_BOLD);
    tui_dialog_free(&d);
    fx_close(&f);
}

static const TestCase tests[] = {
    TEST_CASE(test_macros_are_roles),
    TEST_CASE(test_table),
    TEST_CASE(test_list),
    TEST_CASE(test_dialog_message),
    TEST_CASE(test_dialog_confirm),
    TEST_CASE(test_dialog_prompt),
    TEST_CASE(test_dialog_menu),
    TEST_CASE(test_dialog_checklist),
    TEST_CASE(test_dialog_help),
    TEST_CASE(test_fieldset),
    TEST_CASE(test_statusline),
    TEST_CASE(test_quickmenu),
    TEST_CASE(test_selection_reduced_by_depth),
    TEST_CASE(test_no_scheme_is_the_old_look),
};

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return RUN_SUITE("roles", tests, 20, 60);
}
