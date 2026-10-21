/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-16 */
/* Capability tests: tui_table, tui_statusline, tui_dialog, tui_fieldset, the key table. */

#include "cap_common.h"

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
static uint64_t t_id(void *ctx, int i) { (void)ctx; return 1000u + (uint64_t)i; }

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

    tui_table_set_message(&t, "nothing here", TUI_STYLE_HINT);
    tui_canvas_clear(c, TUI_STYLE_DEFAULT);
    tui_table_render(&t, c, TUI_STYLE_DEFAULT);
    txt = cap_screen_text(s, NULL);
    CHECK_HAS(txt, "nothing here", "message replaces the rows");
    CHECK(strstr(txt, "name1") == NULL, "no rows under a message");
    free(txt);
    tui_screen_free(s);
    tui_table_free(&t);
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
    CHECK_HAS(out, "Saved", "message in the line");
    tui_event ev = cap_ev("x");
    CHECK_INT(tui_statusline_handle(&sl, &ev), TUI_STATUSLINE_CHANGED, "a key clears the message");
    CHECK_STR(tui_statusline_message(&sl), "", "message gone");
    tui_statusline_set_message(&sl, "again", TUI_STYLE_ERROR);
    tui_statusline_clear_message(&sl);
    CHECK_STR(tui_statusline_message(&sl), "", "clear_message");

    tui_statusline_set_wait(&sl, "Syncing", 2, 7);
    tui_statusline_format(&sl, 80, out, sizeof out);
    CHECK_HAS(out, "Syncing 2/7", "wait with progress");
    tui_statusline_clear_wait(&sl);
    tui_statusline_format(&sl, 80, out, sizeof out);
    CHECK(strstr(out, "Syncing") == NULL, "wait cleared");

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
    tui_screen_free(s);
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
    CHECK_HAS(txt, "> *  Star", "the direct key beside the action");
    CHECK_HAS(txt, "Rotate", "a menu-only action");
    free(txt);
    CAP_TYPE(tui_actionmenu_handle, &m, "j");
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

int main(void) {
    cap_table();
    cap_statusline();
    cap_dialog();
    cap_fieldset();
    cap_actions();
    return cap_finish("cap-widgets");
}
