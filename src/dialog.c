/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* The modal dialogs: message, confirm, prompt, menu, checklist and help. */

#include "tuikit/dialog.h"
#include "tuikit/text.h"
#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *dup_or_empty(const char *s) {
    size_t n = s ? strlen(s) : 0;
    char *p = malloc(n + 1);
    if (!p) return NULL;
    if (n) memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

static int setup(tui_dialog *d, tui_dialog_kind kind, const char *title, const char *text) {
    if (!d) {
        errno = EINVAL;
        return -1;
    }
    memset(d, 0, sizeof(*d));
    d->kind = kind;
    d->state = TUI_DIALOG_OPEN;
    d->frame = tui_scheme_style(TUI_ROLE_FRAME);   /* the scheme's roles */
    d->fill = tui_scheme_style(TUI_ROLE_TEXT);
    d->focus = tui_selection_style();
    tui_textview_init(&d->body);
    tui_listview_init(&d->list);
    d->title = dup_or_empty(title);
    d->text = dup_or_empty(text);
    if (!d->title || !d->text || tui_textview_set_text(&d->body, d->text) != 0) {
        tui_dialog_free(d);
        errno = ENOMEM;
        return -1;
    }
    return 0;
}

int tui_dialog_message(tui_dialog *d, const char *title, const char *text) {
    return setup(d, TUI_DIALOG_MESSAGE, title, text);
}

int tui_dialog_confirm(tui_dialog *d, const char *title, const char *text, int default_yes) {
    if (setup(d, TUI_DIALOG_CONFIRM, title, text) != 0) return -1;
    d->answer = default_yes ? 1 : 0;
    return 0;
}

int tui_dialog_prompt(tui_dialog *d, const char *title, const char *text,
                      char *buf, size_t size, const char *initial) {
    if (!buf || size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (setup(d, TUI_DIALOG_PROMPT, title, text) != 0) return -1;
    tui_lineedit_init(&d->edit, buf, size, initial);
    return 0;
}

int tui_dialog_menu(tui_dialog *d, const char *title, const char *const *items, int n,
                    int selected) {
    if ((!items && n > 0) || n < 0) {
        errno = EINVAL;
        return -1;
    }
    if (setup(d, TUI_DIALOG_MENU, title, NULL) != 0) return -1;
    d->items = items;
    d->nitems = n;
    tui_listview_set_count(&d->list, n);
    tui_listview_select(&d->list, selected);
    d->answer = d->list.selected;
    return 0;
}

int tui__dialog_quickmenu(tui_dialog *d, const char *title, const char *const *items, int n,
                          int selected) {
    if (tui_dialog_menu(d, title, items, n, selected) != 0) return -1;
    d->kind = TUI_DIALOG_QUICKMENU;
    return 0;
}

int tui_dialog_checklist(tui_dialog *d, const char *title, const char *const *items,
                         int n, const unsigned char *initial, int selected) {
    if (tui_dialog_menu(d, title, items, n, selected) != 0) return -1;
    d->kind = TUI_DIALOG_CHECKLIST;
    d->checked = calloc(n > 0 ? (size_t)n : 1, 1);
    if (!d->checked) {
        tui_dialog_free(d);
        errno = ENOMEM;
        return -1;
    }
    for (int i = 0; initial && i < n; i++) d->checked[i] = initial[i] ? 1 : 0;
    return 0;
}

/* A help row's key or description; a NULL one is drawn empty. */
static const char *help_str(const tui_dialog *d, int i) {
    return d->items[i] ? d->items[i] : "";
}

/* ── The search of a help dialog (TUI-59) ────────────────────────────────
 * '/' opens a one-line search.  Its buffer is the dialog's @c checked array
 * (a help has no ticks), the editor is @c edit, and @c answer is 1 while the
 * line has the keyboard.  The unit is an entry (a key and what it does): it
 * is shown whole when every typed word is in its key or in its text, in
 * either case, and not at all otherwise. */
#define HELP_QUERY_MAX 128

static const char *help_query(const tui_dialog *d) {
    return d->kind == TUI_DIALOG_HELP && d->checked ? (const char *)d->checked : "";
}

static int help_ci_find(const char *hay, const char *needle, size_t nlen) {
    size_t hl = strlen(hay);
    for (size_t i = 0; i + nlen <= hl; i++) {
        size_t j = 0;
        while (j < nlen && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) j++;
        if (j == nlen) return (int)i;
    }
    return -1;
}

/* The next word of @p q: its start and length, advancing @p q; 0 at the end. */
static int help_word(const char **q, const char **w, size_t *n) {
    while (**q == ' ' || **q == '\t') (*q)++;
    if (!**q) return 0;
    *w = *q;
    while (**q && **q != ' ' && **q != '\t') (*q)++;
    *n = (size_t)(*q - *w);
    return 1;
}

static int help_matches(const tui_dialog *d, int i) {
    const char *q = help_query(d), *w;
    size_t n;
    while (help_word(&q, &w, &n))
        if (help_ci_find(help_str(d, 2 * i), w, n) < 0 && help_ci_find(help_str(d, 2 * i + 1), w, n) < 0)
            return 0;
    return 1;
}

/* How many entries are shown, and which entry is the @p k-th shown. */
static int help_shown(const tui_dialog *d) {
    int c = 0;
    for (int i = 0; i < d->nitems; i++) c += help_matches(d, i);
    return c;
}

static int help_nth(const tui_dialog *d, int k) {
    int i = 0;
    while (i < d->nitems && !(help_matches(d, i) && k-- == 0)) i++;
    return i;     /* callers ask for k below help_shown() */
}

static int help_searching(const tui_dialog *d) { return d->answer == 1; }
static int help_filtered(const tui_dialog *d) { return help_query(d)[0] != '\0'; }

/* Opens the line (allocating its buffer the first time); the old text stays. */
static void help_search_open(tui_dialog *d) {
    if (!d->checked) {
        d->checked = calloc(HELP_QUERY_MAX, 1);
        if (!d->checked) return;
        tui_lineedit_init(&d->edit, (char *)d->checked, HELP_QUERY_MAX, "");
    }
    d->answer = 1;
}

static void help_search_clear(tui_dialog *d) {
    d->answer = 0;
    if (d->checked) tui_lineedit_set_text(&d->edit, "");
    d->list.scroll_top = 0;
}

int tui_dialog_help(tui_dialog *d, const char *title, const char *const *pairs, int n) {
    if ((!pairs && n > 0) || n < 0) {
        errno = EINVAL;
        return -1;
    }
    if (setup(d, TUI_DIALOG_HELP, title, NULL) != 0) return -1;
    d->items = pairs;
    d->nitems = n;
    tui_listview_set_count(&d->list, n);
    return 0;
}

int tui__dialog_help_text(tui_dialog *d, const char *text) {
    char *copy = dup_or_empty(text);
    if (!copy) return -1;
    if (tui_textview_set_text(&d->body, copy) != 0) {
        free(copy);
        return -1;
    }
    free(d->text);
    d->text = copy;
    return 0;
}

int tui_dialog_is_checked(const tui_dialog *d, int i) {
    if (!d || d->kind != TUI_DIALOG_CHECKLIST || !d->checked || i < 0 || i >= d->nitems) return 0;
    return d->checked[i];
}

void tui_dialog_free(tui_dialog *d) {
    if (!d) return;
    free(d->checked);
    d->checked = NULL;
    free(d->title);
    free(d->text);
    d->title = d->text = NULL;
    tui_textview_free(&d->body);
    tui_lineedit_free(&d->edit);
}

static tui_dialog_state close_with(tui_dialog *d, tui_dialog_state s) {
    d->state = s;
    return s;
}

tui_dialog_state tui_dialog_handle(tui_dialog *d, const tui_event *ev) {
    if (!d || !ev) return d ? d->state : TUI_DIALOG_CANCELLED;
    if (d->state != TUI_DIALOG_OPEN) return d->state;
    int is_key = ev->type == TUI_EV_KEY;
    int cancel = is_key && (ev->key == TUI_KEY_ESC || TUI_EV_IS_CTRL(ev, 'c'));

    switch (d->kind) {
    case TUI_DIALOG_MESSAGE:
        if (cancel || (is_key && ev->key == TUI_KEY_ENTER) || TUI_EV_IS_CHAR(ev, ' '))
            return close_with(d, TUI_DIALOG_OK);
        break;
    case TUI_DIALOG_CONFIRM:
        if (cancel) {
            d->answer = 0;
            return close_with(d, TUI_DIALOG_CANCELLED);
        }
        if (TUI_EV_IS_CHAR(ev, 'y') || TUI_EV_IS_CHAR(ev, 'Y')) {
            d->answer = 1;
            return close_with(d, TUI_DIALOG_OK);
        }
        if (TUI_EV_IS_CHAR(ev, 'n') || TUI_EV_IS_CHAR(ev, 'N')) {
            d->answer = 0;
            return close_with(d, TUI_DIALOG_OK);
        }
        if (is_key && ev->key == TUI_KEY_ENTER) return close_with(d, TUI_DIALOG_OK);
        if (is_key && (ev->key == TUI_KEY_LEFT || ev->key == TUI_KEY_RIGHT ||
                       ev->key == TUI_KEY_TAB))
            d->answer = !d->answer;
        break;
    case TUI_DIALOG_PROMPT: {
        tui_edit_result r = tui_lineedit_handle(&d->edit, ev);
        if (r == TUI_EDIT_ACCEPT) return close_with(d, TUI_DIALOG_OK);
        if (r == TUI_EDIT_CANCEL) return close_with(d, TUI_DIALOG_CANCELLED);
        break;
    }
    case TUI_DIALOG_CHECKLIST:
        if (TUI_EV_IS_CHAR(ev, ' ') && d->nitems > 0) {
            d->checked[d->list.selected] ^= 1;
            return d->state;
        }
        /* Everything else is the menu's. */
        /* FALLTHROUGH */
    case TUI_DIALOG_MENU:
    case TUI_DIALOG_QUICKMENU:
        if (cancel) return close_with(d, TUI_DIALOG_CANCELLED);
        if (is_key && ev->key == TUI_KEY_ENTER && d->nitems > 0) {
            d->answer = d->list.selected;
            return close_with(d, TUI_DIALOG_OK);
        }
        tui_listview_handle(&d->list, ev);
        d->answer = d->list.selected;
        break;
    case TUI_DIALOG_HELP: {
        /* Back as from any view: Esc, q, Backspace (and Ctrl-C, as in
         * every dialog).  The movement keys scroll; nothing else does
         * anything (TUI-18).  '/' searches (TUI-59): Esc clears the search
         * first and closes the help on the next press, Enter leaves the
         * line and keeps the narrowed list. */
        if (is_key && TUI_EV_IS_CTRL(ev, 'c')) return close_with(d, TUI_DIALOG_OK);
        if (help_searching(d)) {
            if (is_key && ev->key == TUI_KEY_ESC) {
                help_search_clear(d);
                return d->state;
            }
            tui_edit_result r = tui_lineedit_handle(&d->edit, ev);
            if (r == TUI_EDIT_ACCEPT) {
                d->answer = 0;
                return d->state;
            }
            if (r != TUI_EDIT_IGNORED) {
                d->list.scroll_top = 0;
                return d->state;
            }
        } else {
            if (is_key && ev->key == TUI_KEY_ESC && help_filtered(d)) {
                help_search_clear(d);
                return d->state;
            }
            if (cancel || TUI_EV_IS_CHAR(ev, 'q') ||
                (is_key && ev->key == TUI_KEY_BACKSPACE && ev->mods == 0))
                return close_with(d, TUI_DIALOG_OK);
            if (TUI_EV_IS_CHAR(ev, '/')) {
                help_search_open(d);
                return d->state;
            }
        }
        if (d->list.rows_visible <= 0) return d->state;   /* not drawn yet */
        int max_top = help_shown(d) - d->list.rows_visible;
        if (max_top < 0) max_top = 0;
        int page = d->list.rows_visible > 1 ? d->list.rows_visible - 1 : 1;
        int top = d->list.scroll_top;
        switch (tui__nav_key(ev)) {
        case TUI_KEY_UP:   top--; break;
        case TUI_KEY_DOWN: top++; break;
        case TUI_KEY_PGUP: top -= page; break;
        case TUI_KEY_PGDN: top += page; break;
        case TUI_KEY_HOME: top = 0; break;
        case TUI_KEY_END:  top = max_top; break;
        default:           return d->state;
        }
        d->list.scroll_top = top < 0 ? 0 : top > max_top ? max_top : top;
        break;
    }
    }
    return d->state;
}

/* ── Drawing ─────────────────────────────────────────────────────────── */

static int text_width(const char *s) {
    int widest = 0;
    while (s && *s) {
        const char *nl = strchr(s, '\n');
        size_t n = nl ? (size_t)(nl - s) : strlen(s);
        int w = tui_strn_width(s, n);
        if (w > widest) widest = w;
        s = nl ? nl + 1 : s + n;
    }
    return widest;
}

typedef struct {
    tui_dialog *d;
} menu_ctx;

/* A menu or checklist row: the palette's selection is the table's (the
 * whole row in it, '>' in the first column, the text from the third); a
 * style the program set in focus is its own choice and is kept.  The two
 * columns are there on every row, and the item width counts them. */
static void draw_item(void *ctx, tui_canvas row, int index, int selected) {
    tui_dialog *d = ((menu_ctx *)ctx)->d;
    tui_style st = d->fill;
    if (selected) {
        if (tui__is_default_selection(d->focus))
            st = tui__selection_compose(tui__selection_base(tui__colors_from_env()), d->fill,
                                        tui_style_attrs(0), 1);
        else
            st = d->focus;
    }
    tui_canvas_clear(row, st);
    if (selected) {
        tui_style mark = st;
        mark.attrs = (uint16_t)(mark.attrs | TUI_ATTR_BOLD);
        tui_canvas_put(row, 0, 0, ">", mark);
    }
    row = tui_canvas_sub(row, tui_rect_make(0, 2, 1, tui_canvas_cols(row) - 2));
    if (d->kind == TUI_DIALOG_CHECKLIST) {
        tui_canvas_put(row, 0, 0, d->checked[index] ? "[x] " : "[ ] ", st);
        row = tui_canvas_sub(row, tui_rect_make(0, 4, 1, tui_canvas_cols(row) - 4));
    }
    if (d->kind == TUI_DIALOG_QUICKMENU) {
        /* The first cell of the item is its quick key, in the accent colour
         * (the selected row keeps the selection's colours, in bold). */
        tui_style qk = st;
        qk.attrs = (uint16_t)(qk.attrs | TUI_ATTR_BOLD);
        if (!selected) qk.fg = tui_scheme_style(TUI_ROLE_ACCENT).fg;
        char letter[2] = { d->items[index][0], '\0' };
        tui_canvas_put(row, 0, 0, letter, qk);
        row = tui_canvas_sub(row, tui_rect_make(0, 1, 1, tui_canvas_cols(row) - 1));
        tui_canvas_put_aligned(row, 0, d->items[index] + 1, TUI_ALIGN_LEFT, st);
        return;
    }
    tui_canvas_put_aligned(row, 0, d->items[index] ? d->items[index] : "", TUI_ALIGN_LEFT, st);
}

/* Draws the typed words again, in @p hl, over @p text already drawn at the
 * start of @p row (the span that matches, in either case). */
static void help_highlight(const tui_dialog *d, tui_canvas row, const char *text, tui_style base,
                           tui_style hl, int col0) {
    (void)base;
    const char *q = help_query(d), *w;
    size_t n;
    while (help_word(&q, &w, &n)) {
        size_t from = 0;
        int at;
        while ((at = help_ci_find(text + from, w, n)) >= 0) {
            size_t off = from + (size_t)at;
            char part[HELP_QUERY_MAX + 1];
            memcpy(part, text + off, n);
            part[n] = '\0';
            tui_canvas_put(row, 0, col0 + tui_strn_width(text, off), part, hl);
            from = off + n;
        }
    }
}

void tui_dialog_render(tui_dialog *d, tui_canvas c) {
    if (!d || !c.screen) return;
    /* A margin of one cell around the dialog; on a tiny canvas the box is
     * clamped to the canvas by tui_rect_center(). */
    int avail_w = tui_canvas_cols(c) - 2, avail_h = tui_canvas_rows(c) - 2;

    /* Width: the widest of the text, the title, the buttons, the items. */
    int want = text_width(d->text);
    if (d->kind == TUI_DIALOG_HELP && want > TUI__HELP_TEXT_COLS) want = TUI__HELP_TEXT_COLS;
    int tw = tui_str_width(d->title) + 4;
    if (tw > want) want = tw;
    if (d->kind == TUI_DIALOG_MESSAGE && want < 6) want = 6;
    if (d->kind == TUI_DIALOG_CONFIRM && want < 15) want = 15;
    if (d->kind == TUI_DIALOG_PROMPT && want < 30) want = 30;
    int key_w = 0;
    if (d->kind == TUI_DIALOG_HELP) {
        int what_w = 0;
        for (int i = 0; i < d->nitems; i++) {
            int k = tui_str_width(help_str(d, 2 * i)), t = tui_str_width(help_str(d, 2 * i + 1));
            if (k > key_w) key_w = k;
            if (t > what_w) what_w = t;
        }
        if (key_w + 2 + what_w > want) want = key_w + 2 + what_w;
    }
    if (d->kind == TUI_DIALOG_MENU || d->kind == TUI_DIALOG_QUICKMENU ||
        d->kind == TUI_DIALOG_CHECKLIST) {
        int mark = d->kind == TUI_DIALOG_CHECKLIST ? 4 : 0;      /* "[x] " */
        for (int i = 0; i < d->nitems; i++) {
            int w = tui_str_width(d->items[i]) + 2 + mark;
            if (w > want) want = w;
        }
    }
    int inner_w = want;
    if (inner_w > avail_w - 4) inner_w = avail_w - 4;
    if (inner_w < 1) inner_w = 1;

    /* Height: the frame, the text, then what the kind adds below it. */
    int lines = d->text[0] ? tui_textview_wrap(&d->body, inner_w) : 0;
    if (lines < 0) lines = 0;
    /* The description of a view is one to three lines, then a blank row. */
    if (d->kind == TUI_DIALOG_HELP && lines > 3) lines = 3;
    int help_text = d->kind == TUI_DIALOG_HELP && lines > 0 ? lines + 1 : 0;
    int extra = 0;
    switch (d->kind) {
    case TUI_DIALOG_MESSAGE:
    case TUI_DIALOG_CONFIRM: extra = lines > 0 ? 2 : 1; break;   /* blank + buttons */
    case TUI_DIALOG_PROMPT:  extra = 1; break;                     /* the input line */
    case TUI_DIALOG_MENU:
    case TUI_DIALOG_QUICKMENU:
    case TUI_DIALOG_CHECKLIST: extra = d->nitems > 0 ? d->nitems : 1; break;
    case TUI_DIALOG_HELP:    extra = (d->nitems > 0 ? d->nitems : 1) +
                                     (help_searching(d) || help_filtered(d) ? 1 : 0);
                             break;                  /* + the search line */
    }
    int h = 2 + (d->kind == TUI_DIALOG_HELP ? help_text : lines) + extra;
    if (h > avail_h) h = avail_h;
    int w = inner_w + 4;

    tui_rect box = tui_rect_center(tui_canvas_rect(c), h, w);
    tui_canvas_fill(c, box, d->fill);
    tui_canvas_box(c, box, d->title, d->frame);
    tui_rect content = tui_rect_make(box.row + 1, box.col + 2, box.rows - 2, box.cols - 4);
    tui_canvas cc = tui_canvas_sub(c, content);
    int rows = tui_canvas_rows(cc);
    /* A menu has no text: its list takes every row. */
    int is_list = d->kind == TUI_DIALOG_MENU || d->kind == TUI_DIALOG_QUICKMENU ||
                  d->kind == TUI_DIALOG_CHECKLIST ||
                  d->kind == TUI_DIALOG_HELP;
    int below = is_list ? rows : extra;
    if (d->kind == TUI_DIALOG_HELP) {
        /* The description keeps its rows, but at least one row stays for the keys. */
        int keep = help_text < rows - 1 ? help_text : (rows > 1 ? rows - 1 : 0);
        below = rows - keep;
    }
    if (below > rows) below = rows;
    /* Short of rows, the blank line above the buttons goes first: the
     * text keeps at least one row. */
    if (!is_list && lines > 0 && rows - below < 1 && below > 1) below--;
    int text_rows = rows - below;
    if (text_rows > 0 && lines > 0 && d->kind == TUI_DIALOG_HELP) {
        int shown = text_rows > lines ? lines : text_rows;
        tui_textview_render(&d->body, tui_canvas_sub(cc, tui_rect_make(0, 0, shown, inner_w)),
                            d->fill);
    } else if (text_rows > 0 && lines > 0)
        tui_textview_render(&d->body, tui_canvas_sub(cc, tui_rect_make(0, 0, text_rows, inner_w)),
                            d->fill);

    tui_canvas last = tui_canvas_sub(cc, tui_rect_make(rows - 1, 0, 1, inner_w));
    tui_screen_set_cursor(c.screen, 0, 0, 0);
    switch (d->kind) {
    case TUI_DIALOG_MESSAGE:
        tui_canvas_put_aligned(last, 0, "[ OK ]", TUI_ALIGN_CENTER, tui__button_focus(d->focus));
        break;
    case TUI_DIALOG_CONFIRM: {
        int start = (tui_canvas_cols(last) - 15) / 2;
        if (start < 0) start = 0;
        tui_canvas_put(last, 0, start, "[ Yes ]", d->answer ? tui__button_focus(d->focus) : d->fill);
        tui_canvas_put(last, 0, start + 9, "[ No ]", d->answer ? d->fill : tui__button_focus(d->focus));
        break;
    }
    case TUI_DIALOG_PROMPT:
        tui_lineedit_render(&d->edit, last, NULL, tui__input_style(tui_scheme_style(TUI_ROLE_INPUT), 1));
        break;
    case TUI_DIALOG_MENU:
    case TUI_DIALOG_QUICKMENU:
    case TUI_DIALOG_CHECKLIST: {
        menu_ctx ctx = { d };
        tui_canvas list = tui_canvas_sub(cc, tui_rect_make(text_rows, 0, below, inner_w));
        tui_listview_render(&d->list, list, d->fill, draw_item, &ctx);
        break;
    }
    case TUI_DIALOG_HELP: {
        /* Two columns, the keys in bold; the window is the dialog's own
         * (it has no cursor), kept within the rows it has now.  A search
         * adds its line at the bottom and shows the matching entries only,
         * the matched words highlighted. */
        int sline = help_searching(d) || help_filtered(d);
        int lrows = below - (sline && below > 1 ? 1 : 0);
        tui_style key_st = d->fill;
        key_st.attrs |= TUI_ATTR_BOLD;
        tui_style hl_st = d->fill;
        hl_st.fg = d->frame.fg;
        hl_st.attrs |= TUI_ATTR_BOLD | TUI_ATTR_UNDERLINE;
        int shown = help_shown(d);
        d->list.rows_visible = lrows;
        int max_top = shown - lrows;
        if (d->list.scroll_top > max_top) d->list.scroll_top = max_top > 0 ? max_top : 0;
        for (int r = 0; r < lrows && d->list.scroll_top + r < shown; r++) {
            int i = help_nth(d, d->list.scroll_top + r);
            tui_canvas row = tui_canvas_sub(cc, tui_rect_make(text_rows + r, 0, 1, inner_w));
            tui_canvas what = tui_canvas_sub(row, tui_rect_make(0, key_w + 2, 1, inner_w - key_w - 2));
            tui_canvas_put(row, 0, 0, help_str(d, 2 * i), key_st);
            tui_canvas_put_aligned(what, 0, help_str(d, 2 * i + 1), TUI_ALIGN_LEFT, d->fill);
            if (sline) {
                help_highlight(d, row, help_str(d, 2 * i), key_st, hl_st, 0);
                help_highlight(d, what, help_str(d, 2 * i + 1), d->fill, hl_st, 0);
            }
        }
        if (shown == 0 && help_filtered(d) && lrows > 0) {
            char none[HELP_QUERY_MAX + 32];
            const char *q = help_query(d);
            while (*q == ' ' || *q == '\t') q++;
            snprintf(none, sizeof none, "No key matches \"%s\"", q);
            tui_canvas row = tui_canvas_sub(cc, tui_rect_make(text_rows, 0, 1, inner_w));
            tui_canvas_put_aligned(row, 0, none, TUI_ALIGN_LEFT, d->fill);
        }
        if (sline && below > 1) {
            tui_canvas row = tui_canvas_sub(cc, tui_rect_make(rows - 1, 0, 1, inner_w));
            if (help_searching(d)) {
                tui_lineedit_render(&d->edit, row, "/ ", d->fill);
            } else {
                char line[HELP_QUERY_MAX + 4];
                snprintf(line, sizeof line, "/ %s", help_query(d));
                tui_canvas_put_aligned(row, 0, line, TUI_ALIGN_LEFT, d->fill);
            }
        }
        break;
    }
    }
}
