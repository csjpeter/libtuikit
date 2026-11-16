/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-16 */
/* Capability tests: tui_table, tui_statusline, tui_dialog, tui_fieldset, the key table. */

#include "cap_common.h"
#include "fc_tree.h"

#include <poll.h>

/* ---- table ---------------------------------------------------------- */

static void t_row(void *ctx, int i, tui_table_row *r) {
    (void)ctx;
    if (i == 2) {
        tui_table_row_kind(r, TUI_ROW_SECTION);
        tui_table_row_text(r, "Section");
    } else if (i == 4) {
        tui_table_row_style(r, TUI_STYLE_ERROR);
        tui_table_row_mark(r, "*", TUI_STYLE_OK);
    }
}
static void t_cell(void *ctx, int i, int col, tui_table_cell *c) {
    (void)ctx;
    char t[32];
    if (col == 0) {
        tui_table_cell_prefix(c, "+ ", TUI_STYLE_DEFAULT);
        snprintf(t, sizeof t, "name%d", i);
        tui_table_cell_put(c, t, TUI_STYLE_DEFAULT);
    } else {
        snprintf(t, sizeof t, "%d", i * 10);
        tui_table_cell_put(c, t, TUI_STYLE_ACCENT);
    }
}
static void t_span_row(void *ctx, int i, tui_table_row *r) {
    (void)ctx;
    if (i == 1) tui_table_row_span(r, 0, 2);
}
static void t_span_cell(void *ctx, int i, int col, tui_table_cell *c) {
    (void)ctx; (void)i; (void)col;
    tui_table_cell_put(c, "spanned over name and size", TUI_STYLE_DEFAULT);
}
static uint64_t t_id(void *ctx, int i) { (void)ctx; return 1000u + (uint64_t)i; }

static void t_foot(void *ctx, int row, int col, tui_table_cell *c) {
    (void)ctx; (void)row;
    if (col == 0) tui_table_cell_put(c, "Total", TUI_STYLE_DEFAULT);
    else if (col == 1) tui_table_cell_put(c, "450", TUI_STYLE_DEFAULT);
}

static void cap_table(void) {
    static const tui_table_col cols[] = {
        { "Name", 8, 1, TUI_ALIGN_LEFT, 0, 1, 0 },
        { "Size", 6, 0, TUI_ALIGN_RIGHT, 0, 0, 0 },
        { "Extra", 6, 0, TUI_ALIGN_LEFT, 60, 0, 0 },
    };
    tui_table_model m = { t_row, t_cell, NULL };
    tui_table t;
    tui_table_init(&t, cols, 3, &m);
    t.flags = TUI_TABLE_RULER | TUI_TABLE_MARKS;
    tui_table_set_colors(&t, 256);
    tui_table_set_count(&t, 10);
    tui_table_set_total(&t, 25);
    CHECK_INT(t.total, 25, "total");
    CHECK_INT(tui_table_selected(&t), 0, "first row selected");

    int w[3];
    CHECK_INT(tui_table_widths(&t, 40, w), 2, "a narrow table hides a column");
    CHECK_INT(w[2], 0, "hidden column has width 0");
    CHECK_INT(w[1], 6, "fixed column keeps its width");
    CHECK(w[0] >= 8, "the weighted column takes the rest");

    static const tui_table_col fitc[] = {
        { "Name", 3, 10, TUI_ALIGN_LEFT, 0, 0, TUI_COL_FIT },
        { "Note", 4, 1, TUI_ALIGN_LEFT, 0, 0, 0 },
    };
    tui_table ft;
    tui_table_init(&ft, fitc, 2, &m);
    tui_table_set_count(&ft, 10);
    CHECK_INT(tui_table_widths(&ft, 40, w), 2, "a FIT column is shown");
    CHECK(w[0] >= 3 && w[0] <= 10, "a FIT column stays within its limits");
    tui_table_free(&ft);

    tui_table_select(&t, 2);
    CHECK_INT(tui_table_selected(&t), 3, "a section row is passed over");
    tui_event ev = cap_ev("\x1b[A");
    CHECK_INT(tui_table_handle(&t, &ev), 1, "Up handled");
    CHECK_INT(tui_table_selected(&t), 1, "Up skips the section row");
    ev = cap_ev("j");
    tui_table_handle(&t, &ev);
    CHECK_INT(tui_table_selected(&t), 3, "j moves down past the section");
    ev = cap_ev("G");
    tui_table_handle(&t, &ev);
    CHECK_INT(tui_table_selected(&t), 9, "G goes to the end");
    ev = cap_ev("g");
    tui_table_handle(&t, &ev);
    CHECK_INT(tui_table_selected(&t), 0, "g goes home");

    ev = cap_ev(" ");
    tui_table_handle(&t, &ev);
    CHECK_INT(tui_table_marked(&t, 0), 1, "Space marks");
    CHECK_INT(tui_table_selected(&t), 1, "and moves down");
    CHECK_INT(tui_table_toggle_mark(&t, 0), 0, "toggle");
    CHECK_INT(tui_table_marked(&t, 0), 0, "toggled off");
    CHECK_INT(tui_table_set_marked(&t, 3, 1), 0, "set_marked");
    CHECK_INT(tui_table_set_marked(&t, 2, 1), 0, "section row left alone");
    CHECK_INT(tui_table_marked(&t, 2), 0, "section row never marked");
    CHECK_INT(tui_table_set_marked(&t, 99, 1), -1, "no such row");
    CHECK_INT(errno, EINVAL, "EINVAL");
    CHECK_INT(tui_table_mark_all(&t), 0, "mark_all");
    CHECK_INT(tui_table_marked_count(&t), 9, "all data rows");
    CHECK_INT(tui_table_invert_marks(&t), 0, "invert");
    CHECK_INT(tui_table_marked_count(&t), 0, "inverted to none");
    tui_table_set_marked(&t, 1, 1);
    tui_table_set_marked(&t, 5, 1);
    CHECK_INT(tui_table_next_marked(&t, -1), 1, "first mark");
    CHECK_INT(tui_table_next_marked(&t, 1), 5, "next mark");
    CHECK_INT(tui_table_next_marked(&t, 5), -1, "no more");
    tui_table_clear_marks(&t);
    CHECK_INT(tui_table_marked_count(&t), 0, "clear_marks");

    tui_table_set_row_id(&t, t_id, NULL);
    tui_table_set_marked(&t, 7, 1);
    tui_table_set_count(&t, 5);
    CHECK_INT(tui_table_marked_count(&t), 1, "a mark of a row not held is kept by its id");
    tui_table_set_count(&t, 10);
    CHECK_INT(tui_table_marked(&t, 7), 1, "and is back when the row is");

    tui_screen *s = tui_screen_new(8, 40);
    tui_canvas c = tui_canvas_of(s);
    tui_canvas_clear(c, TUI_STYLE_DEFAULT);
    tui_table_select(&t, 1);
    tui_table_render(&t, c, TUI_STYLE_DEFAULT);
    CHECK_INT(tui_table_first_visible(&t), 0, "first visible");
    CHECK(tui_table_last_visible(&t) >= 4, "last visible");
    char *raw = NULL;
    char *txt = cap_screen_text(s, &raw);
    CHECK_HAS(txt, "Name", "header");
    CHECK_HAS(txt, "\xe2\x96\xb2", "sort marker");
    CHECK_HAS(txt, "\xe2\x95\x90", "ruler");
    CHECK_HAS(txt, "name1", "row");
    CHECK_HAS(txt, "Section", "section text");
    CHECK_HAS(txt, ">", "selection marker");
    CHECK_HAS(raw, "48;5;17", "256-colour selection background");
    free(txt); free(raw);

    tui_canvas_clear(c, TUI_STYLE_DEFAULT);
    tui_table_render_header(&t, c);
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "Size", "header only");
    CHECK(strstr(txt, "name1") == NULL, "no rows in header-only render");
    free(txt);

    tui_canvas_clear(c, TUI_STYLE_DEFAULT);
    CHECK_INT(tui_table_render_row(&t, c, 4), 1,
              "render_row");
    txt = cap_screen_text(s, &raw);
    CHECK_HAS(txt, "name4", "that row drawn");
    CHECK_HAS(txt, "*", "mark glyph of the row");
    CHECK_HAS(raw, "31", "row style is red");
    free(txt); free(raw);

    char line[128];
    CHECK(tui_table_format_header(&t, 40, line, sizeof line) > 0, "format_header");
    CHECK_HAS(line, "Name", "header text");
    CHECK(tui_table_format_row(&t, 40, 1, line, sizeof line) > 0, "format_row");
    CHECK_HAS(line, "name1", "row text");
    CHECK_HAS(line, "10", "cell text");

    CHECK_INT(tui_table_set_footer(&t, 1, t_foot, NULL), 0, "set_footer");
    tui_canvas_clear(c, TUI_STYLE_DEFAULT);
    tui_table_render(&t, c, TUI_STYLE_DEFAULT);
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "Total", "footer under the body");
    CHECK_HAS(txt, "450", "footer cell under its column");
    free(txt);
    tui_canvas_clear(c, TUI_STYLE_DEFAULT);
    tui_table_render_footer(&t, c);
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "Total", "render_footer");
    CHECK(strstr(txt, "name1") == NULL, "footer alone has no rows");
    free(txt);
    CHECK(tui_table_format_footer(&t, 40, 0, line, sizeof line) > 0, "format_footer");
    CHECK_HAS(line, "450", "footer text");
    tui_table_set_footer(&t, 0, NULL, NULL);

    tui_table_set_message(&t, "nothing here", TUI_STYLE_HINT);
    tui_canvas_clear(c, TUI_STYLE_DEFAULT);
    tui_table_render(&t, c, TUI_STYLE_DEFAULT);
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "nothing here", "message replaces the rows");
    CHECK(strstr(txt, "name1") == NULL, "no rows under a message");
    free(txt);
    tui_screen_free(s);
    tui_table_free(&t);

    /* A cell that spans two columns. */
    tui_table_model sm = { t_span_row, t_span_cell, NULL };
    tui_table st;
    tui_table_init(&st, cols, 3, &sm);
    tui_table_set_count(&st, 3);
    tui_screen *ss = tui_screen_new(4, 40);
    tui_canvas sc = tui_canvas_of(ss);
    tui_canvas_clear(sc, TUI_STYLE_DEFAULT);
    tui_table_render(&st, sc, TUI_STYLE_DEFAULT);
    txt = cap_screen_text(ss, NULL);
    CHECK_HAS(txt, "spanned over name and size", "a row spans two columns as one text");
    free(txt);
    tui_screen_free(ss);
    tui_table_free(&st);
}

/* ---- tab row -------------------------------------------------------- */

static void cap_tabrow(void) {
    static const char *const labels[] = { "Overview", "Tests", "Log", "Config", "Hosts",
                                          "Queue", "Metrics", "Alerts", "Help" };
    tui_tabrow tr;
    tui_tabrow_init(&tr);
    CHECK_INT(tui_tabrow_set(&tr, labels, 9, 0), 0, "tabrow set");
    CHECK_INT(tui_tabrow_count(&tr), 9, "tabrow count");
    char out[256];
    CHECK(tui_tabrow_format(&tr, 80, out, sizeof out) > 0, "tabrow format");
    CHECK_INT(tui_str_width(out) <= 80, 1, "the row fits 80 columns");
    CHECK_HAS(out, " 1 Overview ", "each tab shows its digit and its label");
    tui_tabrow_set_active(&tr, 8);
    CHECK_INT(tui_tabrow_active(&tr), 8, "tabrow set_active");
    tui_tabrow_format(&tr, 80, out, sizeof out);
    CHECK_HAS(out, " 9 Help", "the active tab is visible at 80 columns, with its digit");
    tui_tabrow_format(&tr, 40, out, sizeof out);
    CHECK_HAS(out, " 9 Help", "and at 40, where the row scrolls");
    CHECK_HAS(out, "\xE2\x80\xB9", "with a mark for the tabs hidden before it");

    tui_screen *scr = tui_screen_new(3, 80);
    tui_tabrow_render(&tr, tui_canvas_of(scr));
    char *txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, " 9 Help", "tabrow render");
    free(txt);
    tui_tabrow_set_numbers(&tr, 0);
    tui_tabrow_format(&tr, 80, out, sizeof out);
    CHECK_HAS(out, " Overview   Tests ", "numbers off: the labels alone");
    CHECK(strstr(out, " 1 ") == NULL, "numbers off: no digit");
    tui_tabrow_format(&tr, 40, out, sizeof out);
    CHECK_HAS(out, " Help", "numbers off: the active tab is visible at 40 columns");
    scr = tui_screen_new(3, 80);
    tui_tabrow_render(&tr, tui_canvas_of(scr));
    txt = cap_screen_text(scr, NULL);
    CHECK(strstr(txt, " 9 Help") == NULL, "numbers off: render draws no digit");
    free(txt);
    tui_screen_free(scr);
    tui_tabrow_set_numbers(&tr, 1);
    scr = tui_screen_new(3, 80);
    tui_tabrow_render(&tr, tui_canvas_of(scr));
    txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, " 9 Help", "numbers on again: render draws the digit");
    free(txt);
    tui_screen_free(scr);

    tui_event ev = cap_ev("\t");
    CHECK_INT(tui_tabrow_handle(&tr, &ev), TUI_TABROW_CHANGED, "Tab cycles");
    CHECK_INT(tui_tabrow_active(&tr), 0, "and wraps");
    ev = cap_ev("3");
    CHECK_INT(tui_tabrow_handle(&tr, &ev), TUI_TABROW_CHANGED, "a digit jumps");
    CHECK_INT(tui_tabrow_active(&tr), 2, "to that tab");
}

/* ---- main menu ------------------------------------------------------ */

static void cap_menu(void) {
    static const tui_menu_entry e[] = {
        { "Tickets", 12, 3, "All the tickets of the project." },
        { "Agents",   2, 0, "The agents' tmux sessions." },
        { "Quiet",   -1, 0, NULL },
    };
    tui_menu m;
    tui_menu_init(&m);
    CHECK_INT(tui_menu_set(&m, e, 3, 0), 0, "menu set");
    CHECK_INT(tui_menu_count(&m), 3, "menu count");
    char out[1024];
    CHECK(tui_menu_format(&m, 12, 60, out, sizeof out) > 0, "menu format");
    CHECK_HAS(out, "> 1  Tickets", "the cursor row");
    CHECK_HAS(out, "12 (3)", "the count with its attention");
    CHECK_HAS(out, "All the tickets", "the description of the entry under the cursor");

    tui_screen *scr = tui_screen_new(12, 60);
    tui_menu_render(&m, tui_canvas_of(scr));
    char *txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, "Agents", "menu render");
    CHECK_HAS(txt, "> 1  Tickets", "and the cursor row");
    free(txt);
    tui_screen_free(scr);

    tui_event ev = cap_ev("2");
    CHECK_INT(tui_menu_handle(&m, &ev), TUI_MENU_ACTIVATED, "a digit activates");
    CHECK_INT(tui_menu_selected(&m), 1, "and moves the cursor");
    tui_menu_select(&m, 0);
    ev = cap_ev("j");
    CHECK_INT(tui_menu_handle(&m, &ev), TUI_MENU_MOVED, "j moves down");
    CHECK_INT(tui_menu_selected(&m), 1, "to the next entry");

    /* ten entries: the tenth has the hot key 0 */
    static const tui_menu_entry t[] = {
        { "A", 1, 0, NULL }, { "B", 1, 0, NULL }, { "C", 1, 0, NULL }, { "D", 1, 0, NULL },
        { "E", 1, 0, NULL }, { "F", 1, 0, NULL }, { "G", 1, 0, NULL }, { "H", 1, 0, NULL },
        { "I", 1, 0, NULL }, { "J", 1, 0, "The tenth view." },
    };
    CHECK_INT(tui_menu_set(&m, t, 10, 0), 0, "a menu of ten");
    ev = cap_ev("0");
    CHECK_INT(tui_menu_handle(&m, &ev), TUI_MENU_ACTIVATED, "0 activates the tenth");
    CHECK_INT(tui_menu_selected(&m), 9, "and moves the cursor to it");
    CHECK(tui_menu_format(&m, 20, 60, out, sizeof out) > 0, "ten format");
    CHECK_HAS(out, "> 0  J", "the tenth row has the key 0");
}

/* ---- header --------------------------------------------------------- */

static void cap_header(void) {
    tui_header h;
    tui_header_init(&h);
    tui_header_set_left(&h, "Inbox (42)");
    tui_header_set_right(&h, "email-tui");
    char out[256];
    CHECK(tui_header_format(&h, 60, out, sizeof out) > 0, "header format");
    CHECK_HAS(out, "Inbox (42)", "the view");
    CHECK_HAS(out, "email-tui", "the program name");

    tui_header_set_notice(&h, "Saved", TUI_STYLE_OK);
    CHECK_STR(tui_header_notice(&h), "Saved", "notice");
    tui_header_format(&h, 60, out, sizeof out);
    CHECK_HAS(out, "Inbox (42)  Saved", "the notice follows the view");
    tui_event ev = cap_ev("x");
    CHECK_INT(tui_header_handle(&h, &ev), TUI_HEADER_CHANGED, "a key clears a plain notice");
    CHECK_STR(tui_header_notice(&h), "", "notice gone");

    tui_header_set_warning(&h, "Sync failed");
    CHECK_INT(tui_header_warning_active(&h), 1, "warning");
    CHECK_INT(tui_header_handle(&h, &ev), TUI_HEADER_IGNORED, "a key leaves a warning");
    tui_header_set_notice(&h, "Saved", TUI_STYLE_OK);
    CHECK_STR(tui_header_notice(&h), "Sync failed", "the warning wins");
    tui_screen *scr = tui_screen_new(3, 60);
    tui_header_render(&h, tui_canvas_of(scr));
    char *txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, "Sync failed", "header render");
    free(txt);
    tui_screen_free(scr);
    tui_header_clear_warning(&h);
    CHECK_INT(tui_header_warning_active(&h), 0, "warning cleared");
    tui_header_format(&h, 60, out, sizeof out);
    CHECK(strstr(out, "Sync failed") == NULL, "and gone from the bar");
}

/* ---- statusline ----------------------------------------------------- */

static void cap_statusline(void) {
    static const tui_statusline_hint hints[] = {
        { "Enter", "open", 3 }, { "/", "search", 2 }, { "q", "quit", 1 },
    };
    tui_statusline sl;
    tui_statusline_init(&sl);
    CHECK_INT(tui_statusline_set_hints(&sl, hints, 3), 0, "set_hints");
    tui_statusline_set_help(&sl, "?", "help");
    CHECK_INT(tui_statusline_hint_count(&sl), 3, "hint count");
    CHECK_STR(tui_statusline_hint_at(&sl, 1)->key, "/", "hint_at");
    CHECK(tui_statusline_hint_at(&sl, 3) == NULL, "hint_at out of range");
    CHECK_INT(tui_statusline_hints_fit(&sl, 80), 3, "all hints fit");
    CHECK(tui_statusline_hints_fit(&sl, 14) < 3, "fewer fit on a narrow line");

    char out[128];
    CHECK(tui_statusline_format(&sl, 80, out, sizeof out) > 0, "format");
    CHECK_HAS(out, "Enter open", "hint in the line");
    CHECK_HAS(out, "? help", "help hint kept");

    tui_statusline_set_message(&sl, "Saved", TUI_STYLE_OK);
    CHECK_STR(tui_statusline_message(&sl), "Saved", "message");
    tui_statusline_format(&sl, 80, out, sizeof out);
    CHECK(strstr(out, "Saved") == NULL, "the message is not on the line (it goes to the header notice)");
    CHECK_HAS(out, "Enter open", "the hints stay");
    tui_event ev = cap_ev("x");
    CHECK_INT(tui_statusline_handle(&sl, &ev), TUI_STATUSLINE_CHANGED, "a key clears the message");
    CHECK_STR(tui_statusline_message(&sl), "", "message gone");
    tui_statusline_set_message(&sl, "again", TUI_STYLE_ERROR);
    tui_statusline_clear_message(&sl);
    CHECK_STR(tui_statusline_message(&sl), "", "clear_message");

    tui_statusline_set_wait(&sl, "Syncing", 2, 7);
    tui_statusline_format(&sl, 80, out, sizeof out);
    CHECK(strstr(out, "Syncing") == NULL, "the wait text is not on the line either");
    CHECK_HAS(out, "Enter open", "the hints stay");
    tui_statusline_clear_wait(&sl);

    tui_statusline_set_position(&sl, "page 2/7");
    tui_statusline_format(&sl, 80, out, sizeof out);
    CHECK_HAS(out, "page 2/7", "position");
    tui_statusline_set_position_index(&sl, 3, 12);
    tui_statusline_format(&sl, 80, out, sizeof out);
    CHECK_HAS(out, "[3/12]", "position index");

    tui_statusline_begin_prompt(&sl, "/", "ab");
    CHECK_INT(tui_statusline_prompt_active(&sl), 1, "prompt open");
    ev = cap_ev("c");
    CHECK_INT(tui_statusline_handle(&sl, &ev), TUI_STATUSLINE_CHANGED, "typing in the prompt");
    CHECK_STR(tui_statusline_prompt_text(&sl), "abc", "prompt text");
    ev = cap_ev("\r");
    CHECK_INT(tui_statusline_handle(&sl, &ev), TUI_STATUSLINE_ACCEPT, "Enter accepts");
    CHECK_INT(tui_statusline_prompt_active(&sl), 0, "prompt closed");
    CHECK_STR(tui_statusline_prompt_text(&sl), "abc", "text readable after accept");
    tui_statusline_begin_prompt(&sl, "/", NULL);
    ev = cap_ev("\x1b");
    CHECK_INT(tui_statusline_handle(&sl, &ev), TUI_STATUSLINE_CANCEL, "Esc cancels");
    ev = cap_ev("z");
    CHECK_INT(tui_statusline_handle(&sl, &ev), TUI_STATUSLINE_IGNORED, "other keys ignored");

    tui_screen *s = tui_screen_new(3, 40);
    tui_statusline_render(&sl, tui_canvas_of(s));
    char *raw = NULL;
    char *txt = cap_screen_text(s, &raw);
    CHECK_HAS(txt, "Enter", "rendered on the last row");
    CHECK_HAS(raw, "\x1b[3;1H", "at row 3");
    CHECK_HAS(raw, ";7m", "reverse video bar");
    free(txt); free(raw);
    tui_screen_free(s);
    tui_statusline_free(&sl);
}

/* ---- dialog --------------------------------------------------------- */

static void cap_dialog(void) {
    tui_dialog d;
    tui_event ev;

    CHECK_INT(tui_dialog_message(&d, "Note", "Something happened."), 0, "message");
    CHECK_INT(d.kind, TUI_DIALOG_MESSAGE, "kind");
    tui_screen *s = tui_screen_new(10, 40);
    tui_dialog_render(&d, tui_canvas_of(s));
    char *txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "Note", "title drawn");
    CHECK_HAS(txt, "happened.", "text drawn (wrapped)");
    CHECK_HAS(txt, "OK", "OK button");
    free(txt);
    ev = cap_ev("\r");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OK, "Enter closes a message");
    tui_dialog_free(&d);

    tui_dialog_confirm(&d, "Q", "Sure?", 1);
    ev = cap_ev("n");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OK, "n answers");
    CHECK_INT(d.answer, 0, "no");
    tui_dialog_free(&d);
    tui_dialog_confirm(&d, "Q", "Sure?", 0);
    ev = cap_ev("\x1b[C");
    tui_dialog_handle(&d, &ev);
    ev = cap_ev("\r");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OK, "Enter takes the focused button");
    CHECK_INT(d.answer, 1, "Right moved to Yes");
    tui_dialog_free(&d);
    tui_dialog_confirm(&d, "Q", "Sure?", 1);
    ev = cap_ev("\x1b");
    ev = cap_ev("\x03");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_CANCELLED, "Ctrl-C cancels");
    tui_dialog_free(&d);

    char buf[16];
    tui_dialog_prompt(&d, "Name", "Enter:", buf, sizeof buf, "ab");
    CAP_TYPE(tui_dialog_handle, &d, "cd");
    tui_dialog_render(&d, tui_canvas_of(s));
    ev = cap_ev("\r");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OK, "prompt accepted");
    CHECK_STR(buf, "abcd", "prompt text in the caller's buffer");
    tui_dialog_free(&d);

    char pw[16];
    CHECK_INT(tui_dialog_password(&d, "Password", "Current:", pw, sizeof pw), 0, "password");
    CHECK_INT(d.kind, TUI_DIALOG_PASSWORD, "password kind");
    CAP_TYPE(tui_dialog_handle, &d, "hunter2");
    tui_dialog_render(&d, tui_canvas_of(s));
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "*******", "one star per character");
    CHECK(strstr(txt, "hunter2") == NULL, "the password is never drawn");
    free(txt);
    ev = cap_ev("\r");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OK, "password accepted");
    CHECK_STR(pw, "hunter2", "password in the caller's buffer");
    tui_dialog_free(&d);

    const char *items[] = { "red", "green", "blue" };
    tui_dialog_menu(&d, "Colour", items, 3, 1);
    tui_dialog_render(&d, tui_canvas_of(s));
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "green", "menu item drawn");
    free(txt);
    ev = cap_ev("\x1b[B");
    tui_dialog_handle(&d, &ev);
    ev = cap_ev("\r");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OK, "menu choice");
    CHECK_INT(d.answer, 2, "Down then Enter chose blue");
    tui_dialog_free(&d);

    unsigned char init[3] = { 0, 1, 0 };
    tui_dialog_checklist(&d, "Pick", items, 3, init, 0);
    ev = cap_ev(" ");
    tui_dialog_handle(&d, &ev);
    CHECK_INT(tui_dialog_is_checked(&d, 0), 1, "Space ticks");
    CHECK_INT(tui_dialog_is_checked(&d, 1), 1, "initial tick kept");
    CHECK_INT(tui_dialog_is_checked(&d, 2), 0, "untouched");
    ev = cap_ev("\r");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OK, "checklist confirmed");
    CHECK_INT(init[0], 0, "the caller's set is a copy");
    tui_dialog_free(&d);

    const char *pairs[] = { "q", "quit", "?", "help" };
    tui_dialog_help(&d, "Keys", pairs, 2);
    tui_dialog_render(&d, tui_canvas_of(s));
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "quit", "help pair drawn");
    free(txt);
    ev = cap_ev("x");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OPEN, "an ordinary key leaves help open");
    ev = cap_ev("q");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OK, "q closes help");
    ev = cap_ev("\x1b");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OK, "events after closing are ignored");
    tui_dialog_free(&d);

    tui_dialog_text(&d, "Long", "one\ntwo\nthree\nfour\nfive\nsix\nseven\neight\nnine\nten\n"
                    "eleven\ntwelve\nthirteen\nfourteen\nfifteen\nsixteen\nseventeen\neighteen\n"
                    "nineteen\ntwenty\ntwentyone\ntwentytwo\ntwentythree\ntwentyfour");
    tui_dialog_render(&d, tui_canvas_of(s));
    ev = cap_ev("j");
    tui_dialog_handle(&d, &ev);
    ev = cap_ev("G");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OPEN, "a text dialog scrolls");
    tui_dialog_render(&d, tui_canvas_of(s));
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "twentyfour", "text dialog scrolled to the end");
    free(txt);
    ev = cap_ev("\r");
    CHECK_INT(tui_dialog_handle(&d, &ev), TUI_DIALOG_OK, "Enter closes a text dialog");
    tui_dialog_free(&d);
    tui_screen_free(s);
}

/* ---- file chooser --------------------------------------------------- */

static void cap_filechooser(void) {
    char root[256];
    snprintf(root, sizeof root, "%s/fc-cap-%d", TUIKIT_TEST_TMPDIR, (int)getpid());
    fc_tree_make(root);
    setenv("TZ", "UTC", 1);

    tui_filechooser fc;
    CHECK_INT(tui_filechooser_open(&fc, NULL, TUI_FC_OPEN, root, NULL), 0, "filechooser open");
    CHECK(fc.notify_fd >= 0, "a descriptor to wait on");
    for (int i = 0; i < 500 && (tui_filechooser_poll(&fc), tui_filechooser_loading(&fc)); i++) {
        struct pollfd pfd = { fc.notify_fd, POLLIN, 0 };
        poll(&pfd, 1, 10);
    }
    CHECK_INT(tui_filechooser_loading(&fc), 0, "the background read ended");
    char want[300];
    snprintf(want, sizeof want, "%s/", root);
    CHECK_STR(tui_filechooser_dir(&fc), want, "the directory shown");

    tui_screen *scr = tui_screen_new(24, 80);
    tui_filechooser_render(&fc, tui_canvas_of(scr));
    char *txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, "Open file", "the title");
    CHECK_HAS(txt, "alpha/", "a directory");
    CHECK_HAS(txt, "b.eml", "a file");
    CHECK_HAS(txt, "2.0K", "its size");
    CHECK_HAS(txt, "2026-01-02", "its date");
    CHECK_HAS(txt, "03:04", "and time");
    CHECK(strstr(txt, ".dots") == NULL, "no hidden name");
    free(txt);

    tui_filechooser_set_hidden(&fc, 1);
    tui_filechooser_render(&fc, tui_canvas_of(scr));
    txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, ".dots/", "set_hidden shows the dot names");
    free(txt);
    tui_filechooser_set_hidden(&fc, 0);

    tui_event ev = cap_ev("\x1b[B");                           /* four times: a.txt */
    for (int i = 0; i < 4; i++) tui_filechooser_handle(&fc, &ev);
    ev = cap_ev("\r");
    CHECK_INT(tui_filechooser_handle(&fc, &ev), TUI_FC_OK, "Enter on a file chooses it");
    snprintf(want, sizeof want, "%s/a.txt", root);
    CHECK_STR(fc.path, want, "the chosen path");
    tui_filechooser_free(&fc);

    CHECK_INT(tui_filechooser_open(&fc, "Pick", TUI_FC_SAVE, root, "*.eml"), 0, "save mode");
    ev = cap_ev("\x1b");
    CHECK_INT(tui_filechooser_handle(&fc, &ev), TUI_FC_CANCELLED, "Esc cancels");
    tui_filechooser_free(&fc);
    CHECK_INT(tui_filechooser_open(&fc, NULL, TUI_FC_DIR, root, NULL), 0, "directory mode");
    for (int i = 0; i < 500 && (tui_filechooser_poll(&fc), tui_filechooser_loading(&fc)); i++) {
        struct pollfd pfd = { fc.notify_fd, POLLIN, 0 };
        poll(&pfd, 1, 10);
    }
    ev = cap_ev("\r");
    CHECK_INT(tui_filechooser_handle(&fc, &ev), TUI_FC_OK, "Enter on the directory shown chooses it");
    CHECK_STR(fc.path, root, "the directory");
    tui_filechooser_free(&fc);
    tui_screen_free(scr);

    char cmd[400];
    snprintf(cmd, sizeof cmd, "rm -rf '%s/fc-cap-%d'", TUIKIT_TEST_TMPDIR, (int)getpid());
    if (system(cmd) != 0) fprintf(stderr, "cannot remove the test tree\n");
}

/* ---- fieldset ------------------------------------------------------- */

static int complete(void *user, int field, const char *token, tui_completion *out, int max) {
    (void)user; (void)field;
    static const char *names[] = { "alice", "albert", "bob" };
    int n = 0;
    for (int i = 0; i < 3 && n < max; i++)
        if (strncmp(names[i], token, strlen(token)) == 0) {
            out[n].text = names[i];
            out[n].label = NULL;
            n++;
        }
    return n;
}

static int validate(void *user, const tui_fieldset *f, char *err, size_t errsz) {
    (void)user;
    if (f->fields[0].edit.len == 0) {
        snprintf(err, errsz, "To is empty");
        return 0;
    }
    return -1;
}

static int cap_setting_validate(void *user, const char *v, char *err, size_t errsz) {
    (void)user;
    if (strcmp(v, "8no") == 0) {
        snprintf(err, errsz, "refused");
        return 1;
    }
    return 0;
}

static void cap_settingdialog(void) {
    char buf[32];
    tui_settingdialog d;
    CHECK_INT(tui_settingdialog_init(&d, "Port", "The port to listen on.", "1-65535", buf, sizeof buf, "80"),
              0, "setting dialog");
    d.validate = cap_setting_validate;
    tui_screen *s = tui_screen_new(14, 60);
    tui_settingdialog_render(&d, tui_canvas_of(s));
    char *txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "Port", "title drawn");
    CHECK_HAS(txt, "listen", "description drawn");
    CHECK_HAS(txt, "80", "value drawn");
    CHECK_HAS(txt, "1-65535", "hint drawn");
    CHECK_HAS(txt, "OK", "OK button");
    CHECK_HAS(txt, "Cancel", "Cancel button");
    free(txt);
    tui_event ev = cap_ev("\x7f");
    tui_settingdialog_handle(&d, &ev);
    CAP_TYPE(tui_settingdialog_handle, &d, "no");
    CHECK_STR(buf, "8no", "typed into the buffer");
    ev = cap_ev("\r");
    CHECK_INT(tui_settingdialog_handle(&d, &ev), TUI_DIALOG_OPEN, "a refused value keeps it open");
    CHECK_STR(d.error, "refused", "the reason");
    tui_settingdialog_render(&d, tui_canvas_of(s));
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "refused", "the reason drawn");
    free(txt);
    CAP_TYPE(tui_settingdialog_handle, &d, "x");
    ev = cap_ev("\t");
    tui_settingdialog_handle(&d, &ev);
    CHECK_INT(d.focus, TUI_SETTING_OK, "Tab to OK");
    ev = cap_ev("\r");
    CHECK_INT(tui_settingdialog_handle(&d, &ev), TUI_DIALOG_OK, "OK accepts");
    tui_settingdialog_free(&d);
    tui_settingdialog_init(&d, "Port", "x", NULL, buf, sizeof buf, "80");
    CAP_TYPE(tui_settingdialog_handle, &d, "1");
    ev = cap_ev("\x1b");
    CHECK_INT(tui_settingdialog_handle(&d, &ev), TUI_DIALOG_CANCELLED, "Esc cancels");
    CHECK_STR(buf, "80", "the value is put back");
    tui_settingdialog_free(&d);
    tui_screen_free(s);
}

static void cap_fieldset(void) {
    char to[32], subj[32];
    const char *kinds[] = { "plain", "html" };
    tui_field fields[3];
    tui_field_text(&fields[0], "To:", to, sizeof to, "");
    fields[0].complete = complete;
    fields[0].separators = ";,";
    tui_field_text(&fields[1], "Subject:", subj, sizeof subj, "hi");
    tui_field_choice(&fields[2], "Format:", kinds, 2, 0);
    CHECK_INT(fields[0].kind, TUI_FIELD_TEXT, "text field");
    CHECK_INT(fields[2].kind, TUI_FIELD_CHOICE, "choice field");

    tui_fieldset f;
    tui_fieldset_init(&f, fields, 3);
    f.validate = validate;
    CHECK_INT(f.focus, 0, "focus on the first");

    tui_event ev = cap_ev("\r");
    CHECK_INT(tui_fieldset_handle(&f, &ev), TUI_DIALOG_OPEN, "invalid form stays open");
    CHECK_STR(f.error, "To is empty", "validator's reason");
    tui_screen *s = tui_screen_new(10, 40);
    int rows = tui_fieldset_render(&f, tui_canvas_of(s));
    CHECK(rows >= 3, "render returns the rows used");
    char *txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "To:", "label");
    CHECK_HAS(txt, "Subject:", "second label");
    CHECK_HAS(txt, "hi", "initial value");
    CHECK_HAS(txt, "To is empty", "error under the fields");
    free(txt);

    tui_fieldset_set_error(&f, "custom");
    CHECK_STR(f.error, "custom", "set_error");
    CAP_TYPE(tui_fieldset_handle, &f, "al");
    CHECK_STR(f.error, "", "typing clears the error");
    CHECK_STR(to, "al", "typed into the focused field");
    CHECK_INT(f.ncand, 2, "typing asks for completion: two candidates for al");
    tui_fieldset_update_completion(&f);
    CHECK_INT(f.ncand, 2, "update_completion asks again");
    ev = cap_ev("\x1b[B");
    tui_fieldset_handle(&f, &ev);
    tui_fieldset_render(&f, tui_canvas_of(s));
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "albert", "drop-down drawn");
    free(txt);
    ev = cap_ev("\r");
    tui_fieldset_handle(&f, &ev);
    CHECK_STR(to, "alice", "Enter takes the candidate");

    ev = cap_ev("\t");
    tui_fieldset_handle(&f, &ev);
    CHECK_INT(f.focus, 1, "Tab moves on");
    tui_fieldset_focus(&f, 2);
    CHECK_INT(f.focus, 2, "focus");
    ev = cap_ev("\x1b[C");
    tui_fieldset_handle(&f, &ev);
    CHECK_INT(fields[2].choice, 1, "Right changes the choice");
    tui_fieldset_focus(&f, 99);
    CHECK_INT(f.focus, 2, "focus is clamped");
    ev = cap_ev("\r");
    CHECK_INT(tui_fieldset_handle(&f, &ev), TUI_DIALOG_OK, "valid form closes on Enter");
    f.state = TUI_DIALOG_OPEN;
    ev = cap_ev("\x1b");
    CHECK_INT(tui_fieldset_handle(&f, &ev), TUI_DIALOG_CANCELLED, "Esc cancels");
    tui_screen_free(s);
    tui_fieldset_free(&f);

    /* The form in a modal frame (TUI-131). */
    char nm[16];
    tui_field ff[2];
    tui_field_text(&ff[0], "Name:", nm, sizeof nm, "");
    tui_field_choice(&ff[1], "Type:", kinds, 2, 0);
    tui_fieldset fs;
    tui_fieldset_init(&fs, ff, 2);
    tui_dialog_form df;
    CHECK_INT(tui_dialog_form_init(&df, "New client", NULL, &fs), 0, "form dialog");
    s = tui_screen_new(12, 50);
    tui_dialog_form_render(&df, tui_canvas_of(s));
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "New client", "form dialog title");
    CHECK_HAS(txt, "Name:", "form dialog field");
    CHECK_HAS(txt, "Tab next", "form dialog key hint");
    free(txt);
    CAP_TYPE(tui_dialog_form_handle, &df, "acme");
    CHECK_STR(nm, "acme", "form dialog edits the field");
    ev = cap_ev("\r");
    CHECK_INT(tui_dialog_form_handle(&df, &ev), TUI_DIALOG_OK, "form dialog: Enter submits");
    tui_dialog_form_free(&df);
    tui_fieldset_free(&fs);
    tui_screen_free(s);
}

/* ---- actions: the key table, the action menu, the help ---------------- */

static void cap_actions(void) {
    static const tui_keyentry keys[] = {
        { "list", TUI_KC_STAR, TUI_VERB_STAR, "Star", NULL, TUI_ACT_ROUTINE, 0, NULL, 1 },
        { "list", TUI_KC_DELETE_D, TUI_VERB_DELETE, "Delete", NULL, TUI_ACT_SERIOUS, 1,
          "Delete %s? It cannot be undone.", 2 },
        { "list", TUI_KC_NONE, TUI_VERB_NONE, "Rotate secret", NULL, TUI_ACT_RARE, 0, NULL, 3 },
        { "list", TUI_KC_RELOAD, TUI_VERB_NONE, "Reload", NULL, TUI_ACT_HARMLESS, 0, NULL, 4 },
    };
    tui_keytable t;
    char why[128];
    CHECK_INT(tui_keytable_init(&t, keys, 4, why, sizeof why), 0, "a good table");

    static const tui_keyentry bad[] = {
        { "list", 'R', TUI_VERB_NONE, "Restart", NULL, TUI_ACT_ROUTINE, 0, NULL, 1 } };
    tui_keytable b;
    CHECK_INT(tui_keytable_init(&b, bad, 1, why, sizeof why), -1, "R is not in the registry");
    CHECK_HAS(why, "registry", "the reason");

    tui_event star = cap_ev("*"), del = cap_ev("d"), rel = cap_ev("r");
    CHECK_INT(tui_keytable_find(&t, "list", &star), 0, "star found");
    CHECK_INT(tui_keytable_find(&t, "other", &star), -1, "not in another view");
    CHECK_INT(tui_keycode_of(&del), TUI_KC_DELETE_D, "d");
    CHECK_INT(tui_registry_verb('d'), TUI_VERB_DELETE, "d deletes");
    CHECK_INT(tui_registry_verb('r'), TUI_VERB_NONE, "r is not a state change");
    CHECK_INT(tui_key_reserved('r'), 1, "r is reserved");
    CHECK_INT(tui_key_reserved('d'), 0, "d is not");
    CHECK_INT(tui_keytable_find(&t, "list", &rel), 3, "reload found");
    char name[16];
    CHECK_INT(tui_keycode_name(TUI_KC_QUIT, name, sizeof name), 6, "name length");
    CHECK_STR(name, "ctrl-c", "ctrl-c");

    tui_event q = cap_ev("q"), esc = cap_ev("\x1b"), cc = cap_ev("\x03"), bs = cap_ev("\x7f");
    CHECK_INT(tui_nav_key(&bs, 1), TUI_NAV_BACK, "Backspace goes back");
    CHECK_INT(tui_nav_key(&bs, 0), TUI_NAV_NONE, "Backspace never quits");
    CHECK_INT(tui_nav_key(&q, 0), TUI_NAV_NONE, "q never quits");
    CHECK_INT(tui_nav_key(&esc, 0), TUI_NAV_QUIT, "Esc on the top level quits");
    CHECK_INT(tui_nav_key(&esc, 1), TUI_NAV_BACK, "Esc goes back");
    CHECK_INT(tui_nav_key(&cc, 2), TUI_NAV_QUIT, "ctrl-c quits");

    tui_screen *scr = tui_screen_new(12, 60);
    tui_actionmenu m;
    CHECK_INT(tui_actionmenu_open(&m, &t, "list", "mail 7"), 0, "menu opens");
    tui_actionmenu_render(&m, tui_canvas_of(scr));
    char *txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, "Actions", "the menu title");
    CHECK_HAS(txt, "Delete", "the action listed");
    CHECK_HAS(txt, "> s  *  Star", "the quick key, then the direct key beside the action");
    CHECK_HAS(txt, "Rotate", "a menu-only action");
    free(txt);
    CAP_TYPE(tui_actionmenu_handle, &m, "j");
    {
        char qk[8];
        CHECK_INT(tui_keytable_assign_quick(&t, "list", NULL, qk), 0, "every entry has a quick key");
        CHECK_INT(qk[0], 's', "Star is s");
        CHECK_INT(qk[1], 'd', "Delete is d, the verb table's letter");
    }
    tui_event ent = cap_ev("\r");
    CHECK_INT(tui_actionmenu_handle(&m, &ent), TUI_ACTION_OPEN, "Delete asks first");
    tui_actionmenu_render(&m, tui_canvas_of(scr));
    txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, "mail", "the target is named");
    CHECK_HAS(txt, "7?", "in the question");
    free(txt);
    CHECK_INT(tui_actionmenu_handle(&m, &ent), TUI_ACTION_CANCELLED, "Enter on No does nothing");
    tui_actionmenu_free(&m);

    CHECK_INT(tui_actionmenu_run(&m, &t, 0, "mail 7"), 0, "run");
    CHECK_INT(m.state, TUI_ACTION_CHOSEN, "a routine action is chosen at once");
    tui_actionmenu_free(&m);
    CHECK_INT(tui_actionmenu_run(&m, &t, 1, "mail 7"), 0, "run a serious one");
    tui_event y = cap_ev("y");
    CHECK_INT(tui_actionmenu_handle(&m, &y), TUI_ACTION_CHOSEN, "y performs it");
    tui_actionmenu_free(&m);

    /* A quick key runs its entry as Enter does; the program may give the letters. */
    {
        char given[8] = { 'x', 0 };
        CHECK_INT(tui_actionmenu_open_keys(&m, &t, "list", "mail 7", given), 0, "menu with a given letter");
        tui_event ks = cap_ev("x");
        CHECK_INT(tui_actionmenu_handle(&m, &ks), TUI_ACTION_CHOSEN, "the given letter runs Star");
        tui_actionmenu_free(&m);
        CHECK_INT(tui_actionmenu_open(&m, &t, "list", "mail 7"), 0, "menu");
        tui_event kd = cap_ev("d"), kn = cap_ev("n");
        CHECK_INT(tui_actionmenu_handle(&m, &kd), TUI_ACTION_OPEN, "d asks first");
        CHECK_INT(tui_actionmenu_handle(&m, &kn), TUI_ACTION_CANCELLED, "n refuses");
        tui_actionmenu_free(&m);
    }

    tui_keyhelp h;
    CHECK_INT(tui_keyhelp_open(&h, &t, "list", "List shortcuts"), 0, "help");
    CHECK_INT(h.dialog.nitems, 4, "a row for every entry, the menu-only one too");
    tui_dialog_render(&h.dialog, tui_canvas_of(scr));
    txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, "Star", "the help lists the star");
    CHECK_HAS(txt, "Reload", "and the reload");
    free(txt);
    /* '/' searches whole entries: only the reload is left. */
    tui_event sl = cap_ev("/"), ty[4] = { cap_ev("r"), cap_ev("e"), cap_ev("l"), cap_ev("\r") };
    tui_dialog_handle(&h.dialog, &sl);
    for (int i = 0; i < 4; i++) tui_dialog_handle(&h.dialog, &ty[i]);
    tui_dialog_render(&h.dialog, tui_canvas_of(scr));
    txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, "Reload", "the search keeps the matching entry");
    CHECK_HAS(txt, "rel", "and shows its line");
    CHECK(strstr(txt, "Star") == NULL, "and drops the others");
    free(txt);
    tui_keyhelp_free(&h);

    static const tui_keyentry described[] = {
        { "list", TUI_KC_NONE, TUI_VERB_NONE, NULL, "The list of things; Enter opens one.",
          TUI_ACT_HARMLESS, 0, NULL, 0 },
        { "list", TUI_KC_RELOAD, TUI_VERB_NONE, "Reload", NULL, TUI_ACT_HARMLESS, 0, NULL, 1 } };
    tui_keytable dt;
    CHECK_INT(tui_keytable_init(&dt, described, 2, why, sizeof why), 0, "a table with a description");
    CHECK_INT(tui_keyhelp_open(&h, &dt, "list", "List shortcuts"), 0, "described help");
    tui_dialog_render(&h.dialog, tui_canvas_of(scr));
    txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, "things;", "the description is shown");
    CHECK_HAS(txt, "Reload", "above the keys");
    free(txt);
    tui_keyhelp_free(&h);
    tui_screen_free(scr);
}

static void cap_yesno(void) {
    tui_yesno yn;
    tui_yesno_init(&yn, "Delete R01?");
    CHECK_INT(tui_yesno_get(&yn), TUI_YESNO_OPEN, "yesno starts open");
    char out[128];
    CHECK(tui_yesno_format(&yn, 80, out, sizeof out) > 0, "yesno format");
    CHECK_STR(out, "Delete R01? (y/N)", "the question and the suffix");
    tui_yesno_format(&yn, 10, out, sizeof out);
    CHECK_STR(out, "Del\xE2\x80\xA6 (y/N)", "a narrow row keeps the suffix");

    tui_screen *scr = tui_screen_new(4, 40);
    tui_yesno_render(&yn, tui_canvas_of(scr), 3);
    char *txt = cap_screen_text(scr, NULL);
    CHECK_HAS(txt, "R01?", "yesno render on the given row");
    CHECK_HAS(txt, "(y/N)", "with its suffix");
    free(txt);
    tui_screen_free(scr);

    tui_event ev;
    memset(&ev, 0, sizeof ev);
    ev.type = TUI_EV_KEY;
    ev.key = TUI_KEY_CHAR;
    ev.cp = 'y';
    CHECK_INT(tui_yesno_handle(&yn, &ev), TUI_YESNO_YES, "y is yes");
    tui_yesno_init(&yn, "Save?");
    ev.key = TUI_KEY_ESC;
    ev.cp = 0;
    CHECK_INT(tui_yesno_handle(&yn, &ev), TUI_YESNO_NO, "Esc is no");
    tui_yesno_init(&yn, "Save?");
    ev.key = TUI_KEY_CHAR;
    ev.cp = 'c';
    ev.mods = TUI_MOD_CTRL;
    CHECK_INT(tui_yesno_handle(&yn, &ev), TUI_YESNO_INTERRUPT, "Ctrl-C is reported");
    CHECK_INT(tui_yesno_get(&yn), TUI_YESNO_INTERRUPT, "yesno get");
}

int main(void) {
    cap_table();
    cap_header();
    cap_statusline();
    cap_tabrow();
    cap_yesno();
    cap_menu();
    cap_dialog();
    cap_fieldset();
    cap_settingdialog();
    cap_filechooser();
    cap_actions();
    return cap_finish("cap-widgets");
}
