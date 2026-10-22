/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-21 */
/* A program for the PTY suite: the history view over a stub server. */

/*
 * argv[1]: "object" (default), "recent", "empty" or "failed".  The stub
 * revert callback counts its calls (row 1 shows "calls=N last=ID"), refuses
 * step 2 in the server's words and accepts every other step.
 */

#include "tuikit/tuikit.h"

#include <stdio.h>
#include <string.h>

static const tui_histview_row g_rows[] = {
    { 5, "2026-10-21 10:07", "ann", "set", "TUI-9", "status", "OPEN", "DONE", 0, 1 },
    { 4, "2026-10-21 10:06", "bob", "revert", "TUI-9", "title", "New", "Old", 3, 1 },
    { 3, "2026-10-21 10:05", "bob", "set", "TUI-9", "title", "Old", "New", 0, 1 },
    { 2, "2026-10-21 10:04", "ann", "send", "TUI-8", "mail", "", "sent", 0, 0 },
    { 1, "2026-10-21 10:03", "ann", "set", "TUI-8", "priority", "LOW", "HIGH", 0, 1 },
};
#define N_ROWS ((int)(sizeof g_rows / sizeof g_rows[0]))

typedef struct {
    int                calls;
    unsigned long long last;
    int                reloads;
} stub;

static int do_revert(void *ctx, uint64_t id, char *why, size_t size) {
    stub *s = ctx;
    s->calls++;
    s->last = id;
    if (id == 1) {
        snprintf(why, size, "step 1 was overtaken by a later change, not yours");
        return -1;
    }
    return 0;
}

int main(int argc, char **argv) {
    const char *mode = argc > 1 ? argv[1] : "object";
    stub st;
    memset(&st, 0, sizeof st);
    tui_histview h;
    if (tui_histview_init(&h, strcmp(mode, "recent") == 0 ? TUI_HISTVIEW_RECENT : TUI_HISTVIEW_OBJECT,
                          do_revert, &st) != 0)
        return 2;
    if (strcmp(mode, "empty") == 0) tui_histview_set_rows(&h, g_rows, 0);
    else if (strcmp(mode, "failed") == 0) tui_histview_set_failed(&h, 1);
    else tui_histview_set_rows(&h, g_rows, N_ROWS);

    tui_term_opts o;
    tui_term_opts_init(&o);
    tui_term *t = tui_term_open(&o);
    if (!t) {
        perror("histview_fixture");
        return 2;
    }
    int rows = 0, cols = 0;
    if (tui_term_size(t, &rows, &cols) != 0) return 2;
    tui_screen *scr = tui_screen_new(rows, cols);
    tui_buf out;
    tui_buf_init(&out);
    const char *leave = "";
    for (int quit = 0; !quit;) {
        tui_canvas all = tui_canvas_of(scr);
        tui_canvas_clear(all, TUI_STYLE_DEFAULT);
        tui_canvas_put_aligned(tui_canvas_sub(all, tui_rect_make(0, 0, 1, tui_canvas_cols(all))), 0,
                               " History", TUI_ALIGN_LEFT, TUI_STYLE_HEADER);
        char line[96];
        snprintf(line, sizeof line, "calls=%d last=%llu reloads=%d %s", st.calls, st.last,
                 st.reloads, leave);
        tui_canvas_put(all, 1, 0, line, TUI_STYLE_OK);
        tui_histview_render(&h, tui_canvas_sub(all, tui_rect_make(3, 0, tui_canvas_rows(all) - 3,
                                                                  tui_canvas_cols(all))));
        tui_screen_render(scr, &out);
        tui_term_flush_buf(t, &out);
        tui_event ev;
        if (tui_term_wait(t, -1, NULL, 0, &ev) != 1 || ev.type == TUI_EV_HANGUP) break;
        if (ev.type == TUI_EV_RESIZE) {
            tui_screen_resize(scr, ev.rows, ev.cols);
        } else if (ev.type == TUI_EV_KEY) {
            switch (tui_histview_handle(&h, &ev)) {
            case TUI_HISTVIEW_BACK: leave = "back"; break;
            case TUI_HISTVIEW_QUIT: quit = 1; break;
            case TUI_HISTVIEW_REVERTED: st.reloads++; break;
            default: break;
            }
        }
    }
    tui_histview_free(&h);
    tui_buf_free(&out);
    tui_screen_free(scr);
    tui_term_close(t);
    return 0;
}
