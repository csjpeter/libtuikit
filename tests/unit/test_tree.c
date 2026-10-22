/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-20 */
/* Unit tests of tui_tree: the open state, the movement rules, filters, drawing. */

/*
 * policies/tui-interface-model.md, "Trees": closed on opening, the marker in
 * the first two cells, the keys, the open state kept by identity over a
 * reload, a filter that opens the branches above a match and restores the
 * earlier state when cleared.
 */

#include "test_helpers.h"
#include "faults.h"
#include "tuikit/tree.h"

#include <errno.h>
#include <string.h>

int g_tests_run = 0;
int g_tests_failed = 0;

/*  0 A            1 A1           2 A1a   3 A1b
 *  4 A2           5 B            6 B1    7 C        */
static const tui_treerow g_rows[] = {
    { "A", 0, 2 }, { "A1", 1, 2 }, { "A1a", 2, 0 }, { "A1b", 2, 0 },
    { "A2", 1, 0 }, { "B", 0, 1 }, { "B1", 1, 0 }, { "C", 0, 0 } };
#define N_ROWS 8

static tui_event key_ev(tui_key k) {
    tui_event e;
    memset(&e, 0, sizeof e);
    e.type = TUI_EV_KEY;
    e.key = k;
    return e;
}

static tui_event char_ev(uint32_t cp) {
    tui_event e = key_ev(TUI_KEY_CHAR);
    e.cp = cp;
    return e;
}

static int press(tui_tree *t, tui_event e) { return tui_tree_handle(t, &e); }
#define RIGHT press(&t, key_ev(TUI_KEY_RIGHT))
#define LEFT  press(&t, key_ev(TUI_KEY_LEFT))
#define DOWN  press(&t, key_ev(TUI_KEY_DOWN))

/* The visible ids, comma separated; the selected one in brackets. */
static const char *shown(const tui_tree *t) {
    static char buf[256];
    buf[0] = '\0';
    for (int i = 0; i < t->list.count; i++) {
        const char *id = t->rows[tui_tree_row_at(t, i)].id;
        if (i) strcat(buf, ",");
        strcat(buf, i == t->list.selected ? "[" : "");
        strcat(buf, id);
        strcat(buf, i == t->list.selected ? "]" : "");
    }
    return buf;
}

static void load(tui_tree *t) {
    tui_tree_init(t);
    ASSERT_EQ_INT(tui_tree_set_rows(t, g_rows, N_ROWS), 0, "set_rows");
}

static void test_closed_on_opening(void) {
    tui_tree t;
    load(&t);
    ASSERT_EQ_STR(shown(&t), "[A],B,C", "only the top level");
    ASSERT_EQ_STR(tui_tree_marker(&t, 0), "+ ", "closed with children");
    ASSERT_EQ_STR(tui_tree_marker(&t, 7), "  ", "leaf");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 0), 0, "A closed");
    ASSERT_EQ_INT(tui_tree_selected_row(&t), 0, "first selected");
    tui_tree_free(&t);
}

static void test_right_and_left(void) {
    tui_tree t;
    load(&t);
    ASSERT_EQ_INT(RIGHT, 1, "Right is used");
    ASSERT_EQ_STR(shown(&t), "[A],A1,A2,B,C", "Right opens");
    ASSERT_EQ_STR(tui_tree_marker(&t, 0), "- ", "open marker");
    ASSERT_EQ_INT(RIGHT, 1, "Right on an open row");
    ASSERT_EQ_STR(shown(&t), "A,[A1],A2,B,C", "moves to the first child");
    ASSERT_EQ_INT(press(&t, char_ev('l')), 1, "l is used");
    ASSERT_EQ_STR(shown(&t), "A,[A1],A1a,A1b,A2,B,C", "l opens");
    press(&t, char_ev('l'));
    ASSERT_EQ_STR(shown(&t), "A,A1,[A1a],A1b,A2,B,C", "l on an open row: first child");
    press(&t, char_ev('l'));
    ASSERT_EQ_STR(shown(&t), "A,A1,[A1a],A1b,A2,B,C", "Right on a leaf does nothing");
    ASSERT_EQ_INT(LEFT, 1, "Left is used");
    ASSERT_EQ_STR(shown(&t), "A,[A1],A1a,A1b,A2,B,C", "Left on a leaf: the parent");
    press(&t, char_ev('h'));
    ASSERT_EQ_STR(shown(&t), "A,[A1],A2,B,C", "h closes an open row");
    LEFT;
    ASSERT_EQ_STR(shown(&t), "[A],A1,A2,B,C", "Left on a closed row: the parent");
    LEFT;
    ASSERT_EQ_STR(shown(&t), "[A],B,C", "Left closes the top row");
    LEFT;
    ASSERT_EQ_STR(shown(&t), "[A],B,C", "Left on a closed top row does nothing");
    tui_tree_free(&t);
}

static void test_last_item_and_empty(void) {
    tui_tree t;
    load(&t);
    tui_tree_set_open(&t, 5, 1);
    tui_tree_select_row(&t, 6);
    ASSERT_EQ_INT(RIGHT, 1, "Right on a leaf");
    tui_tree_select_row(&t, 5);
    tui_tree_set_rows(&t, g_rows, 6); /* B is last, open, its child gone */
    tui_tree_select_row(&t, 5);
    DOWN;
    tui_tree_free(&t);

    tui_tree_init(&t);
    ASSERT_EQ_INT(RIGHT, 1, "Right on an empty tree is used");
    ASSERT_EQ_INT(LEFT, 1, "Left on an empty tree is used");
    ASSERT_EQ_INT(tui_tree_selected_row(&t), -1, "nothing selected");
    ASSERT_EQ_STR(tui_tree_marker(&t, 0), "  ", "no row");
    ASSERT_EQ_INT(tui_tree_set_rows(&t, NULL, 0), 0, "zero rows");
    tui_tree_free(&t);

    /* An open row whose only visible neighbour does not exist: the last item. */
    static const tui_treerow one[] = { { "X", 0, 1 }, { "X1", 1, 0 } };
    tui_tree_init(&t);
    tui_tree_set_rows(&t, one, 2);
    tui_tree_set_open(&t, 0, 1);
    tui_tree_set_rows(&t, one, 1);   /* X still open, but its child is gone */
    ASSERT_EQ_INT(RIGHT, 1, "Right on the last, open row");
    ASSERT_EQ_STR(shown(&t), "[X]", "stays");
    tui_tree_free(&t);
}

static void test_enter_and_movement(void) {
    tui_tree t;
    load(&t);
    ASSERT_EQ_INT(press(&t, key_ev(TUI_KEY_ENTER)), 0, "Enter is not used");
    ASSERT_EQ_STR(shown(&t), "[A],B,C", "Enter does not toggle");
    tui_tree_set_open(&t, 0, 1);
    ASSERT_EQ_INT(press(&t, char_ev('j')), 1, "j");
    ASSERT_EQ_STR(shown(&t), "A,[A1],A2,B,C", "j moves over visible rows");
    press(&t, char_ev('G'));
    ASSERT_EQ_STR(shown(&t), "A,A1,A2,B,[C]", "G");
    press(&t, char_ev('k'));
    ASSERT_EQ_STR(shown(&t), "A,A1,A2,[B],C", "k");
    press(&t, key_ev(TUI_KEY_HOME));
    ASSERT_EQ_STR(shown(&t), "[A],A1,A2,B,C", "Home");
    ASSERT_EQ_INT(press(&t, char_ev('x')), 0, "other keys are not used");
    tui_event ctl = char_ev('l');
    ctl.mods = TUI_MOD_CTRL;
    ASSERT_EQ_INT(tui_tree_handle(&t, &ctl), 0, "ctrl-l is not Right");
    tui_event resize;
    memset(&resize, 0, sizeof resize);
    resize.type = TUI_EV_RESIZE;
    ASSERT_EQ_INT(tui_tree_handle(&t, &resize), 0, "not a key");
    ASSERT_EQ_INT(tui_tree_handle(NULL, &ctl), 0, "NULL tree");
    ASSERT_EQ_INT(tui_tree_handle(&t, NULL), 0, "NULL event");
    tui_tree_free(&t);
}

static void test_open_all_close_all(void) {
    tui_tree t;
    load(&t);
    ASSERT_EQ_INT(tui_tree_open_all(&t), 0, "open_all");
    ASSERT_EQ_INT(t.list.count, N_ROWS, "everything shows");
    ASSERT_EQ_STR(tui_tree_marker(&t, 1), "- ", "A1 open");
    tui_tree_select_row(&t, 3);
    tui_tree_close_all(&t);
    ASSERT_EQ_STR(shown(&t), "[A],B,C", "close_all: the selection goes to the ancestor");
    tui_tree_close_all(&t);
    ASSERT_EQ_INT(t.list.count, 3, "closing again changes nothing");
    tui_tree_free(&t);
}

static void test_select_row_and_set_open(void) {
    tui_tree t;
    load(&t);
    ASSERT_EQ_INT(tui_tree_select_row(&t, 3), 0, "select_row");
    ASSERT_EQ_STR(shown(&t), "A,A1,A1a,[A1b],A2,B,C", "the branches above open");
    ASSERT_EQ_INT(tui_tree_select_row(&t, 99), -1, "no such row");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_tree_set_open(&t, -1, 1), -1, "set_open: no such row");
    ASSERT_EQ_INT(tui_tree_set_open(&t, 2, 1), 0, "a leaf cannot be opened");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 2), 0, "and stays a leaf");
    ASSERT_EQ_INT(tui_tree_set_open(&t, 1, 0), 0, "close A1 under the selection");
    ASSERT_EQ_STR(shown(&t), "A,[A1],A2,B,C", "the selection moved to A1");
    ASSERT_EQ_INT(tui_tree_set_open(&t, 5, 0), 0, "closing a closed row");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 99), 0, "is_open: no such row");
    ASSERT_EQ_INT(tui_tree_is_open(NULL, 0), 0, "is_open: NULL");
    ASSERT_EQ_INT(tui_tree_row_at(&t, -1), -1, "row_at: below");
    ASSERT_EQ_INT(tui_tree_row_at(&t, 99), -1, "row_at: above");
    ASSERT_EQ_INT(tui_tree_row_at(NULL, 0), -1, "row_at: NULL");
    ASSERT_EQ_INT(tui_tree_selected_row(NULL), -1, "selected_row: NULL");
    ASSERT_EQ_INT(tui_tree_set_open(NULL, 0, 1), -1, "set_open: NULL");
    ASSERT_EQ_INT(tui_tree_select_row(NULL, 0), -1, "select_row: NULL");
    ASSERT_EQ_INT(tui_tree_open_all(NULL), -1, "open_all: NULL");
    ASSERT_EQ_INT(tui_tree_show_matches(NULL, NULL), -1, "show_matches: NULL tree");
    tui_tree_close_all(NULL);
    tui_tree_show_counts(NULL, 1);
    tui_tree_init(NULL);
    tui_tree_free(NULL);
    tui_tree_free(&t);
}

static void test_reload_keeps_state(void) {
    tui_tree t;
    load(&t);
    tui_tree_select_row(&t, 3);          /* A, A1 open, A1b selected */
    tui_tree_set_open(&t, 5, 1);         /* B open */
    /* A reload: the order changed, a row was added at the top. */
    static const tui_treerow now[] = {
        { "Z", 0, 1 }, { "Z1", 1, 0 },
        { "B", 0, 1 }, { "B1", 1, 0 }, { "B2", 1, 0 },
        { "A", 0, 2 }, { "A1", 1, 2 }, { "A1a", 2, 0 }, { "A1b", 2, 0 }, { "A2", 1, 0 } };
    ASSERT_EQ_INT(tui_tree_set_rows(&t, now, 10), 0, "reload");
    ASSERT_EQ_INT(t.list.count, 9, "Z closed, B, A and A1 still open");
    ASSERT_EQ_STR(t.rows[tui_tree_selected_row(&t)].id, "A1b", "the selection follows its id");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 2), 1, "B still open");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 0), 0, "Z closed");
    /* The selected row is gone: the position is kept, clamped. */
    static const tui_treerow gone[] = { { "B", 0, 0 }, { "C", 0, 0 } };
    ASSERT_EQ_INT(tui_tree_set_rows(&t, gone, 2), 0, "reload without the selection");
    ASSERT_EQ_INT(t.list.selected, 1, "clamped");
    tui_tree_free(&t);
}

static void test_invalid_rows(void) {
    tui_tree t;
    load(&t);
    static const tui_treerow noid[] = { { NULL, 0, 0 } };
    static const tui_treerow neg[] = { { "a", -1, 0 } };
    static const tui_treerow deep[] = { { "a", 1, 0 } };
    static const tui_treerow jump[] = { { "a", 0, 1 }, { "b", 2, 0 } };
    ASSERT_EQ_INT(tui_tree_set_rows(&t, noid, 1), -1, "no id");
    ASSERT_EQ_INT(errno, EINVAL, "EINVAL");
    ASSERT_EQ_INT(tui_tree_set_rows(&t, neg, 1), -1, "negative depth");
    ASSERT_EQ_INT(tui_tree_set_rows(&t, deep, 1), -1, "first row deeper than 0");
    ASSERT_EQ_INT(tui_tree_set_rows(&t, jump, 2), -1, "depth jumps by two");
    ASSERT_EQ_INT(tui_tree_set_rows(&t, g_rows, -1), -1, "negative count");
    ASSERT_EQ_INT(tui_tree_set_rows(&t, NULL, 3), -1, "no rows");
    ASSERT_EQ_INT(tui_tree_set_rows(NULL, g_rows, 1), -1, "no tree");
    ASSERT_EQ_STR(shown(&t), "[A],B,C", "unchanged after a refusal");
    tui_tree_free(&t);
}

static void test_filter_opens_and_restores(void) {
    tui_tree t;
    load(&t);
    tui_tree_set_open(&t, 5, 1);                      /* the user opened B */
    unsigned char m[N_ROWS] = { 0 };
    m[3] = 1;                                         /* A1b matches */
    ASSERT_EQ_INT(tui_tree_show_matches(&t, m), 0, "show_matches");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 0), 1, "A opened above the match");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 1), 1, "A1 opened above the match");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 5), 1, "B kept");
    ASSERT_EQ_STR(shown(&t), "[A],A1,A1a,A1b,A2,B,B1,C", "the match shows");
    /* A new filter replaces the earlier one. */
    memset(m, 0, sizeof m);
    m[6] = 1;
    ASSERT_EQ_INT(tui_tree_show_matches(&t, m), 0, "a second filter");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 0), 0, "A closes again with the first filter");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 5), 1, "B stays open");
    tui_tree_select_row(&t, 6);
    ASSERT_EQ_INT(tui_tree_show_matches(&t, NULL), 0, "clear");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 5), 1, "the earlier state is back: B");
    ASSERT_EQ_INT(tui_tree_is_open(&t, 0), 0, "the earlier state is back: A");
    ASSERT_EQ_STR(shown(&t), "A,B,[B1],C", "the selection stays on its row");
    ASSERT_EQ_INT(tui_tree_show_matches(&t, NULL), 0, "clearing twice is harmless");
    /* A reload while filtering: the program shows the matches again. */
    ASSERT_EQ_INT(tui_tree_show_matches(&t, m), 0, "filter again");
    ASSERT_EQ_INT(tui_tree_set_rows(&t, g_rows, N_ROWS), 0, "reload");
    ASSERT_EQ_INT(tui_tree_show_matches(&t, m), 0, "and again");
    ASSERT_EQ_INT(tui_tree_show_matches(&t, NULL), 0, "clear");
    ASSERT_EQ_INT(t.list.count, 4, "A closed, B open: A,B,B1,C");
    tui_tree_free(&t);
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

static int label(void *ctx, tui_canvas c, int row, int selected, tui_style base) {
    const tui_tree *t = ctx;
    tui_canvas_put(c, 0, 0, t->rows[row].id, base);
    (void)selected;
    return (int)strlen(t->rows[row].id);
}

static void test_render(void) {
    tui_tree t;
    load(&t);
    tui_tree_set_open(&t, 0, 1);
    tui_tree_set_open(&t, 1, 1);
    tui_screen *scr = tui_screen_new(7, 20);
    tui_buf buf;
    tui_buf_init(&buf);
    PtySession *s = pty_open(20, 7);
    ASSERT(scr && s, "open");
    tui_tree_render(&t, tui_canvas_of(scr), TUI_STYLE_DEFAULT, TUI_STYLE_SELECTION, label, &t);
    tui_screen_render(scr, &buf);
    pty_feed(s, buf.data, buf.len);
    char row[64];
    static const char *const want[] = { "- A", "-   A1", "      A1a", "      A1b", "    A2", "+ B", "  C" };
    for (int r = 0; r < 7; r++)
        ASSERT_EQ_STR(pty_row_text(s, r, row, sizeof row), want[r], "row");
    tui_tree_show_counts(&t, 1);
    tui_tree_set_open(&t, 1, 0);
    tui_buf_reset(&buf);
    tui_tree_render(&t, tui_canvas_of(scr), TUI_STYLE_DEFAULT, TUI_STYLE_SELECTION, label, &t);
    tui_screen_render(scr, &buf);
    pty_feed(s, buf.data, buf.len);
    ASSERT_EQ_STR(pty_row_text(s, 1, row, sizeof row), "+   A1 (2)", "count on a closed row");
    ASSERT_EQ_STR(pty_row_text(s, 3, row, sizeof row), "+ B (1)", "count on B");
    ASSERT_EQ_STR(pty_row_text(s, 0, row, sizeof row), "- A", "no count on an open row");
    /* No label callback, a negative width, and a canvas narrower than the indent. */
    tui_tree_render(&t, tui_canvas_sub(tui_canvas_of(scr), tui_rect_make(0, 0, 3, 3)),
                    TUI_STYLE_DEFAULT, TUI_STYLE_SELECTION, NULL, NULL);
    tui_tree_select_row(&t, 2);
    tui_tree_render(&t, tui_canvas_sub(tui_canvas_of(scr), tui_rect_make(0, 0, 6, 2)),
                    TUI_STYLE_DEFAULT, TUI_STYLE_SELECTION, NULL, NULL);
    tui_tree_render(NULL, tui_canvas_of(scr), TUI_STYLE_DEFAULT, TUI_STYLE_SELECTION, NULL, NULL);
    tui_tree_render(&t, tui_canvas_of(scr), TUI_STYLE_DEFAULT, TUI_STYLE_SELECTION, NULL, NULL);
    pty_close(s);
    tui_buf_free(&buf);
    tui_screen_free(scr);
    tui_tree_free(&t);
}

static int negative_label(void *ctx, tui_canvas c, int row, int selected, tui_style base) {
    (void)ctx; (void)c; (void)row; (void)selected; (void)base;
    return -5;
}

static void test_negative_width(void) {
    tui_tree t;
    load(&t);
    tui_tree_show_counts(&t, 1);
    tui_screen *scr = tui_screen_new(3, 20);
    tui_tree_render(&t, tui_canvas_of(scr), TUI_STYLE_DEFAULT, TUI_STYLE_SELECTION, negative_label, NULL);
    tui_screen_free(scr);
    tui_tree_free(&t);
}

/* ── Failures ────────────────────────────────────────────────────────── */

static void test_memory_failures(void) {
    tui_tree t;
    tui_tree_init(&t);
    fault_arm(F_MALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_tree_set_rows(&t, g_rows, N_ROWS), -1, "set_rows: no memory");
    ASSERT_EQ_INT(errno, ENOMEM, "ENOMEM");
    fault_reset();
    ASSERT_EQ_INT(tui_tree_set_rows(&t, g_rows, N_ROWS), 0, "set_rows");

    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_tree_set_open(&t, 0, 1), -1, "set_open: no memory");
    fault_reset();
    ASSERT_EQ_INT(tui_tree_is_open(&t, 0), 0, "stays closed");

    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(press(&t, key_ev(TUI_KEY_RIGHT)), 1, "Right without memory is used");
    fault_reset();
    ASSERT_EQ_INT(tui_tree_is_open(&t, 0), 0, "and does nothing");

    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_tree_open_all(&t), -1, "open_all: no memory");
    fault_reset();

    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_tree_select_row(&t, 3), -1, "select_row: no memory");
    fault_reset();

    /* Growth: the open set doubles past its first 64 bytes. */
    char names[40][8];
    tui_treerow many[80];
    for (int i = 0; i < 40; i++) {
        snprintf(names[i], sizeof names[i], "node%d", i);
        many[2 * i] = (tui_treerow){ names[i], 0, 1 };
        many[2 * i + 1] = (tui_treerow){ "leaf", 1, 0 };
    }
    ASSERT_EQ_INT(tui_tree_set_rows(&t, many, 80), 0, "many rows");
    ASSERT_EQ_INT(tui_tree_open_all(&t), 0, "open_all grows the set");
    ASSERT_EQ_INT(t.list.count, 80, "all shown");
    tui_tree_close_all(&t);

    ASSERT_EQ_INT(tui_tree_set_open(&t, 0, 1), 0, "one branch open before the filter");
    unsigned char m[80] = { 0 };
    m[1] = 1;
    fault_arm(F_REALLOC, 0, 1, ENOMEM);
    ASSERT_EQ_INT(tui_tree_show_matches(&t, m), -1, "show_matches: no memory saving");
    fault_reset();
    ASSERT_EQ_INT(tui_tree_show_matches(&t, m), 0, "show_matches");
    tui_tree_free(&t);

    /* Opening the ancestors fails after the copy succeeded. */
    load(&t);
    memset(m, 0, sizeof m);
    m[3] = 1;
    fault_arm(F_REALLOC, 0, 1, ENOMEM);  /* the copy needs none (empty set), the first add fails */
    ASSERT_EQ_INT(tui_tree_show_matches(&t, m), -1, "ancestors: no memory");
    fault_reset();
    tui_tree_free(&t);
}

static const TestCase tests[] = {
    TEST_CASE(test_closed_on_opening),
    TEST_CASE(test_right_and_left),
    TEST_CASE(test_last_item_and_empty),
    TEST_CASE(test_enter_and_movement),
    TEST_CASE(test_open_all_close_all),
    TEST_CASE(test_select_row_and_set_open),
    TEST_CASE(test_reload_keeps_state),
    TEST_CASE(test_invalid_rows),
    TEST_CASE(test_filter_opens_and_restores),
    TEST_CASE(test_render),
    TEST_CASE(test_negative_width),
    TEST_CASE(test_memory_failures),
};

int main(int argc, char **argv) {
    return RUN_SUITE("tree", tests, 100, 60);
}
