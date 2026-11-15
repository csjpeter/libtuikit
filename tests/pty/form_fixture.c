/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* A program for the PTY suite: a form in a modal frame and a masked password prompt. */

/*
 * The stub of TUI-131.  Row 0 shows what was done last.  'f' opens a form
 * dialog (Name, Redirect URL, Type) and 'p' a password prompt; the result
 * goes to row 0 ("form: ..." / "password: ...", the password only as its
 * length and whether it is "secret", never as text).  q quits.
 */

#include "tuikit/tuikit.h"

#include <stdio.h>
#include <string.h>

int main(void) {
    tui_term_opts o;
    tui_term_opts_init(&o);
    tui_term *t = tui_term_open(&o);
    if (!t) {
        perror("form_fixture");
        return 2;
    }
    int rows = 0, cols = 0;
    if (tui_term_size(t, &rows, &cols) != 0) return 2;
    tui_screen *scr = tui_screen_new(rows, cols);
    tui_buf out;
    tui_buf_init(&out);

    char name[24] = "", url[40] = "", pw[24] = "";
    static const char *const kinds[] = { "web", "native" };
    tui_field fields[3];
    tui_fieldset fs;
    tui_dialog_form form;
    tui_dialog pass;
    int form_open = 0, pass_open = 0;
    char done[96] = "done: nothing";

    for (int quit = 0; !quit;) {
        tui_canvas all = tui_canvas_of(scr);
        tui_canvas_clear(all, TUI_STYLE_DEFAULT);
        tui_canvas_put(all, 0, 0, done, TUI_STYLE_OK);
        if (form_open) tui_dialog_form_render(&form, all);
        if (pass_open) tui_dialog_render(&pass, all);
        tui_screen_render(scr, &out);
        tui_term_flush_buf(t, &out);
        tui_event ev;
        if (tui_term_wait(t, -1, NULL, 0, &ev) != 1 || ev.type == TUI_EV_HANGUP) break;
        if (ev.type == TUI_EV_RESIZE) {
            tui_screen_resize(scr, ev.rows, ev.cols);
        } else if (ev.type == TUI_EV_KEY && form_open) {
            tui_dialog_state st = tui_dialog_form_handle(&form, &ev);
            if (st == TUI_DIALOG_OPEN) continue;
            if (st == TUI_DIALOG_OK)
                snprintf(done, sizeof done, "form: %s|%s|%s", name, url, kinds[fields[2].choice]);
            else
                snprintf(done, sizeof done, "form: cancelled");
            tui_dialog_form_free(&form);
            tui_fieldset_free(&fs);
            form_open = 0;
        } else if (ev.type == TUI_EV_KEY && pass_open) {
            tui_dialog_state st = tui_dialog_handle(&pass, &ev);
            if (st == TUI_DIALOG_OPEN) continue;
            if (st == TUI_DIALOG_OK)
                snprintf(done, sizeof done, "password: len=%zu ok=%d", strlen(pw),
                         strcmp(pw, "secret") == 0);
            else
                snprintf(done, sizeof done, "password: cancelled");
            tui_dialog_free(&pass);
            pass_open = 0;
        } else if (ev.type == TUI_EV_KEY) {
            if (TUI_EV_IS_CHAR(&ev, 'q')) quit = 1;
            else if (TUI_EV_IS_CHAR(&ev, 'f')) {
                name[0] = url[0] = '\0';
                tui_field_text(&fields[0], "Name:", name, sizeof name, "");
                tui_field_text(&fields[1], "Redirect URL:", url, sizeof url, "");
                tui_field_choice(&fields[2], "Type:", kinds, 2, 0);
                tui_fieldset_init(&fs, fields, 3);
                if (tui_dialog_form_init(&form, "New client", NULL, &fs) == 0) form_open = 1;
                else tui_fieldset_free(&fs);
            } else if (TUI_EV_IS_CHAR(&ev, 'p')) {
                pw[0] = '\0';
                if (tui_dialog_password(&pass, "Change password", "Current password:", pw,
                                        sizeof pw) == 0) pass_open = 1;
            }
        }
    }
    if (form_open) {
        tui_dialog_form_free(&form);
        tui_fieldset_free(&fs);
    }
    if (pass_open) tui_dialog_free(&pass);
    tui_buf_free(&out);
    tui_screen_free(scr);
    tui_term_close(t);
    return 0;
}
