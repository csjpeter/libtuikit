/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-09 */
/* The scroll bar at the right edge of a scrolling area (policies/tui-interface-model.md). */

#include "internal.h"

int tui__scrollbar_needed(int total, int visible) {
    return visible > 0 && total > visible;
}

void tui__scrollbar_geometry(int rows, int total, int visible, int top, int *thumb_top,
                             int *thumb_len) {
    int len = (int)((long)rows * visible / total);
    if (len < 1) len = 1;
    if (len > rows - 1) len = rows > 1 ? rows - 1 : rows;
    int range = total - visible;
    if (top < 0) top = 0;
    if (top > range) top = range;
    int pos = rows > len ? (int)(((long)(rows - len) * top + range / 2) / range) : 0;
    *thumb_top = pos;
    *thumb_len = len;
}

int tui__scrollbar(tui_canvas c, int total, int top, int visible) {
    int rows = tui_canvas_rows(c), cols = tui_canvas_cols(c);
    if (rows <= 0 || cols <= 0 || !tui__scrollbar_needed(total, visible)) return 0;
    int tt, tl;
    tui__scrollbar_geometry(rows, total, visible, top, &tt, &tl);
    tui_style track = tui_scheme_style(TUI_ROLE_SCROLLBAR_TRACK);
    tui_style thumb = tui_scheme_style(TUI_ROLE_SCROLLBAR_THUMB);
    for (int r = 0; r < rows; r++) {
        int in = r >= tt && r < tt + tl;
        tui_canvas_put(c, r, cols - 1, in ? "\xE2\x96\x88" : "\xE2\x94\x82", in ? thumb : track);
    }
    return 1;
}
