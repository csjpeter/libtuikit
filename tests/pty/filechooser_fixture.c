/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* A program for the PTY suite: a file chooser over a header, a status row and a result line. */

/*
 * filechooser-fixture MODE START [FILTER]  (MODE: open, dir or save)
 *
 * Opens a tui_filechooser at once, waits on its notify descriptor together
 * with the terminal, and writes the outcome on row 1: "chosen: PATH" or
 * "cancelled".  'o' opens the chooser again, 'q' ends the program.
 */

#include "tuikit/tuikit.h"

#include <poll.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: filechooser-fixture open|dir|save START [FILTER]\n");
        return 2;
    }
    tui_fc_mode mode = !strcmp(argv[1], "dir") ? TUI_FC_DIR : !strcmp(argv[1], "save") ? TUI_FC_SAVE
                                                                                        : TUI_FC_OPEN;
    const char *filter = argc > 3 ? argv[3] : NULL;
    tui_term_opts o;
    tui_term_opts_init(&o);
    tui_term *t = tui_term_open(&o);
    if (!t) {
        perror("filechooser-fixture");
        return 2;
    }
    int rows = 0, cols = 0;
    if (tui_term_size(t, &rows, &cols) != 0) return 2;
    tui_screen *scr = tui_screen_new(rows, cols);
    tui_buf out;
    tui_buf_init(&out);
    tui_filechooser fc;
    int open = tui_filechooser_open(&fc, NULL, mode, argv[2], filter) == 0;
    char result[600] = "result: none";
    for (int quit = 0; !quit;) {
        tui_canvas all = tui_canvas_of(scr);
        tui_canvas_clear(all, TUI_STYLE_DEFAULT);
        tui_canvas_put_aligned(tui_canvas_sub(all, tui_rect_make(0, 0, 1, tui_canvas_cols(all))), 0,
                               " File chooser", TUI_ALIGN_LEFT, TUI_STYLE_HEADER);
        tui_canvas_put(all, 1, 0, result, TUI_STYLE_OK);
        if (open) tui_filechooser_render(&fc, all);
        tui_screen_render(scr, &out);
        tui_term_flush_buf(t, &out);
        struct pollfd pfd = { open ? fc.notify_fd : -1, POLLIN, 0 };
        tui_event ev;
        if (tui_term_wait(t, -1, &pfd, 1, &ev) != 1 || ev.type == TUI_EV_HANGUP) break;
        if (ev.type == TUI_EV_RESIZE) {
            tui_screen_resize(scr, ev.rows, ev.cols);
        } else if (ev.type == TUI_EV_FD) {
            tui_filechooser_poll(&fc);
        } else if (open && ev.type == TUI_EV_KEY) {
            tui_fc_state st = tui_filechooser_handle(&fc, &ev);
            if (st == TUI_FC_OK) snprintf(result, sizeof result, "chosen: %s", fc.path);
            if (st == TUI_FC_CANCELLED) snprintf(result, sizeof result, "cancelled");
            if (st != TUI_FC_ACTIVE) {
                tui_filechooser_free(&fc);
                open = 0;
            }
        } else if (ev.type == TUI_EV_KEY && ev.key == TUI_KEY_CHAR && ev.cp == 'o') {
            open = tui_filechooser_open(&fc, NULL, mode, argv[2], filter) == 0;
        } else if (ev.type == TUI_EV_KEY && ev.key == TUI_KEY_CHAR && ev.cp == 'q') {
            quit = 1;
        }
    }
    if (open) tui_filechooser_free(&fc);
    tui_buf_free(&out);
    tui_screen_free(scr);
    tui_term_close(t);
    return 0;
}
