/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Péter Császár */

#include "tuikit/listview.h"
#include "internal.h"

#include <string.h>

void tui_listview_init(tui_listview *lv) {
    if (!lv) return;
    memset(lv, 0, sizeof(*lv));
    lv->selected = -1;
}

void tui_listview_reveal_selected(tui_listview *lv) {
    if (!lv) return;
    if (lv->count <= 0) {
        lv->selected = -1;
        lv->scroll_top = 0;
        return;
    }
    if (lv->rows_visible > 0 && lv->selected >= 0) {
        if (lv->selected < lv->scroll_top) lv->scroll_top = lv->selected;
        else if (lv->selected >= lv->scroll_top + lv->rows_visible)
            lv->scroll_top = lv->selected - lv->rows_visible + 1;
    }
    /* No empty rows at the bottom while items are hidden above. */
    int max_top = lv->count - (lv->rows_visible > 0 ? lv->rows_visible : 1);
    if (max_top < 0) max_top = 0;
    if (lv->scroll_top > max_top) lv->scroll_top = max_top;
    if (lv->scroll_top < 0) lv->scroll_top = 0;
}

void tui_listview_set_count(tui_listview *lv, int count) {
    if (!lv) return;
    lv->count = count < 0 ? 0 : count;
    if (lv->count == 0) {
        lv->selected = -1;
    } else {
        if (lv->selected < 0) lv->selected = 0;
        if (lv->selected >= lv->count) lv->selected = lv->count - 1;
    }
    tui_listview_reveal_selected(lv);
}

void tui_listview_set_viewport(tui_listview *lv, int rows) {
    if (!lv) return;
    lv->rows_visible = rows < 0 ? 0 : rows;
    tui_listview_reveal_selected(lv);
}

void tui_listview_select(tui_listview *lv, int index) {
    if (!lv || lv->count <= 0) return;
    if (index < 0) index = 0;
    if (index >= lv->count) index = lv->count - 1;
    lv->selected = index;
    tui_listview_reveal_selected(lv);
}

static int page(const tui_listview *lv) {
    return lv->rows_visible > 1 ? lv->rows_visible : 1;
}

void tui_listview_move_up(tui_listview *lv) {
    if (lv && lv->count > 0) tui_listview_select(lv, lv->selected - 1);
}

void tui_listview_move_down(tui_listview *lv) {
    if (lv && lv->count > 0) tui_listview_select(lv, lv->selected + 1);
}

void tui_listview_page_up(tui_listview *lv) {
    if (lv && lv->count > 0) tui_listview_select(lv, lv->selected - page(lv));
}

void tui_listview_page_down(tui_listview *lv) {
    if (lv && lv->count > 0) tui_listview_select(lv, lv->selected + page(lv));
}

void tui_listview_home(tui_listview *lv) {
    if (lv && lv->count > 0) tui_listview_select(lv, 0);
}

void tui_listview_end(tui_listview *lv) {
    if (lv && lv->count > 0) tui_listview_select(lv, lv->count - 1);
}

int tui_listview_is_visible(const tui_listview *lv, int index) {
    if (!lv || lv->rows_visible <= 0 || index < 0 || index >= lv->count) return 0;
    return index >= lv->scroll_top && index < lv->scroll_top + lv->rows_visible;
}

int tui_listview_handle(tui_listview *lv, const tui_event *ev) {
    if (!lv || !ev) return 0;
    switch (tui__nav_key(ev)) {
    case TUI_KEY_UP:   tui_listview_move_up(lv); return 1;
    case TUI_KEY_DOWN: tui_listview_move_down(lv); return 1;
    case TUI_KEY_PGUP: tui_listview_page_up(lv); return 1;
    case TUI_KEY_PGDN: tui_listview_page_down(lv); return 1;
    case TUI_KEY_HOME: tui_listview_home(lv); return 1;
    case TUI_KEY_END:  tui_listview_end(lv); return 1;
    default:           return 0;
    }
}

void tui_listview_render(tui_listview *lv, tui_canvas c, tui_style blank,
                         tui_listview_draw_fn draw, void *ctx) {
    if (!lv) return;
    int rows = tui_canvas_rows(c);
    tui_listview_set_viewport(lv, rows);
    for (int r = 0; r < rows; r++) {
        tui_canvas row = tui_canvas_sub(c, tui_rect_make(r, 0, 1, tui_canvas_cols(c)));
        tui_canvas_clear(row, blank);
        int index = lv->scroll_top + r;
        if (draw && index < lv->count) draw(ctx, row, index, index == lv->selected);
    }
}
