/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

/*
 * tuikit-demo -- a small two-pane message reader built on libtuikit.
 *
 * It is the library's worked example and its consumer check: a list on the
 * left, the selected message on the right, a status row, a search prompt
 * and a quit confirmation, redrawn on resize.  When standard input is not
 * a terminal it prints the subjects as plain text instead.
 *
 * Keys: Up/Down/PgUp/PgDn/Home/End move in the focused pane, Tab switches
 * panes, / searches the subjects, q asks to quit, Ctrl-C quits at once.
 *
 * Build: cc tuikit_demo.c $(pkg-config --cflags --libs libtuikit)
 */

#include <tuikit/tuikit.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *subject;
    const char *body;
} message;

static const message g_messages[] = {
    { "Welcome", "Welcome to the tuikit demo.\n\nUse the arrow keys to move, "
                 "Tab to switch panes, / to search and q to quit." },
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

typedef enum { FOCUS_LIST, FOCUS_BODY } focus_t;

typedef struct {
    tui_listview list;
    tui_textview body;
    focus_t      focus;
    int          shown;          /* message in the body view */
    tui_dialog   dialog;
    int          dialog_open;
    int          dialog_is_search;
    char         query[64];
    char         status[96];
    int          rows, cols;
} app;

static const tui_style st_normal = { TUI_COLOR_DEFAULT, TUI_COLOR_DEFAULT, 0 };

static void show_message(app *a) {
    int i = a->list.selected;
    if (i < 0 || i == a->shown) return;
    tui_textview_set_text(&a->body, g_messages[i].body);
    a->shown = i;
}

static void draw_subject(void *ctx, tui_canvas row, int index, int selected) {
    app *a = ctx;
    tui_style st = st_normal;
    if (selected) {
        st.attrs = a->focus == FOCUS_LIST ? TUI_ATTR_REVERSE : TUI_ATTR_BOLD;
        tui_canvas_clear(row, st);
    }
    tui_canvas_put_aligned(row, 0, g_messages[index].subject, TUI_ALIGN_LEFT, st);
}

static void draw(app *a, tui_screen *scr) {
    tui_canvas all = tui_canvas_of(scr);
    tui_canvas_clear(all, st_normal);
    tui_screen_set_cursor(scr, 0, 0, 0);

    tui_rect top, status, left, right;
    tui_rect_split_v(tui_canvas_rect(all), -1, &top, &status);
    int left_w = a->cols / 3;
    if (left_w < 12) left_w = 12;
    tui_rect_split_h(top, left_w, &left, &right);

    tui_style frame_l = a->focus == FOCUS_LIST ? tui_style_attrs(TUI_ATTR_BOLD) : st_normal;
    tui_style frame_r = a->focus == FOCUS_BODY ? tui_style_attrs(TUI_ATTR_BOLD) : st_normal;
    tui_canvas_box(all, left, "Messages", frame_l);
    tui_canvas_box(all, right, a->shown >= 0 ? g_messages[a->shown].subject : "", frame_r);
    tui_listview_render(&a->list, tui_canvas_sub(all, tui_rect_inset(left, 1)), st_normal,
                        draw_subject, a);
    tui_textview_render(&a->body, tui_canvas_sub(all, tui_rect_inset(right, 1)), st_normal);

    tui_style bar = tui_style_attrs(TUI_ATTR_REVERSE);
    tui_canvas cs = tui_canvas_sub(all, status);
    tui_canvas_clear(cs, bar);
    tui_canvas_put_aligned(cs, 0, a->status[0] ? a->status : "Tab pane  / search  q quit",
                           TUI_ALIGN_LEFT, bar);
    char size[32];
    snprintf(size, sizeof(size), "%dx%d", a->rows, a->cols);
    tui_canvas_put_aligned(cs, 0, size, TUI_ALIGN_RIGHT, bar);

    if (a->dialog_open) tui_dialog_render(&a->dialog, all);
}

/* Applies a closed dialog; 1 when the program should end. */
static int dialog_closed(app *a) {
    int quit = 0;
    if (a->dialog_is_search) {
        if (a->dialog.state == TUI_DIALOG_OK && a->query[0]) {
            int found = -1;
            for (int i = 0; i < N_MESSAGES && found < 0; i++)
                if (strstr(g_messages[i].subject, a->query)) found = i;
            if (found >= 0) {
                tui_listview_select(&a->list, found);
                show_message(a);
                snprintf(a->status, sizeof(a->status), "Found \"%s\"", a->query);
            } else {
                snprintf(a->status, sizeof(a->status), "No match for \"%s\"", a->query);
            }
        }
    } else {
        quit = a->dialog.state == TUI_DIALOG_OK && a->dialog.answer == 1;
    }
    tui_dialog_free(&a->dialog);
    a->dialog_open = 0;
    return quit;
}

/* One key; 1 when the program should end. */
static int handle_key(app *a, const tui_event *ev) {
    if (TUI_EV_IS_CTRL(ev, 'c')) return 1;
    if (a->dialog_open) {
        if (tui_dialog_handle(&a->dialog, ev) != TUI_DIALOG_OPEN) return dialog_closed(a);
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
        a->dialog_is_search = 0;
        return 0;
    }
    if (TUI_EV_IS_CHAR(ev, '/')) {
        tui_dialog_prompt(&a->dialog, "Search", "Subject contains:", a->query,
                          sizeof(a->query), NULL);
        a->dialog_open = 1;
        a->dialog_is_search = 1;
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
    tui_term *t = tui_term_open(&o);
    if (!t) {
        if (errno == ENOTTY) return print_plain();
        perror("tuikit-demo: terminal");
        return 1;
    }

    app a;
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
    tui_textview_free(&a.body);
    tui_buf_free(&out);
    tui_screen_free(scr);
    tui_term_close(t);
    return rc;
}
