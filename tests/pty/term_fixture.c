/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* A program for the PTY suite: a tui_term session that leaves in every way a program can. */

/*
 * A program for the PTY suite: opens a tui_term session, shows the size
 * and the last key, and on command leaves in one of the ways a real program
 * can -- so the suite can check, through a real terminal, that every way
 * out gives the terminal back.
 *
 *   q         close the session and exit 0
 *   e         exit(3) without closing (the atexit path)
 *   t / h     raise SIGTERM / SIGHUP (the fatal-signal path)
 *   s         suspend: print SUSPENDED on the normal screen, wait for a
 *             key there in canonical mode, then resume
 *   any key   shown on row 1 by name
 */

#include "tuikit/tuikit.h"

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void draw(tui_screen *scr, int rows, int cols, const char *last) {
    tui_canvas all = tui_canvas_of(scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    char line[64];
    snprintf(line, sizeof(line), "READY %dx%d", rows, cols);
    tui_canvas_put(all, 0, 0, line, TUI_STYLE_DEFAULT);
    snprintf(line, sizeof(line), "key: %s", last);
    tui_canvas_put(all, 1, 0, line, TUI_STYLE_DEFAULT);
    tui_canvas_put(all, rows - 1, 0, "bottom", tui_style_attrs(TUI_ATTR_REVERSE));
}

int main(void) {
    tui_term_opts o;
    tui_term_opts_init(&o);
    tui_term *t = tui_term_open(&o);
    if (!t) {
        perror("term_fixture");
        return 2;
    }
    int rows = 0, cols = 0;
    if (tui_term_size(t, &rows, &cols) != 0) return 2;
    tui_screen *scr = tui_screen_new(rows, cols);
    tui_buf out;
    tui_buf_init(&out);
    char last[64] = "none";
    for (;;) {
        draw(scr, rows, cols, last);
        tui_screen_render(scr, &out);
        tui_term_flush_buf(t, &out);
        tui_event ev;
        if (tui_term_wait(t, -1, NULL, 0, &ev) != 1) return 4;
        if (ev.type == TUI_EV_HANGUP) return 5;
        if (ev.type == TUI_EV_RESIZE) {
            rows = ev.rows;
            cols = ev.cols;
            tui_screen_resize(scr, rows, cols);
            continue;
        }
        if (ev.type != TUI_EV_KEY) continue;
        if (TUI_EV_IS_CHAR(&ev, 'q')) break;
        if (TUI_EV_IS_CHAR(&ev, 'e')) exit(3);
        if (TUI_EV_IS_CHAR(&ev, 't')) raise(SIGTERM);
        if (TUI_EV_IS_CHAR(&ev, 'h')) raise(SIGHUP);
        if (TUI_EV_IS_CHAR(&ev, 's')) {
            tui_term_suspend(t);
            const char msg[] = "SUSPENDED, press Enter\n";
            ssize_t w = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
            (void)w;
            char c;
            ssize_t r = read(STDIN_FILENO, &c, 1);   /* canonical: waits for Enter */
            (void)r;
            tui_term_resume(t);
            continue;
        }
        tui_event_name(&ev, last, sizeof(last));
    }
    tui_buf_free(&out);
    tui_screen_free(scr);
    tui_term_close(t);
    return 0;
}
