/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-21 */
/* The demo of the help page and the view description: a view with two columns, ? and d. */

/*
 * viewdoc-fixture [bad]
 *
 * One view, "Work", registered once (keys, statement, columns).  Row 1 shows
 * the result of tui_viewdoc_check() of its table columns against the
 * description ("check: ok", or the difference; the argument "bad" registers a
 * description that misses a column).  ? opens the help page, d inside it the
 * description; q on the plain screen ends the program.
 */

#include "tuikit/tuikit.h"

#include <stdio.h>
#include <string.h>

static const tui_table_col COLS[] = {
    { "ITEM", 12, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "STATE", 10, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
    { "NOTE", 20, 0, TUI_ALIGN_LEFT, 0, 0, 0 },
};
static const tui_viewdoc_col DOC_COLS[] = {
    { "ITEM", "the ticket or its session" },
    { "STATE", "what it is doing now" },
    { "NOTE", "why it needs you, if it does" },
};
static const char *const KEYS[] = { "Enter", "open the row", "d", "describe the row" };
static const char *const COMMON[] = { "?", "this page", "Esc", "back" };

int main(int argc, char **argv) {
    tui_viewdoc view = { "Work", "The tickets in work and what works on them.", DOC_COLS,
                         argc > 1 && !strcmp(argv[1], "bad") ? 2 : 3, KEYS, 2 };
    char msg[200];
    char result[260] = "check: ok";
    if (tui_viewdoc_check(&view, COLS, 3, msg, sizeof msg) != 0)
        snprintf(result, sizeof result, "check: %s", msg);
    tui_term_opts o;
    tui_term_opts_init(&o);
    tui_term *t = tui_term_open(&o);
    if (!t) {
        perror("viewdoc-fixture");
        return 2;
    }
    int rows = 0, cols = 0;
    if (tui_term_size(t, &rows, &cols) != 0) return 2;
    tui_screen *scr = tui_screen_new(rows, cols);
    tui_buf out;
    tui_buf_init(&out);
    tui_viewdoc_page page;
    int open = 0;
    for (int quit = 0; !quit;) {
        tui_canvas all = tui_canvas_of(scr);
        tui_canvas_clear(all, TUI_STYLE_DEFAULT);
        tui_canvas_put_aligned(tui_canvas_sub(all, tui_rect_make(0, 0, 1, tui_canvas_cols(all))), 0,
                               " Work", TUI_ALIGN_LEFT, TUI_STYLE_HEADER);
        tui_canvas_put(all, 1, 0, result, TUI_STYLE_OK);
        tui_canvas_put(all, 3, 0, "ITEM         STATE      NOTE", TUI_STYLE_DEFAULT);
        tui_canvas_put(all, 4, 0, "TUI-147      running", TUI_STYLE_DEFAULT);
        if (open) tui_viewdoc_render(&page, all);
        tui_screen_render(scr, &out);
        tui_term_flush_buf(t, &out);
        tui_event ev;
        if (tui_term_wait(t, -1, NULL, 0, &ev) != 1 || ev.type == TUI_EV_HANGUP) break;
        if (ev.type == TUI_EV_RESIZE) {
            tui_screen_resize(scr, ev.rows, ev.cols);
        } else if (open && ev.type == TUI_EV_KEY) {
            if (tui_viewdoc_handle(&page, &ev) != TUI_DIALOG_OPEN) {
                tui_viewdoc_free(&page);
                open = 0;
            }
        } else if (ev.type == TUI_EV_KEY && TUI_EV_IS_CHAR(&ev, '?')) {
            open = tui_viewdoc_open(&page, &view, COMMON, 2) == 0;
        } else if (ev.type == TUI_EV_KEY && TUI_EV_IS_CHAR(&ev, 'q')) {
            quit = 1;
        }
    }
    if (open) tui_viewdoc_free(&page);
    tui_buf_free(&out);
    tui_screen_free(scr);
    tui_term_close(t);
    return 0;
}
