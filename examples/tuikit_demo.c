/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

/*
 * tuikit-demo -- a small two-pane message reader built on libtuikit.
 *
 * It is the library's worked example and its consumer check: a list on the
 * left, the selected message on the right, a status row, a search prompt
 * and a quit confirmation, redrawn on resize -- and a compose view that
 * uses the form, the multi-line editor and the checklist dialog.  When
 * standard input is not a terminal it prints the subjects as plain text.
 *
 * Reader keys: Up/Down/PgUp/PgDn/Home/End move in the focused pane, Tab
 * switches panes, / searches the subjects, c writes a message, q asks to
 * quit, Ctrl-C quits at once.
 *
 * Compose keys: Tab moves between the header fields (To completes from the
 * contacts; ";" separates addresses), Ctrl-O switches between the header
 * and the body, Ctrl-S sends (after asking for labels), Esc discards.
 *
 * Build: cc tuikit_demo.c $(pkg-config --cflags --libs libtuikit)
 */

#include <tuikit/tuikit.h>

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *subject;
    const char *body;
} message;

static const message g_messages[] = {
    { "Welcome", "Welcome to the tuikit demo.\n\nUse the arrow keys to move, "
                 "Tab to switch panes, / to search, c to write and q to quit." },
    { "Árvíztűrő tükörfúrógép", "Ékezetes betűk: á é í ó ö ő ú ü ű.\n"
                                 "Every one of them takes one column." },
    { "Wide characters", "\xE4\xB8\xAD\xE6\x96\x87 takes two columns per "
                         "character; the layout must not shift." },
    { "Budget 2027", "Line 1 of the budget.\nLine 2.\nLine 3.\nLine 4.\n"
                     "Line 5.\nLine 6.\nLine 7.\nLine 8.\nLine 9.\nLine 10.\n"
                     "Line 11.\nLine 12." },
    { "Meeting notes", "Agenda:\n\t1. Status\n\t2. Plans\n\t3. Questions" },
    { "Re: Budget 2027", "Looks good to me." },
};
#define N_MESSAGES ((int)(sizeof(g_messages) / sizeof(g_messages[0])))
#define MAX_SENT 4

typedef struct {
    const char *addr, *name, *label;
} contact;

static const contact g_contacts[] = {
    { "alice@example.com", "Alice Smith", "alice@example.com  Alice Smith" },
    { "albert@example.org", "Albert", "albert@example.org  Albert" },
    { "bob@example.net", "Bob", "bob@example.net  Bob" },
};
#define N_CONTACTS ((int)(sizeof(g_contacts) / sizeof(g_contacts[0])))

static const char *const g_labels[] = { "Work", "Personal", "Urgent" };
#define N_LABELS 3

typedef enum { FOCUS_LIST, FOCUS_BODY } focus_t;
typedef enum { DLG_SEARCH, DLG_QUIT, DLG_LABELS } dialog_t;

typedef struct {
    /* the reader */
    tui_listview list;
    tui_textview body;
    focus_t      focus;
    int          shown;          /* message in the body view, -1: none */
    char         sent_subject[MAX_SENT][64];
    char         sent_body[MAX_SENT][1400];
    int          nsent;
    /* the dialog on top, if any */
    tui_dialog   dialog;
    int          dialog_open;
    dialog_t     dialog_kind;
    char         query[64];
    char         status[96];
    /* the compose view */
    int          composing;
    int          compose_body;   /* the body has the focus, not the header */
    char         to[256], subject[64], text[1024];
    tui_field    fields[2];
    tui_fieldset form;
    tui_textedit editor;
    int          rows, cols;
} app;

static const tui_style st_normal = { TUI_COLOR_DEFAULT, TUI_COLOR_DEFAULT, 0 };

static int message_count(const app *a) { return N_MESSAGES + a->nsent; }

static const char *subject_of(const app *a, int i) {
    return i < N_MESSAGES ? g_messages[i].subject : a->sent_subject[i - N_MESSAGES];
}

static const char *body_of(const app *a, int i) {
    return i < N_MESSAGES ? g_messages[i].body : a->sent_body[i - N_MESSAGES];
}

static void show_message(app *a) {
    int i = a->list.selected;
    if (i < 0 || i == a->shown) return;
    tui_textview_set_text(&a->body, body_of(a, i));
    a->shown = i;
}

/* ── The reader ──────────────────────────────────────────────────────── */

static void draw_subject(void *ctx, tui_canvas row, int index, int selected) {
    app *a = ctx;
    tui_style st = st_normal;
    if (selected) {
        st.attrs = a->focus == FOCUS_LIST ? TUI_ATTR_REVERSE : TUI_ATTR_BOLD;
        tui_canvas_clear(row, st);
    }
    tui_canvas_put_aligned(row, 0, subject_of(a, index), TUI_ALIGN_LEFT, st);
}

/* The status row: hints or a message on the left, the size on the right
 * when there is room for both. */
static void draw_status(app *a, tui_canvas row, const char *text) {
    tui_style bar = tui_style_attrs(TUI_ATTR_REVERSE);
    tui_canvas_clear(row, bar);
    tui_canvas_put_aligned(row, 0, text, TUI_ALIGN_LEFT, bar);
    char size[32];
    snprintf(size, sizeof(size), "%dx%d", a->rows, a->cols);
    if (tui_str_width(text) + 1 + tui_str_width(size) <= tui_canvas_cols(row))
        tui_canvas_put_aligned(row, 0, size, TUI_ALIGN_RIGHT, bar);
}

static void draw_reader(app *a, tui_canvas all) {
    tui_rect top, status, left, right;
    tui_rect_split_v(tui_canvas_rect(all), -1, &top, &status);
    int left_w = a->cols / 3;
    if (left_w < 12) left_w = 12;
    tui_rect_split_h(top, left_w, &left, &right);

    tui_style frame_l = a->focus == FOCUS_LIST ? tui_style_attrs(TUI_ATTR_BOLD) : st_normal;
    tui_style frame_r = a->focus == FOCUS_BODY ? tui_style_attrs(TUI_ATTR_BOLD) : st_normal;
    tui_canvas_box(all, left, "Messages", frame_l);
    tui_canvas_box(all, right, a->shown >= 0 ? subject_of(a, a->shown) : "", frame_r);
    tui_listview_render(&a->list, tui_canvas_sub(all, tui_rect_inset(left, 1)), st_normal,
                        draw_subject, a);
    tui_textview_render(&a->body, tui_canvas_sub(all, tui_rect_inset(right, 1)), st_normal);
    draw_status(a, tui_canvas_sub(all, status),
                a->status[0] ? a->status : "Tab pane  / search  q quit");
}

/* ── The compose view ────────────────────────────────────────────────── */

/* A case-insensitive substring test, ASCII letters folded. */
static int contains(const char *hay, const char *needle) {
    size_t n = strlen(needle);
    for (; *hay; hay++) {
        size_t i = 0;
        while (i < n && hay[i] &&
               tolower((unsigned char)hay[i]) == tolower((unsigned char)needle[i])) i++;
        if (i == n) return 1;
    }
    return n == 0;
}

/* The To field's completion: contacts whose address or name contains the
 * token being typed. */
static int complete_contact(void *user, int field, const char *token,
                            tui_completion *out, int max) {
    (void)user;
    (void)field;
    if (!token[0]) return 0;
    int n = 0;
    for (int i = 0; i < N_CONTACTS && n < max; i++) {
        if (contains(g_contacts[i].addr, token) || contains(g_contacts[i].name, token)) {
            out[n].text = g_contacts[i].addr;
            out[n].label = g_contacts[i].label;
            n++;
        }
    }
    return n;
}

static void compose_open(app *a) {
    tui_field_text(&a->fields[0], "To:", a->to, sizeof(a->to), NULL);
    a->fields[0].separators = ";,";
    a->fields[0].complete = complete_contact;
    tui_field_text(&a->fields[1], "Subject:", a->subject, sizeof(a->subject), NULL);
    tui_fieldset_init(&a->form, a->fields, 2);
    tui_textedit_init(&a->editor, a->text, sizeof(a->text), NULL);
    a->composing = 1;
    a->compose_body = 0;
    a->status[0] = '\0';
}

static void compose_close(app *a, const char *status) {
    tui_fieldset_free(&a->form);
    a->composing = 0;
    snprintf(a->status, sizeof(a->status), "%s", status);
}

/* The message goes into the list, with its labels, and is selected. */
static void compose_send(app *a) {
    if (a->nsent == MAX_SENT) {
        compose_close(a, "Outbox full");
        return;
    }
    int k = a->nsent++;
    snprintf(a->sent_subject[k], sizeof(a->sent_subject[k]), "%s",
             a->subject[0] ? a->subject : "(no subject)");
    char labels[64] = "";
    for (int i = 0; i < N_LABELS; i++) {
        if (!tui_dialog_is_checked(&a->dialog, i)) continue;
        if (labels[0]) strcat(labels, ", ");
        strcat(labels, g_labels[i]);
    }
    snprintf(a->sent_body[k], sizeof(a->sent_body[k]), "To: %s\nLabels: %s\n\n%s",
             a->to, labels[0] ? labels : "none", a->text);
    tui_listview_set_count(&a->list, message_count(a));
    tui_listview_select(&a->list, N_MESSAGES + k);
    a->shown = -1;
    show_message(a);
    char st[96];
    snprintf(st, sizeof(st), "Sent to %.80s", a->to);   /* the status row is short */
    compose_close(a, st);
}

static void draw_compose(app *a, tui_canvas all) {
    int rows = tui_canvas_rows(all), cols = tui_canvas_cols(all);
    tui_canvas title = tui_canvas_sub(all, tui_rect_make(0, 0, 1, cols));
    tui_canvas_clear(title, tui_style_attrs(TUI_ATTR_REVERSE));
    tui_canvas_put(title, 0, 1, "New message", tui_style_attrs(TUI_ATTR_REVERSE | TUI_ATTR_BOLD));
    /* Six rows for the header: two fields, three of drop-down, the error. */
    tui_canvas header = tui_canvas_sub(all, tui_rect_make(1, 0, 6, cols));
    tui_canvas_hline(all, 7, 0, cols, st_normal);
    tui_canvas body = tui_canvas_sub(all, tui_rect_make(8, 0, rows - 9, cols));
    /* The focused part is drawn last: it places the cursor. */
    if (a->compose_body) {
        tui_fieldset_render(&a->form, header);
        tui_textedit_render(&a->editor, body, st_normal);
    } else {
        tui_textedit_render(&a->editor, body, st_normal);
        tui_fieldset_render(&a->form, header);
    }
    char hint[96];
    int line, col;
    tui_textedit_cursor(&a->editor, &line, &col);
    snprintf(hint, sizeof(hint), "%s  Ctrl-O switch  Ctrl-S send  Esc discard",
             a->compose_body ? "[body]" : "[header]");
    if (a->compose_body)
        snprintf(hint + strlen(hint), sizeof(hint) - strlen(hint), "  %d:%d", line + 1, col + 1);
    draw_status(a, tui_canvas_sub(all, tui_rect_make(rows - 1, 0, 1, cols)), hint);
}

static void draw(app *a, tui_screen *scr) {
    tui_canvas all = tui_canvas_of(scr);
    tui_canvas_clear(all, st_normal);
    tui_screen_set_cursor(scr, 0, 0, 0);
    if (a->composing) draw_compose(a, all);
    else draw_reader(a, all);
    if (a->dialog_open) tui_dialog_render(&a->dialog, all);
}

/* ── Keys ────────────────────────────────────────────────────────────── */

/* Applies a closed dialog; 1 when the program should end. */
static int dialog_closed(app *a) {
    int quit = 0;
    if (a->dialog_kind == DLG_SEARCH) {
        if (a->dialog.state == TUI_DIALOG_OK && a->query[0]) {
            int found = -1;
            for (int i = 0; i < message_count(a) && found < 0; i++)
                if (strstr(subject_of(a, i), a->query)) found = i;
            if (found >= 0) {
                tui_listview_select(&a->list, found);
                show_message(a);
                snprintf(a->status, sizeof(a->status), "Found \"%s\"", a->query);
            } else {
                snprintf(a->status, sizeof(a->status), "No match for \"%s\"", a->query);
            }
        }
    } else if (a->dialog_kind == DLG_LABELS) {
        if (a->dialog.state == TUI_DIALOG_OK) compose_send(a);
    } else {
        quit = a->dialog.state == TUI_DIALOG_OK && a->dialog.answer == 1;
    }
    tui_dialog_free(&a->dialog);
    a->dialog_open = 0;
    return quit;
}

static void compose_key(app *a, const tui_event *ev) {
    if (TUI_EV_IS_CTRL(ev, 'o')) {
        a->compose_body = !a->compose_body;
        return;
    }
    if (TUI_EV_IS_CTRL(ev, 's')) {
        if (!a->to[0]) {
            tui_fieldset_set_error(&a->form, "To: is empty \xE2\x80\x94 enter a recipient.");
            tui_fieldset_focus(&a->form, 0);
            a->compose_body = 0;
            return;
        }
        tui_dialog_checklist(&a->dialog, "Labels", g_labels, N_LABELS, NULL, 0);
        a->dialog_open = 1;
        a->dialog_kind = DLG_LABELS;
        return;
    }
    if (a->compose_body) {
        if (ev->type == TUI_EV_KEY && ev->key == TUI_KEY_ESC) compose_close(a, "Discarded");
        else tui_textedit_handle(&a->editor, ev);
        return;
    }
    tui_dialog_state st = tui_fieldset_handle(&a->form, ev);
    if (st == TUI_DIALOG_CANCELLED) {
        compose_close(a, "Discarded");
    } else if (st == TUI_DIALOG_OK) {
        /* Enter in the header goes on to the body. */
        a->form.state = TUI_DIALOG_OPEN;
        a->compose_body = 1;
    }
}

/* One key; 1 when the program should end. */
static int handle_key(app *a, const tui_event *ev) {
    if (TUI_EV_IS_CTRL(ev, 'c')) return 1;
    if (a->dialog_open) {
        if (tui_dialog_handle(&a->dialog, ev) != TUI_DIALOG_OPEN) return dialog_closed(a);
        return 0;
    }
    if (a->composing) {
        compose_key(a, ev);
        return 0;
    }
    a->status[0] = '\0';
    if (ev->type == TUI_EV_KEY && ev->key == TUI_KEY_TAB) {
        a->focus = a->focus == FOCUS_LIST ? FOCUS_BODY : FOCUS_LIST;
        return 0;
    }
    if (TUI_EV_IS_CHAR(ev, 'q')) {
        tui_dialog_confirm(&a->dialog, "Quit", "Leave the demo?", 0);
        a->dialog_open = 1;
        a->dialog_kind = DLG_QUIT;
        return 0;
    }
    if (TUI_EV_IS_CHAR(ev, '/')) {
        tui_dialog_prompt(&a->dialog, "Search", "Subject contains:", a->query,
                          sizeof(a->query), NULL);
        a->dialog_open = 1;
        a->dialog_kind = DLG_SEARCH;
        return 0;
    }
    if (TUI_EV_IS_CHAR(ev, 'c')) {
        compose_open(a);
        return 0;
    }
    if (a->focus == FOCUS_LIST) {
        if (tui_listview_handle(&a->list, ev)) show_message(a);
    } else {
        tui_textview_handle(&a->body, ev);
    }
    return 0;
}

/* Not a terminal: the same content as plain text (email-cli's non-TTY
 * fallback). */
static int print_plain(void) {
    for (int i = 0; i < N_MESSAGES; i++) printf("%d. %s\n", i + 1, g_messages[i].subject);
    return 0;
}

int main(void) {
    tui_term_opts o;
    tui_term_opts_init(&o);
    o.flags |= TUI_TERM_PASTE;
    tui_term *t = tui_term_open(&o);
    if (!t) {
        if (errno == ENOTTY) return print_plain();
        perror("tuikit-demo: terminal");
        return 1;
    }

    static app a;
    memset(&a, 0, sizeof(a));
    a.shown = -1;
    tui_listview_init(&a.list);
    tui_listview_set_count(&a.list, N_MESSAGES);
    tui_textview_init(&a.body);
    show_message(&a);

    if (tui_term_size(t, &a.rows, &a.cols) != 0) {
        a.rows = 24;
        a.cols = 80;
    }
    tui_screen *scr = tui_screen_new(a.rows, a.cols);
    tui_buf out;
    tui_buf_init(&out);
    int rc = 0;

    for (int quit = 0; !quit && scr;) {
        draw(&a, scr);
        tui_screen_render(scr, &out);
        if (tui_term_flush_buf(t, &out) != 0) {
            rc = 1;
            break;
        }
        tui_event ev;
        if (tui_term_wait(t, -1, NULL, 0, &ev) < 0) {
            rc = 1;
            break;
        }
        switch (ev.type) {
        case TUI_EV_RESIZE:
            a.rows = ev.rows;
            a.cols = ev.cols;
            tui_screen_resize(scr, ev.rows, ev.cols);
            break;
        case TUI_EV_HANGUP:
            quit = 1;
            break;
        case TUI_EV_KEY:
        case TUI_EV_PASTE:
            quit = handle_key(&a, &ev);
            break;
        default:
            break;
        }
    }

    if (a.dialog_open) tui_dialog_free(&a.dialog);
    if (a.composing) tui_fieldset_free(&a.form);
    tui_textview_free(&a.body);
    tui_buf_free(&out);
    tui_screen_free(scr);
    tui_term_close(t);
    return rc;
}
