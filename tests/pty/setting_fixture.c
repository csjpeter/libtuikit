/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* A program for the PTY suite: the setting dialog. */

/*
 * The stub of TUI-138.  Row 0 shows what was done last.  's' opens the
 * setting dialog "Listen port" (value 8080, valid: digits, 1 to 65535); the
 * result goes to row 0 ("setting: ok <value>" or "setting: cancelled
 * <value>", where the value is what the buffer holds afterwards).  q quits.
 */

#include "tuikit/tuikit.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int check_port(void *user, const char *v, char *err, size_t errsz) {
    (void)user;
    char *end = NULL;
    long n = strtol(v, &end, 10);
    if (!*v || *end || n < 1 || n > 65535) {
        snprintf(err, errsz, "Not a port: use 1-65535");
        return 1;
    }
    return 0;
}

int main(void) {
    tui_term_opts o;
    tui_term_opts_init(&o);
    tui_term *t = tui_term_open(&o);
    if (!t) {
        perror("setting_fixture");
        return 2;
    }
    int rows = 0, cols = 0;
    if (tui_term_size(t, &rows, &cols) != 0) return 2;
    tui_screen *scr = tui_screen_new(rows, cols);
    tui_buf out;
    tui_buf_init(&out);

    char value[16] = "8080", shown[64] = "setting: nothing";
    tui_settingdialog d;
    int open = 0;

    for (int quit = 0; !quit;) {
        tui_canvas all = tui_canvas_of(scr);
        tui_canvas_clear(all, TUI_STYLE_DEFAULT);
        tui_canvas_put(all, 0, 0, shown, TUI_STYLE_OK);
        if (open) tui_settingdialog_render(&d, all);
        tui_screen_render(scr, &out);
        tui_term_flush_buf(t, &out);
        tui_event ev;
        if (tui_term_wait(t, -1, NULL, 0, &ev) != 1 || ev.type == TUI_EV_HANGUP) break;
        if (ev.type == TUI_EV_RESIZE) {
            tui_screen_resize(scr, ev.rows, ev.cols);
        } else if (ev.type == TUI_EV_KEY && open) {
            tui_dialog_state st = tui_settingdialog_handle(&d, &ev);
            if (st == TUI_DIALOG_OPEN) continue;
            snprintf(shown, sizeof shown, "setting: %s %s", st == TUI_DIALOG_OK ? "ok" : "cancelled",
                     value);
            tui_settingdialog_free(&d);
            open = 0;
        } else if (ev.type == TUI_EV_KEY) {
            if (TUI_EV_IS_CHAR(&ev, 'q')) quit = 1;
            else if (TUI_EV_IS_CHAR(&ev, 's')) {
                snprintf(value, sizeof value, "8080");
                if (tui_settingdialog_init(&d, "Listen port",
                                           "The TCP port the server listens on.  A change takes "
                                           "effect after the server is restarted.",
                                           "Allowed: 1-65535", value, sizeof value, "8080") == 0) {
                    d.validate = check_port;
                    open = 1;
                }
            }
        }
    }
    if (open) tui_settingdialog_free(&d);
    tui_buf_free(&out);
    tui_screen_free(scr);
    tui_term_close(t);
    return 0;
}
