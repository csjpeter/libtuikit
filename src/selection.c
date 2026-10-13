/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-13 */
/* The one selection of the library: its colours by depth and how it composes. */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

int tui__colors_from_env(void) {
    const char *ct = getenv("COLORTERM");
    const char *term = getenv("TERM");
    if (ct && *ct) return 256;
    if (!term || !*term) return 8;
    size_t n = strlen(term);
    if (n >= 8 && strcmp(term + n - 8, "256color") == 0) return 256;
    if (strcmp(term, "dumb") == 0 || strcmp(term, "linux") == 0 ||
        strcmp(term, "ansi") == 0 || strncmp(term, "vt", 2) == 0)
        return 8;
    return 16;
}

tui_style tui__selection_base(int colors) {
    if (colors >= 256) return tui_style_make(TUI_COLOR_DEFAULT, TUI_COLOR_256(17), 0);
    if (colors >= 16) return tui_style_make(TUI_COLOR_DEFAULT, TUI_BLUE, 0);
    return tui_style_attrs(TUI_ATTR_BOLD);
}

tui_style tui__selection_compose(tui_style sel, tui_style row, tui_style span, int selected) {
    tui_style s;
    if (selected) row.bg = TUI_COLOR_DEFAULT;  /* the selection's is the one */
    s.fg = span.fg != TUI_COLOR_DEFAULT ? span.fg : row.fg;
    s.bg = span.bg != TUI_COLOR_DEFAULT ? span.bg : row.bg;
    s.attrs = (uint16_t)(row.attrs | span.attrs);
    if (!selected) return s;
    s.attrs = (uint16_t)(s.attrs | sel.attrs);
    if (s.bg != TUI_COLOR_DEFAULT || sel.bg == TUI_COLOR_DEFAULT) return s;
    s.bg = sel.bg;
    if (s.fg == TUI_COLOR_DEFAULT) {
        s.fg = TUI_BRIGHT_WHITE;
    } else if ((s.fg & 0xFF000000u) == 0x01000000u) {
        unsigned n = s.fg & 0x0Fu;
        if (n == 0) {
            s.fg = TUI_WHITE;
        } else if (n < 8) {
            s.fg = TUI_COLOR_16(n + 8);
            s.attrs = (uint16_t)(s.attrs | TUI_ATTR_BOLD);
        }
    }
    return s;
}

int tui__is_default_selection(tui_style st) {
    tui_style d = TUI_STYLE_SELECTION;
    return st.fg == d.fg && st.bg == d.bg && st.attrs == d.attrs;
}
