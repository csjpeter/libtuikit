/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-16 */
/* Capability tests: history, tui_lineedit, tui_textedit, tui_textview, tui_listview. */

#include "cap_common.h"

static void cap_history(void) {
    tui_history h;
    tui_history_init(&h, 3);
    CHECK_INT(tui_history_count(&h), 0, "empty");
    CHECK_INT(tui_history_add(&h, "one"), 0, "add");
    tui_history_add(&h, "one");
    tui_history_add(&h, "");
    CHECK_INT(tui_history_count(&h), 1, "empty and repeated lines are not added");
    tui_history_add(&h, "two");
    tui_history_add(&h, "three");
    tui_history_add(&h, "four");
    CHECK_INT(tui_history_count(&h), 3, "capped at max");
    CHECK_STR(tui_history_get(&h, 0), "two", "oldest dropped");
    CHECK_STR(tui_history_get(&h, 2), "four", "newest last");
    CHECK(tui_history_get(&h, 3) == NULL, "out of range");
    tui_history_free(&h);
}

static void cap_lineedit(void) {
    char buf[32];
    tui_lineedit le;
    tui_lineedit_init(&le, buf, sizeof buf, "hello");
    CHECK_STR(buf, "hello", "initial text");
    CHECK_INT(le.cur, 5, "cursor at the end");
    CHECK_INT(tui_lineedit_cursor_col(&le), 5, "cursor column");

    tui_event ev = cap_ev("!");
    CHECK_INT(tui_lineedit_handle(&le, &ev), TUI_EDIT_CHANGED, "char changes");
    CHECK_STR(buf, "hello!", "char inserted");
    ev = cap_ev("\x7f");
    CHECK_INT(tui_lineedit_handle(&le, &ev), TUI_EDIT_CHANGED, "backspace");
    CHECK_STR(buf, "hello", "char deleted");
    ev = cap_ev("\x01");
    tui_lineedit_handle(&le, &ev);
    CHECK_INT(tui_lineedit_cursor_col(&le), 0, "Ctrl-A goes home");
    ev = cap_ev("\x0b");
    tui_lineedit_handle(&le, &ev);
    CHECK_STR(buf, "", "Ctrl-K kills to the end");
    ev = cap_ev("\x19");
    tui_lineedit_handle(&le, &ev);
    CHECK_STR(buf, "hello", "Ctrl-Y yanks it back");

    CHECK_INT(tui_lineedit_insert(&le, "\xc3\xa9\tx"), 1, "insert");
    CHECK_STR(buf, "hello\xc3\xa9 x", "tab becomes a space");
    tui_lineedit_replace(&le, 0, 5, "J");
    CHECK_STR(buf, "J\xc3\xa9 x", "replace a range");
    tui_lineedit_set_text(&le, "abc");
    CHECK_STR(buf, "abc", "set_text");
    CHECK_INT(le.cur, 3, "cursor at the end after set_text");

    ev = cap_ev("\r");
    CHECK_INT(tui_lineedit_handle(&le, &ev), TUI_EDIT_ACCEPT, "Enter accepts");
    ev = cap_ev("\x03");
    CHECK_INT(tui_lineedit_handle(&le, &ev), TUI_EDIT_CANCEL, "Ctrl-C cancels");
    tui_lineedit_set_text(&le, "");
    ev = cap_ev("\x04");
    CHECK_INT(tui_lineedit_handle(&le, &ev), TUI_EDIT_EOF, "Ctrl-D on empty is EOF");
    ev = cap_ev("\x1b[11~");
    CHECK_INT(tui_lineedit_handle(&le, &ev), TUI_EDIT_IGNORED, "F1 is ignored");

    tui_history h;
    tui_history_init(&h, 8);
    tui_history_add(&h, "older");
    tui_history_add(&h, "newer");
    le.history = &h;
    ev = cap_ev("\x1b[A");
    tui_lineedit_handle(&le, &ev);
    CHECK_STR(buf, "newer", "Up browses the history");
    tui_lineedit_handle(&le, &ev);
    CHECK_STR(buf, "older", "Up again");
    ev = cap_ev("\x1b[B");
    tui_lineedit_handle(&le, &ev);
    CHECK_STR(buf, "newer", "Down");
    tui_history_free(&h);

    tui_screen *s = tui_screen_new(2, 20);
    tui_lineedit_set_text(&le, "text");
    int col = tui_lineedit_render(&le, tui_canvas_of(s), "> ", TUI_STYLE_DEFAULT);
    CHECK_INT(col, 6, "render returns the cursor column");
    char *t = cap_screen_text(s, NULL);
    CHECK_HAS(t, ">", "prompt drawn");
    CHECK_HAS(t, "text", "text drawn");
    free(t);
    tui_screen_free(s);
    tui_lineedit_free(&le);
}

static void cap_textedit(void) {
    char buf[128];
    tui_textedit te;
    tui_textedit_init(&te, buf, sizeof buf, "one\ntwo");
    CHECK_INT(tui_textedit_lines(&te), 2, "two lines");
    int row, col;
    tui_textedit_cursor(&te, &row, &col);
    CHECK_INT(row, 0, "initial cursor row");
    CHECK_INT(col, 0, "initial cursor col");

    tui_event ev = cap_ev("\x1b[B");
    CHECK_INT(tui_textedit_handle(&te, &ev), 1, "Down is used");
    tui_textedit_cursor(&te, &row, &col);
    CHECK_INT(row, 1, "cursor on line 2");
    CHECK_INT(tui_textedit_insert(&te, "X\r\nY", 4), 3, "CRLF is one byte inserted");
    CHECK_STR(buf, "one\nX\nYtwo", "CRLF becomes a newline");
    CHECK_INT(tui_textedit_lines(&te), 3, "three lines");
    tui_textedit_set_text(&te, "a\tb");
    tui_textedit_cursor(&te, &row, &col);
    CHECK_INT(col, 0, "cursor at the start after set_text");
    ev = cap_ev("\x1b[F");
    tui_textedit_handle(&te, &ev);
    tui_textedit_cursor(&te, &row, &col);
    CHECK_INT(col, 9, "a tab reaches the next multiple of eight");

    tui_screen *s = tui_screen_new(3, 20);
    tui_textedit_set_text(&te, "first\nsecond");
    tui_textedit_render(&te, tui_canvas_of(s), TUI_STYLE_DEFAULT);
    char *t = cap_screen_text(s, NULL);
    CHECK_HAS(t, "first", "first line drawn");
    CHECK_HAS(t, "second", "second line drawn");
    free(t);
    tui_screen_free(s);

    char sum[64];
    CHECK_INT(tui_textedit_summary("first line\nsecond", 40, sum, sizeof sum), 20, "summary columns");
    CHECK_STR(sum, "first line (2 lines)", "summary is the first line and the count");
}

static void cap_textview(void) {
    tui_textview tv;
    tui_textview_init(&tv);
    CHECK_INT(tui_textview_set_text(&tv, "alpha beta gamma delta epsilon zeta eta theta"), 0, "set_text");
    CHECK_INT(tui_textview_wrap(&tv, 12), 4, "wrap returns the lines");
    int n = tui_textview_lines(&tv);
    CHECK(n >= 4, "wrapped to several lines");
    CHECK_INT(tui_textview_top(&tv), 0, "at the top");
    tui_textview_scroll(&tv, 2);
    CHECK_INT(tui_textview_top(&tv), 2, "scroll down");
    tui_textview_scroll(&tv, -5);
    CHECK_INT(tui_textview_top(&tv), 0, "scroll is clamped");
    tui_textview_scroll_to(&tv, 1);
    CHECK_INT(tui_textview_top(&tv), 1, "scroll_to");

    tui_screen *s = tui_screen_new(2, 12);
    tui_textview_render(&tv, tui_canvas_of(s), TUI_STYLE_DEFAULT);
    char *t = cap_screen_text(s, NULL);
    CHECK_HAS(t, "gamma", "second wrapped line is the top");
    free(t);
    CHECK_INT(tui_textview_at_end(&tv), 0, "not at the end");
    tui_textview_scroll_end(&tv);
    CHECK_INT(tui_textview_at_end(&tv), 1, "scroll_end");
    CHECK_INT(tui_textview_append(&tv, " iota", 5), 0, "append");
    CHECK_INT(tui_textview_wrap(&tv, 12), 5, "wrap again");

    tui_event ev = cap_ev("\x1b[H");
    CHECK_INT(tui_textview_handle(&tv, &ev), 1, "Home is used");
    CHECK_INT(tui_textview_top(&tv), 0, "Home goes to the top");
    ev = cap_ev("\x1b[B");
    tui_textview_handle(&tv, &ev);
    CHECK_INT(tui_textview_top(&tv), 1, "Down scrolls");
    ev = cap_ev("q");
    CHECK_INT(tui_textview_handle(&tv, &ev), 0, "other keys are not used");
    tui_screen_free(s);
    tui_textview_free(&tv);
}

static int tree_label(void *ctx, tui_canvas c, int row, int selected, tui_style base) {
    const tui_treerow *rows = ctx;
    (void)selected;
    tui_canvas_put(c, 0, 0, rows[row].id, base);
    return (int)strlen(rows[row].id);
}

static void cap_tree(void) {
    static const tui_treerow rows[] = {
        { "top", 0, 2 }, { "mid", 1, 1 }, { "leaf", 2, 0 }, { "other", 1, 0 }, { "last", 0, 0 } };
    tui_tree t;
    tui_tree_init(&t);
    CHECK_INT(tui_tree_set_rows(&t, rows, 5), 0, "set_rows");
    CHECK_INT(t.list.count, 2, "closed on opening");
    CHECK_INT(tui_tree_selected_row(&t), 0, "first row selected");
    CHECK_STR(tui_tree_marker(&t, 0), "+ ", "closed marker");
    tui_event ev = cap_ev("\x1b[C");
    CHECK_INT(tui_tree_handle(&t, &ev), 1, "Right is used");
    CHECK_INT(tui_tree_is_open(&t, 0), 1, "Right opens");
    CHECK_STR(tui_tree_marker(&t, 0), "- ", "open marker");
    CHECK_INT(tui_tree_row_at(&t, 1), 1, "the child shows");
    ev = cap_ev("\r");
    CHECK_INT(tui_tree_handle(&t, &ev), 0, "Enter is not used");
    CHECK_INT(tui_tree_set_open(&t, 1, 1), 0, "set_open");
    CHECK_INT(tui_tree_open_all(&t), 0, "open_all");
    CHECK_INT(t.list.count, 5, "everything shows");
    tui_tree_close_all(&t);
    CHECK_INT(t.list.count, 2, "close_all");
    CHECK_INT(tui_tree_select_row(&t, 2), 0, "select_row opens the way");
    unsigned char m[5] = { 0, 0, 0, 1, 0 };
    CHECK_INT(tui_tree_show_matches(&t, m), 0, "show_matches");
    CHECK_INT(tui_tree_show_matches(&t, NULL), 0, "filter cleared");
    tui_tree_show_counts(&t, 1);
    tui_screen *s = tui_screen_new(6, 20);
    tui_tree_render(&t, tui_canvas_of(s), TUI_STYLE_DEFAULT, TUI_STYLE_SELECTION, tree_label, (void *)rows);
    char *txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "top", "label drawn");
    free(txt);
    tui_screen_free(s);
    tui_tree_free(&t);
}

static int hv_calls;
static int hv_revert(void *ctx, uint64_t id, char *why, size_t size) {
    (void)ctx;
    hv_calls++;
    if (id == 2) {
        snprintf(why, size, "a later change overtook step 2");
        return -1;
    }
    return 0;
}

static void cap_histview(void) {
    static const tui_histview_row rows[] = {
        { 1, "2026-10-21 10:00", "ann", "set", "T-1", "status", "NEW", "DONE", 0, 1 },
        { 2, "2026-10-21 10:05", "bob", "set", "T-1", "title", "a", "b", 0, 1 },
        { 3, "2026-10-21 10:06", "bob", "send", "T-1", "mail", "", "sent", 0, 0 } };
    tui_histview h;
    CHECK_INT(tui_histview_init(&h, TUI_HISTVIEW_OBJECT, hv_revert, NULL), 0, "init");
    CHECK_INT(tui_histview_selected(&h), -1, "no rows, no selection");
    tui_screen *s = tui_screen_new(24, 80);
    tui_histview_render(&h, tui_canvas_of(s));
    char *txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "no recorded changes", "empty state");
    free(txt);
    tui_histview_set_rows(&h, rows, 3);
    CHECK_INT(tui_histview_selected(&h), 0, "first step selected");
    tui_histview_set_failed(&h, 1);
    tui_histview_render(&h, tui_canvas_of(s));
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "could not be read", "error state");
    free(txt);
    tui_histview_set_failed(&h, 0);
    tui_event ev = cap_ev("\r");
    CHECK_INT(tui_histview_handle(&h, &ev), TUI_HISTVIEW_OPEN, "Enter opens the question");
    CHECK_INT(tui_histview_modal(&h), 1, "modal");
    ev = cap_ev("n");
    tui_histview_handle(&h, &ev);
    CHECK_INT(hv_calls, 0, "No calls nothing");
    ev = cap_ev("j");
    tui_histview_handle(&h, &ev);
    ev = cap_ev("\r");
    tui_histview_handle(&h, &ev);
    ev = cap_ev("y");
    CHECK_INT(tui_histview_handle(&h, &ev), TUI_HISTVIEW_OPEN, "refused: stay");
    CHECK_STR(tui_histview_notice(&h), "a later change overtook step 2", "the server's words");
    tui_histview_render(&h, tui_canvas_of(s));
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "a later change overtook step 2", "refusal drawn");
    free(txt);
    ev = cap_ev("k");
    tui_histview_handle(&h, &ev);
    ev = cap_ev(":");
    tui_histview_handle(&h, &ev);
    ev = cap_ev("\r");
    tui_histview_handle(&h, &ev);
    ev = cap_ev("y");
    CHECK_INT(tui_histview_handle(&h, &ev), TUI_HISTVIEW_REVERTED, "reverted");
    ev = cap_ev("G");
    tui_histview_handle(&h, &ev);
    ev = cap_ev("\r");
    tui_histview_handle(&h, &ev);
    CHECK_INT(tui_histview_modal(&h), 0, "not revertable: no dialog");
    ev = cap_ev("q");
    CHECK_INT(tui_histview_handle(&h, &ev), TUI_HISTVIEW_BACK, "q goes back");
    tui_histview_free(&h);
    tui_screen_free(s);
}

struct draw_ctx { tui_screen *s; };
static void draw_row(void *ctx, tui_canvas row, int index, int selected) {
    (void)ctx;
    char t[16];
    snprintf(t, sizeof t, "%sitem%d", selected ? ">" : " ", index);
    tui_canvas_put(row, 0, 0, t, TUI_STYLE_DEFAULT);
}

static void cap_listview(void) {
    tui_listview lv;
    tui_listview_init(&lv);
    CHECK_INT(lv.selected, -1, "no selection when empty");
    tui_listview_set_count(&lv, 20);
    CHECK_INT(lv.selected, 0, "first item selected");
    tui_listview_set_viewport(&lv, 5);
    CHECK_INT(lv.rows_visible, 5, "viewport");
    tui_listview_move_down(&lv);
    CHECK_INT(lv.selected, 1, "down");
    tui_listview_move_up(&lv);
    CHECK_INT(lv.selected, 0, "up");
    tui_listview_page_down(&lv);
    CHECK_INT(lv.selected, 5, "page down");
    tui_listview_page_up(&lv);
    CHECK_INT(lv.selected, 0, "page up");
    tui_listview_end(&lv);
    CHECK_INT(lv.selected, 19, "end");
    CHECK_INT(tui_listview_is_visible(&lv, 19), 1, "end is visible");
    CHECK_INT(tui_listview_is_visible(&lv, 0), 0, "start scrolled out");
    tui_listview_home(&lv);
    CHECK_INT(lv.selected, 0, "home");
    tui_listview_select(&lv, 12);
    CHECK_INT(lv.selected, 12, "select");
    CHECK_INT(tui_listview_is_visible(&lv, 12), 1, "selection brought into view");
    tui_listview_select(&lv, 99);
    CHECK_INT(lv.selected, 19, "select clamps");
    lv.scroll_top = 0;
    tui_listview_reveal_selected(&lv);
    CHECK_INT(tui_listview_is_visible(&lv, 19), 1, "reveal_selected");

    tui_event ev = cap_ev("\x1b[H");
    CHECK_INT(tui_listview_handle(&lv, &ev), 1, "Home handled");
    CHECK_INT(lv.selected, 0, "Home key");
    ev = cap_ev("x");
    CHECK_INT(tui_listview_handle(&lv, &ev), 0, "other key not handled");

    tui_screen *s = tui_screen_new(5, 20);
    tui_listview_render(&lv, tui_canvas_of(s), TUI_STYLE_DEFAULT, draw_row, NULL);
    char *t = cap_screen_text(s, NULL);
    CHECK_HAS(t, ">item0", "selected row");
    CHECK_HAS(t, "item4", "last visible row");
    free(t);
    tui_screen_free(s);
}

int main(void) {
    cap_history();
    cap_lineedit();
    cap_textedit();
    cap_textview();
    cap_listview();
    cap_tree();
    cap_histview();
    return cap_finish("cap-edit");
}
