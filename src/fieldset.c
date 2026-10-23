/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* The form: fields, focus, a completion drop-down and validation. */

/*
 * The form: fields, focus, a completion drop-down, validation.
 * Completion follows email-cli's compose dialog: the token is the text
 * after the last separator before the cursor, leading spaces skipped, and
 * an accepted candidate replaces exactly that token.
 */

#include "tuikit/fieldset.h"
#include "internal.h"
#include "tuikit/text.h"

#include <string.h>

void tui_field_text(tui_field *fl, const char *label, char *buf, size_t size,
                         const char *initial) {
    if (!fl) return;
    memset(fl, 0, sizeof(*fl));
    fl->kind = TUI_FIELD_TEXT;
    fl->label = label;
    tui_lineedit_init(&fl->edit, buf, size, initial);
}

void tui_field_choice(tui_field *fl, const char *label,
                           const char *const *choices, int n, int selected) {
    if (!fl) return;
    memset(fl, 0, sizeof(*fl));
    fl->kind = TUI_FIELD_CHOICE;
    fl->label = label;
    fl->choices = choices;
    fl->nchoices = choices && n > 0 ? n : 0;
    if (selected < 0) selected = 0;
    if (selected >= fl->nchoices) selected = fl->nchoices > 0 ? fl->nchoices - 1 : 0;
    fl->choice = selected;
}

void tui_fieldset_init(tui_fieldset *f, tui_field *fields, int n) {
    if (!f) return;
    memset(f, 0, sizeof(*f));
    f->fields = fields;
    f->nfields = fields && n > 0 ? n : 0;
    f->state = TUI_DIALOG_OPEN;
    /* The palette's roles (policies/tui-interface-model.md). */
    f->label_style = tui_scheme_style(TUI_ROLE_TEXT);
    f->focus_style = tui_scheme_style(TUI_ROLE_ACCENT);
    f->value_style = tui_scheme_style(TUI_ROLE_INPUT);
    f->cand_style = tui_scheme_style(TUI_ROLE_TEXT);
    f->cand_sel_style = tui_selection_style();
    f->error_style = tui_scheme_style(TUI_ROLE_ERROR);
}

void tui_fieldset_free(tui_fieldset *f) {
    if (!f) return;
    for (int i = 0; i < f->nfields; i++)
        if (f->fields[i].kind == TUI_FIELD_TEXT) tui_lineedit_free(&f->fields[i].edit);
}

static tui_field *focused(tui_fieldset *f) {
    if (f->nfields == 0) return NULL;
    return &f->fields[f->focus];
}

void tui_fieldset_focus(tui_fieldset *f, int i) {
    if (!f || f->nfields == 0) return;
    if (i < 0) i = 0;
    if (i >= f->nfields) i = f->nfields - 1;
    f->focus = i;
    f->ncand = 0;
}

void tui_fieldset_set_error(tui_fieldset *f, const char *msg) {
    if (!f) return;
    f->error[0] = '\0';
    if (msg) {
        size_t n = strlen(msg);
        if (n >= sizeof(f->error)) n = sizeof(f->error) - 1;
        memcpy(f->error, msg, n);
        f->error[n] = '\0';
    }
}

/* Start of the token being typed in a text field: after the last
 * separator before the cursor, leading spaces skipped. */
static size_t token_start(const tui_field *fl) {
    const tui_lineedit *le = &fl->edit;
    size_t start = 0;
    if (fl->separators) {
        for (size_t i = le->cur; i > 0; i--) {
            if (strchr(fl->separators, le->buf[i - 1])) {
                start = i;
                break;
            }
        }
    }
    while (start < le->cur && le->buf[start] == ' ') start++;
    return start;
}

void tui_fieldset_update_completion(tui_fieldset *f) {
    if (!f) return;
    f->ncand = 0;
    f->cand_sel = -1;                  /* nothing chosen until Down */
    tui_field *fl = focused(f);
    if (!fl || fl->kind != TUI_FIELD_TEXT || !fl->complete || !fl->edit.buf) return;
    size_t start = token_start(fl);
    char tok[512];
    size_t n = fl->edit.cur - start;
    if (n >= sizeof(tok)) n = sizeof(tok) - 1;
    memcpy(tok, fl->edit.buf + start, n);
    tok[n] = '\0';
    int got = fl->complete(f->user, f->focus, tok, f->cand, TUI_FIELDSET_CAND_MAX);
    if (got < 0) got = 0;
    if (got > TUI_FIELDSET_CAND_MAX) got = TUI_FIELDSET_CAND_MAX;
    f->ncand = got;
}

static void accept_candidate(tui_fieldset *f) {
    tui_field *fl = focused(f);
    const char *text = f->cand[f->cand_sel].text;
    tui_lineedit_replace(&fl->edit, token_start(fl), fl->edit.cur, text ? text : "");
    f->ncand = 0;
}

/* Tab with the drop-down open: the token becomes the longest prefix every
 * candidate shares, when that is longer than what was typed.  Cut on a
 * character boundary. */
static void insert_common_prefix(tui_fieldset *f) {
    tui_field *fl = focused(f);
    const char *first = f->cand[0].text ? f->cand[0].text : "";
    size_t n = strlen(first);
    for (int i = 1; i < f->ncand; i++) {
        const char *t = f->cand[i].text ? f->cand[i].text : "";
        size_t k = 0;
        while (k < n && t[k] == first[k]) k++;
        n = k;
    }
    while (n > 0 && ((unsigned char)first[n] & 0xC0) == 0x80) n--;
    size_t start = token_start(fl);
    if (n <= fl->edit.cur - start) return;
    char prefix[512];
    if (n >= sizeof(prefix)) n = sizeof(prefix) - 1;
    memcpy(prefix, first, n);
    prefix[n] = '\0';
    int complete = f->ncand == 1;      /* the one candidate, written out */
    tui_lineedit_replace(&fl->edit, start, fl->edit.cur, prefix);
    if (complete) f->ncand = 0;
    else tui_fieldset_update_completion(f);
}

static void move_focus(tui_fieldset *f, int dir) {
    if (f->nfields == 0) return;
    tui_fieldset_focus(f, (f->focus + dir + f->nfields) % f->nfields);
}

static tui_dialog_state submit(tui_fieldset *f) {
    if (f->validate) {
        char err[sizeof(f->error)];
        err[0] = '\0';
        int to = f->validate(f->user, f, err, sizeof(err));
        if (to >= 0) {
            tui_fieldset_set_error(f, err);
            tui_fieldset_focus(f, to);
            return f->state;
        }
    }
    f->state = TUI_DIALOG_OK;
    return f->state;
}

tui_dialog_state tui_fieldset_handle(tui_fieldset *f, const tui_event *ev) {
    if (!f || !ev) return f ? f->state : TUI_DIALOG_CANCELLED;
    if (f->state != TUI_DIALOG_OPEN) return f->state;
    tui_field *fl = focused(f);
    int key = ev->type == TUI_EV_KEY;
    int plain = key && ev->mods == 0;
    int cancel = key && (ev->key == TUI_KEY_ESC || TUI_EV_IS_CTRL(ev, 'c'));

    if (f->ncand > 0) {
        /* Down moves onto the list, Up back and, from its first row, into
         * the field again; Enter takes a chosen candidate only; Tab fills
         * in what the candidates share.  (Decided by the human, TUI-17.) */
        if (plain && ev->key == TUI_KEY_DOWN) {
            if (f->cand_sel < f->ncand - 1) f->cand_sel++;
            return f->state;
        }
        if (plain && ev->key == TUI_KEY_UP) {
            if (f->cand_sel >= 0) f->cand_sel--;
            return f->state;
        }
        if (plain && ev->key == TUI_KEY_ENTER && f->cand_sel >= 0) {
            accept_candidate(f);
            return f->state;
        }
        if (plain && ev->key == TUI_KEY_TAB) {
            insert_common_prefix(f);
            return f->state;
        }
        if (cancel) {                 /* closes the drop-down, keeps the text */
            f->ncand = 0;
            return f->state;
        }
    }
    if (cancel) {
        f->state = TUI_DIALOG_CANCELLED;
        return f->state;
    }
    if (key && ev->key == TUI_KEY_ENTER) return submit(f);
    if ((plain && (ev->key == TUI_KEY_TAB || ev->key == TUI_KEY_DOWN))) {
        move_focus(f, +1);
        return f->state;
    }
    if ((key && ev->key == TUI_KEY_TAB && (ev->mods & TUI_MOD_SHIFT)) ||
        (plain && ev->key == TUI_KEY_UP)) {
        move_focus(f, -1);
        return f->state;
    }
    if (!fl) return f->state;
    if (fl->kind == TUI_FIELD_CHOICE) {
        if (fl->nchoices > 0) {
            if (plain && ev->key == TUI_KEY_LEFT)
                fl->choice = (fl->choice + fl->nchoices - 1) % fl->nchoices;
            else if ((plain && ev->key == TUI_KEY_RIGHT) || TUI_EV_IS_CHAR(ev, ' '))
                fl->choice = (fl->choice + 1) % fl->nchoices;
        }
        return f->state;
    }
    if (tui_lineedit_handle(&fl->edit, ev) == TUI_EDIT_CHANGED) {
        f->error[0] = '\0';
        tui_fieldset_update_completion(f);
    }
    return f->state;
}

int tui_fieldset_render(tui_fieldset *f, tui_canvas c) {
    if (!f || !c.screen) return 0;
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    int label_w = 0;
    for (int i = 0; i < f->nfields; i++) {
        int w = f->fields[i].label ? tui_str_width(f->fields[i].label) : 0;
        if (w > label_w) label_w = w;
    }
    if (label_w > 0) label_w++;                  /* a space after the labels */
    if (label_w > cols) label_w = cols;
    tui_screen_set_cursor(c.screen, 0, 0, 0);

    int r = 0;
    for (int i = 0; i < f->nfields && r < rows; i++, r++) {
        tui_field *fl = &f->fields[i];
        int is_focus = i == f->focus;
        tui_canvas row = tui_canvas_sub(c, tui_rect_make(r, 0, 1, cols));
        tui_canvas_clear(row, f->value_style);
        if (fl->label) tui_canvas_put(row, 0, 0, fl->label, is_focus ? f->focus_style : f->label_style);
        tui_canvas value = tui_canvas_sub(row, tui_rect_make(0, label_w, 1, cols - label_w));
        if (fl->kind == TUI_FIELD_CHOICE) {
            const char *text = fl->nchoices > 0 && fl->choices[fl->choice] ? fl->choices[fl->choice] : "";
            if (is_focus) {
                int w = tui_canvas_put(value, 0, 0, "< ", f->focus_style);
                w += tui_canvas_put(value, 0, w, text, f->focus_style);
                tui_canvas_put(value, 0, w, " >", f->focus_style);
            } else {
                tui_canvas_put(value, 0, 0, text, f->value_style);
            }
        } else if (is_focus) {
            tui_lineedit_render(&fl->edit, value, NULL, tui__input_style(f->value_style, 1));
        } else if (fl->edit.buf) {
            tui_canvas_put(value, 0, 0, fl->edit.buf, f->value_style);
        }
        if (is_focus) {
            for (int k = 0; k < f->ncand && r + 1 < rows; k++) {
                r++;
                tui_canvas line = tui_canvas_sub(c, tui_rect_make(r, 0, 1, cols));
                tui_canvas_clear(line, f->value_style);
                int chosen = k == f->cand_sel;
                tui_style st = chosen ? f->cand_sel_style : f->cand_style;
                /* The palette's selection is the table's: dark blue over
                 * the item row and '>' where the others have blanks. */
                if (chosen && tui__is_default_selection(st))
                    st = tui__selection_compose(tui__selection_base(tui__colors_from_env()),
                                                f->value_style, TUI_STYLE_DEFAULT, 1);
                const char *shown = f->cand[k].label ? f->cand[k].label : f->cand[k].text;
                tui_canvas item = tui_canvas_sub(line, tui_rect_make(0, label_w, 1, cols - label_w));
                tui_canvas_clear(item, st);
                tui_style mark = st;
                if (chosen) mark.attrs = (uint16_t)(mark.attrs | TUI_ATTR_BOLD);
                tui_canvas_put(item, 0, 0, chosen ? ">" : " ", mark);
                tui_canvas_put(item, 0, 2, shown ? shown : "", st);
            }
        }
    }
    if (f->error[0] && r < rows) {
        tui_canvas line = tui_canvas_sub(c, tui_rect_make(r, 0, 1, cols));
        tui_canvas_clear(line, f->value_style);
        tui_canvas_put_aligned(line, 0, f->error, TUI_ALIGN_LEFT, f->error_style);
        r++;
    }
    return r;
}
