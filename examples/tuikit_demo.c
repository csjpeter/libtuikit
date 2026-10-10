/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* tuikit-demo: a small message reader, the library's worked example. */

/*
 * tuikit-demo -- a small message reader built on libtuikit.
 *
 * It is the library's worked example and its consumer check, and it
 * follows the workspace's interface policy (policies/tui-interface-model.md)
 * the way every TUI here should: a header bar, the body, one status line;
 * the palette; the shared keys; help on ?; a modal at a time.
 *
 * The reader: a list of subjects on the left, the selected message on the
 * right.  The list keys move (j/k, the arrows, ctrl-f/ctrl-b, g/G), Enter
 * or Tab reads the message, / filters the subjects and c clears the
 * filter, n writes a new message, ? shows the keys, Esc quits, ctrl-c
 * quits at once, ctrl-z suspends.
 *
 * The compose view: To (completed from the contacts; ";" separates
 * addresses) and Subject, then the body, a full-screen editor.  Tab moves
 * on, shift-Tab back, ctrl-s sends after asking for labels, Esc discards
 * (asking first when something was typed).
 *
 * Below 80x24 it shows a notice instead of a layout it cannot draw.  When
 * standard input is not a terminal it prints the subjects as plain text.
 *
 * Build: cc tuikit_demo.c $(pkg-config --cflags --libs libtuikit)
 */

#include <tuikit/tuikit.h>

#include <ctype.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>

#define MIN_ROWS 24
#define MIN_COLS 80
#define PROGRAM  "tuikit-demo"

typedef struct {
    const char *subject;
    const char *body;
} message;

static const message g_messages[] = {
    { "Welcome", "Welcome to the tuikit demo.\n\nThe arrow keys or j and k move, "
                 "Enter reads, / filters, n writes, ? lists the keys and Esc "
                 "quits." },
    { "Árvíztűrő tükörfúrógép", "Ékezetes betűk: á é í ó ö ő ú ü ű.\n"
                                 "Every one of them takes one column." },
    { "Wide characters", "\xE4\xB8\xAD\xE6\x96\x87 takes two columns per "
                         "character; the layout must not shift." },
    { "Budget 2027", "Line 1 of the budget.\nLine 2.\nLine 3.\nLine 4.\n"
                     "Line 5.\nLine 6.\nLine 7.\nLine 8.\nLine 9.\nLine 10.\n"
                     "Line 11.\nLine 12.\nLine 13.\nLine 14.\nLine 15.\n"
                     "Line 16.\nLine 17.\nLine 18.\nLine 19.\nLine 20.\n"
                     "Line 21.\nLine 22.\nLine 23.\nLine 24." },
    { "Meeting notes", "Agenda:\n\t1. Status\n\t2. Plans\n\t3. Questions" },
    { "Re: Budget 2027", "Looks good to me." },
};
#define N_MESSAGES ((int)(sizeof(g_messages) / sizeof(g_messages[0])))
#define MAX_SENT 4
#define MAX_ALL (N_MESSAGES + MAX_SENT)

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

/* The help screens: key, what it does. */
static const char *const g_list_keys[] = {
    "j, k, \xE2\x86\x93, \xE2\x86\x91", "move one row",
    "ctrl-f, ctrl-b",                   "move one page",
    "g, G",                             "first, last row",
    "Enter, Tab, l, \xE2\x86\x92",      "read the message",
    "/",                                "filter the subjects",
    "c",                                "clear the filter",
    "n",                                "write a new message",
    "?",                                "this help",
    "Esc",                              "quit",
    "ctrl-c",                           "quit at once",
    "ctrl-z",                           "suspend",
};
static const char *const g_body_keys[] = {
    "j, k, \xE2\x86\x93, \xE2\x86\x91", "scroll one line",
    "ctrl-f, ctrl-b",                   "scroll one page",
    "g, G",                             "top, end",
    "Esc, q, Tab, h, \xE2\x86\x90",     "back to the list",
    "n",                                "write a new message",
    "?",                                "this help",
    "ctrl-c",                           "quit at once",
};

typedef enum { FOCUS_LIST, FOCUS_BODY } focus_t;
typedef enum { DLG_FILTER, DLG_HELP, DLG_LABELS, DLG_DISCARD } dialog_t;
typedef enum { ST_PLAIN, ST_OK } status_kind;

typedef struct {
    /* the reader */
    tui_listview list;           /* over shown[] */
    tui_textview body;
    focus_t      focus;
    int          open;           /* message in the body view, -1: none */
    int          shown[MAX_ALL]; /* the messages the filter lets through */
    int          nshown;
    char         filter[64];
    char         sent_subject[MAX_SENT][64];
    char         sent_body[MAX_SENT][1400];
    int          nsent;
    /* the modal on top, if any */
    tui_dialog   dialog;
    int          dialog_open;
    dialog_t     dialog_kind;
    char         query[64];
    /* the status line: a message until the next key, else the hints */
    char         status[96];
    status_kind  status_kind;
    /* the compose view */
    int          composing;
    int          compose_body;   /* the body has the cursor, not the header */
    char         to[256], subject[64], text[1024];
    tui_field    fields[2];
    tui_fieldset form;
    tui_textedit editor;
    int          rows, cols;
} app;

static const tui_style st_plain = { TUI_COLOR_DEFAULT, TUI_COLOR_DEFAULT, 0 };

static int message_count(const app *a) { return N_MESSAGES + a->nsent; }

static const char *subject_of(const app *a, int i) {
    return i < N_MESSAGES ? g_messages[i].subject : a->sent_subject[i - N_MESSAGES];
}

static const char *body_of(const app *a, int i) {
    return i < N_MESSAGES ? g_messages[i].body : a->sent_body[i - N_MESSAGES];
}

static void set_status(app *a, status_kind kind, const char *text) {
    snprintf(a->status, sizeof(a->status), "%s", text);
    a->status_kind = kind;
}

/* The message under the list's cursor, -1 when the list is empty. */
static int selected_message(const app *a) {
    int i = a->list.selected;
    return i >= 0 && i < a->nshown ? a->shown[i] : -1;
}

static void show_selected(app *a) {
    int m = selected_message(a);
    if (m == a->open) return;
    tui_textview_set_text(&a->body, m >= 0 ? body_of(a, m) : "");
    a->open = m;
}

/* Rebuilds the list from the filter; the cursor goes to @p want (a message
 * index) if it is shown, else to the first row. */
static void apply_filter(app *a, int want) {
    a->nshown = 0;
    int row = 0;
    for (int i = 0; i < message_count(a); i++) {
        if (a->filter[0] && !strstr(subject_of(a, i), a->filter)) continue;
        if (i == want) row = a->nshown;
        a->shown[a->nshown++] = i;
    }
    tui_listview_set_count(&a->list, a->nshown);
    tui_listview_select(&a->list, row);
    show_selected(a);
}

/* ── Frame: the header bar and the status line ───────────────────────── */

/* Where we are on the left, the program on the right; the left side is
 * clipped first. */
static void draw_header(tui_canvas row, const char *where) {
    tui_canvas_clear(row, TUI_STYLE_HEADER);
    int cols = tui_canvas_cols(row), name_w = (int)strlen(PROGRAM) + 1;
    tui_canvas_put_aligned(row, 0, PROGRAM " ", TUI_ALIGN_RIGHT, TUI_STYLE_HEADER);
    tui_canvas left = tui_canvas_sub(row, tui_rect_make(0, 1, 1, cols - name_w - 2));
    tui_canvas_put_aligned(left, 0, where, TUI_ALIGN_LEFT, TUI_STYLE_HEADER);
}

/* The message if there is one, else the hints; the hints always fit, a
 * message is cut to make room for them. */
static void draw_status(app *a, tui_canvas row, const char *hints) {
    tui_canvas_clear(row, st_plain);
    if (!a->status[0]) {
        tui_canvas_put_aligned(row, 0, hints, TUI_ALIGN_LEFT, TUI_STYLE_HINT);
        return;
    }
    tui_style st = a->status_kind == ST_OK ? TUI_STYLE_OK : st_plain;
    tui_canvas_put_aligned(row, 0, a->status, TUI_ALIGN_LEFT, st);
}

/* ── The reader ──────────────────────────────────────────────────────── */

static void draw_subject(void *ctx, tui_canvas row, int index, int selected) {
    app *a = ctx;
    tui_style st = st_plain;
    if (selected) {
        /* The focused list fills the row; the other pane's cursor is bold. */
        st = a->focus == FOCUS_LIST ? TUI_STYLE_SELECTION : tui_style_attrs(TUI_ATTR_BOLD);
        tui_canvas_clear(row, st);
    }
    tui_canvas_put_aligned(row, 0, subject_of(a, a->shown[index]), TUI_ALIGN_LEFT, st);
}

static void draw_reader(app *a, tui_canvas all) {
    char where[128];
    if (a->filter[0])
        snprintf(where, sizeof(where), "Inbox \xC2\xB7 %d of %d \xC2\xB7 filter: %s",
                 a->nshown, message_count(a), a->filter);
    else
        snprintf(where, sizeof(where), "Inbox \xC2\xB7 %d messages", message_count(a));
    draw_header(tui_canvas_sub(all, tui_rect_make(0, 0, 1, a->cols)), where);

    tui_rect body = tui_rect_make(1, 0, a->rows - 2, a->cols), left, right;
    tui_rect_split_h(body, a->cols / 3, &left, &right);
    tui_style frame_l = a->focus == FOCUS_LIST ? TUI_STYLE_ACCENT : st_plain;
    tui_style frame_r = a->focus == FOCUS_BODY ? TUI_STYLE_ACCENT : st_plain;
    tui_canvas_box(all, left, "Messages", frame_l);
    tui_canvas_box(all, right, a->open >= 0 ? subject_of(a, a->open) : "", frame_r);

    /* The list: a header row naming the column, then the rows. */
    tui_rect in = tui_rect_inset(left, 1);
    tui_canvas cl = tui_canvas_sub(all, in);
    tui_canvas_put(cl, 0, 0, "Subject", tui_style_attrs(TUI_ATTR_BOLD));
    tui_canvas rows = tui_canvas_sub(cl, tui_rect_make(1, 0, in.rows - 1, in.cols));
    if (a->nshown == 0) {
        tui_canvas_put_aligned(rows, 0, "Nothing matches.", TUI_ALIGN_LEFT, st_plain);
        tui_canvas_put_aligned(rows, 1, "c clears the filter.", TUI_ALIGN_LEFT, TUI_STYLE_HINT);
    } else {
        tui_listview_render(&a->list, rows, st_plain, draw_subject, a);
    }
    tui_textview_render(&a->body, tui_canvas_sub(all, tui_rect_inset(right, 1)), st_plain);

    draw_status(a, tui_canvas_sub(all, tui_rect_make(a->rows - 1, 0, 1, a->cols)),
                a->focus == FOCUS_LIST
                    ? "\xE2\x86\x91\xE2\x86\x93 move  Enter read  / filter  c clear  n new  ? help  Esc quit"
                    : "\xE2\x86\x91\xE2\x86\x93 scroll  Esc back  n new  ? help");
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
}

static void compose_close(app *a, status_kind kind, const char *status) {
    tui_fieldset_free(&a->form);
    a->composing = 0;
    set_status(a, kind, status);
}

/* Esc: what was typed is not thrown away on one stray key. */
static void compose_discard(app *a) {
    if (!a->to[0] && !a->subject[0] && !a->text[0]) {
        compose_close(a, ST_PLAIN, "Discarded.");
        return;
    }
    tui_dialog_confirm(&a->dialog, "Discard the message", "Throw away what was typed?", 0);
    a->dialog_open = 1;
    a->dialog_kind = DLG_DISCARD;
}

/* The message goes into the list, with its labels, and is selected. */
static void compose_send(app *a) {
    if (a->nsent == MAX_SENT) {
        compose_close(a, ST_PLAIN, "Outbox full: the message was not sent.");
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
             a->to, labels[0] ? labels : "-", a->text);
    a->open = -1;
    apply_filter(a, N_MESSAGES + k);
    a->focus = FOCUS_LIST;
    char st[96];
    snprintf(st, sizeof(st), "Sent to %.80s.", a->to);
    compose_close(a, ST_OK, st);
}

static void draw_compose(app *a, tui_canvas all) {
    int rows = a->rows, cols = a->cols;
    draw_header(tui_canvas_sub(all, tui_rect_make(0, 0, 1, cols)), "New message");
    /* Six rows for the header fields: two fields, three of drop-down, the
     * message; then a rule, then the body to the status line. */
    tui_canvas fields = tui_canvas_sub(all, tui_rect_make(1, 0, 6, cols));
    tui_canvas_hline(all, 7, 0, cols, st_plain);
    tui_canvas body = tui_canvas_sub(all, tui_rect_make(8, 0, rows - 9, cols));
    /* The part with the cursor is drawn last: it places the cursor. */
    if (a->compose_body) {
        /* No field has the cursor: no label is the focused one. */
        tui_style focus = a->form.focus_style;
        a->form.focus_style = a->form.label_style;
        tui_fieldset_render(&a->form, fields);
        a->form.focus_style = focus;
        tui_textedit_render(&a->editor, body, st_plain);
    } else {
        tui_textedit_render(&a->editor, body, st_plain);
        tui_fieldset_render(&a->form, fields);
    }
    char hints[128];
    if (a->compose_body) {
        int line, col;
        tui_textedit_cursor(&a->editor, &line, &col);
        snprintf(hints, sizeof(hints),
                 "shift-Tab fields  ctrl-s send  Esc discard  line %d, col %d%s",
                 line + 1, col + 1, a->editor.truncated ? "  limit reached" : "");
    } else {
        snprintf(hints, sizeof(hints), "Tab next field  ctrl-s send  Esc discard");
    }
    draw_status(a, tui_canvas_sub(all, tui_rect_make(rows - 1, 0, 1, cols)), hints);
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

static void draw(app *a, tui_screen *scr) {
    tui_canvas all = tui_canvas_of(scr);
    tui_canvas_clear(all, st_plain);
    tui_screen_set_cursor(scr, 0, 0, 0);
    if (a->rows < MIN_ROWS || a->cols < MIN_COLS) {
        char line[96];
        snprintf(line, sizeof(line), "The terminal is %dx%d; " PROGRAM " needs %dx%d.",
                 a->cols, a->rows, MIN_COLS, MIN_ROWS);
        int mid = a->rows / 2 > 0 ? a->rows / 2 - 1 : 0;
        tui_canvas_put_aligned(all, mid, "Terminal too small", TUI_ALIGN_CENTER, TUI_STYLE_ERROR);
        tui_canvas_put_aligned(all, mid + 1, line, TUI_ALIGN_CENTER, st_plain);
        tui_canvas_put_aligned(all, mid + 2, "Esc quits.", TUI_ALIGN_CENTER, TUI_STYLE_HINT);
        return;
    }
    if (a->composing) draw_compose(a, all);
    else draw_reader(a, all);
    if (a->dialog_open) tui_dialog_render(&a->dialog, all);
}

/* ── Keys ────────────────────────────────────────────────────────────── */

static void open_dialog(app *a, dialog_t kind) {
    a->dialog_open = 1;
    a->dialog_kind = kind;
}

static void dialog_closed(app *a) {
    tui_dialog_state st = a->dialog.state;
    switch (a->dialog_kind) {
    case DLG_FILTER:
        if (st == TUI_DIALOG_OK) {
            snprintf(a->filter, sizeof(a->filter), "%s", a->query);
            apply_filter(a, selected_message(a));
        }
        break;
    case DLG_LABELS:
        if (st == TUI_DIALOG_OK) compose_send(a);
        break;
    case DLG_DISCARD:
        if (st == TUI_DIALOG_OK && a->dialog.answer == 1) compose_close(a, ST_PLAIN, "Discarded.");
        break;
    case DLG_HELP:
        break;
    }
    tui_dialog_free(&a->dialog);
    a->dialog_open = 0;
}

static int is_key(const tui_event *ev, tui_key key, unsigned mods) {
    return ev->type == TUI_EV_KEY && ev->key == key && ev->mods == mods;
}

static void compose_key(app *a, const tui_event *ev) {
    if (TUI_EV_IS_CTRL(ev, 's')) {
        if (!a->to[0]) {
            tui_fieldset_set_error(&a->form, "To: is empty \xE2\x80\x94 enter a recipient.");
            tui_fieldset_focus(&a->form, 0);
            a->compose_body = 0;
            return;
        }
        tui_dialog_checklist(&a->dialog, "Labels", g_labels, N_LABELS, NULL, 0);
        open_dialog(a, DLG_LABELS);
        return;
    }
    if (a->compose_body) {
        if (is_key(ev, TUI_KEY_ESC, 0)) compose_discard(a);
        else if (is_key(ev, TUI_KEY_TAB, TUI_MOD_SHIFT)) a->compose_body = 0;
        else tui_textedit_handle(&a->editor, ev);
        return;
    }
    /* Tab past the last field, with no drop-down open, goes on to the body. */
    if (is_key(ev, TUI_KEY_TAB, 0) && a->form.ncand == 0 && a->form.focus == 1) {
        a->compose_body = 1;
        return;
    }
    tui_dialog_state st = tui_fieldset_handle(&a->form, ev);
    if (st == TUI_DIALOG_CANCELLED) {
        a->form.state = TUI_DIALOG_OPEN;
        compose_discard(a);
    } else if (st == TUI_DIALOG_OK) {
        /* Enter in the header goes on to the body. */
        a->form.state = TUI_DIALOG_OPEN;
        a->compose_body = 1;
    }
}

static void reader_key(app *a, const tui_event *ev, int *quit) {
    if (TUI_EV_IS_CHAR(ev, '?')) {
        if (a->focus == FOCUS_LIST)
            tui_dialog_help(&a->dialog, "Message list shortcuts", g_list_keys,
                            (int)(sizeof(g_list_keys) / sizeof(g_list_keys[0]) / 2));
        else
            tui_dialog_help(&a->dialog, "Message shortcuts", g_body_keys,
                            (int)(sizeof(g_body_keys) / sizeof(g_body_keys[0]) / 2));
        open_dialog(a, DLG_HELP);
        return;
    }
    if (TUI_EV_IS_CHAR(ev, 'n')) {
        compose_open(a);
        return;
    }
    if (TUI_EV_IS_CHAR(ev, '/')) {
        tui_dialog_prompt(&a->dialog, "Filter the subjects", "Subject contains:", a->query,
                          sizeof(a->query), a->filter);
        open_dialog(a, DLG_FILTER);
        return;
    }
    if (TUI_EV_IS_CHAR(ev, 'c')) {
        if (a->filter[0]) {
            a->filter[0] = '\0';
            apply_filter(a, selected_message(a));
        }
        return;
    }
    if (a->focus == FOCUS_BODY) {
        if (is_key(ev, TUI_KEY_ESC, 0) || is_key(ev, TUI_KEY_TAB, 0) ||
            is_key(ev, TUI_KEY_LEFT, 0) || TUI_EV_IS_CHAR(ev, 'q') || TUI_EV_IS_CHAR(ev, 'h'))
            a->focus = FOCUS_LIST;
        else
            tui_textview_handle(&a->body, ev);
        return;
    }
    if (is_key(ev, TUI_KEY_ESC, 0)) {           /* the top level: Esc quits */
        *quit = 1;
        return;
    }
    if (a->nshown > 0 && (is_key(ev, TUI_KEY_ENTER, 0) || is_key(ev, TUI_KEY_TAB, 0) ||
                          is_key(ev, TUI_KEY_RIGHT, 0) || TUI_EV_IS_CHAR(ev, 'l'))) {
        a->focus = FOCUS_BODY;
        return;
    }
    if (tui_listview_handle(&a->list, ev)) show_selected(a);
}

/* One key; 1 when the program should end. */
static int handle_key(app *a, const tui_event *ev) {
    if (TUI_EV_IS_CTRL(ev, 'c')) return 1;
    if (TUI_EV_IS_CTRL(ev, 'z')) {
        raise(SIGTSTP);                          /* tui_term gives the terminal back */
        return 0;
    }
    if (a->rows < MIN_ROWS || a->cols < MIN_COLS) return is_key(ev, TUI_KEY_ESC, 0);
    a->status[0] = '\0';                         /* a message lasts until the next key */
    if (a->dialog_open) {
        if (tui_dialog_handle(&a->dialog, ev) != TUI_DIALOG_OPEN) dialog_closed(a);
        return 0;
    }
    if (a->composing) {
        compose_key(a, ev);
        return 0;
    }
    int quit = 0;
    reader_key(a, ev, &quit);
    return quit;
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
        perror(PROGRAM ": terminal");
        return 1;
    }

    static app a;
    memset(&a, 0, sizeof(a));
    a.open = -1;
    tui_listview_init(&a.list);
    tui_textview_init(&a.body);
    apply_filter(&a, 0);

    if (tui_term_size(t, &a.rows, &a.cols) != 0) {
        a.rows = MIN_ROWS;
        a.cols = MIN_COLS;
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
