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

static int same_style(tui_style a, tui_style b) {
    return a.fg == b.fg && a.bg == b.bg && a.attrs == b.attrs;
}

tui_style tui__selection_base(int colors) {
    tui_style cur = tui_scheme_style(TUI_ROLE_SELECTION);
    if (!same_style(cur, tui_scheme_builtin(TUI_ROLE_SELECTION))) {
        /* The scheme's own selection: its colours at the colour depth. */
        if (colors < 256) {
            cur.fg = tui_color_reduce(cur.fg, colors);
            cur.bg = tui_color_reduce(cur.bg, colors);
        }
        return cur;
    }
    /* The built-in one: a dark blue background by depth, bold at 8. */
    if (colors >= 256) return tui_style_make(TUI_COLOR_DEFAULT, TUI_COLOR_256(17), 0);
    if (colors >= 16) return tui_style_make(TUI_COLOR_DEFAULT, TUI_BLUE, 0);
    return tui_style_attrs(TUI_ATTR_BOLD);
}

tui_style tui_selection_style(void) {
    tui_style cur = tui_scheme_style(TUI_ROLE_SELECTION);
    if (same_style(cur, tui_scheme_builtin(TUI_ROLE_SELECTION)))
        return tui_style_attrs(TUI_ATTR_BOLD | TUI_ATTR_REVERSE);
    return cur;
}

tui_style tui__button_focus(tui_style focus) {
    if (!tui__is_default_selection(focus)) return focus;
    tui_style cur = tui_selection_style();
    cur.attrs = (uint16_t)(cur.attrs | TUI_ATTR_BOLD);  /* not by colour alone */
    return cur;
}

tui_style tui__input_style(tui_style st, int focused) {
    tui_style in = tui_scheme_style(TUI_ROLE_INPUT);
    if (!same_style(st, in)) return st;  /* the program's own choice */
    if (!focused) return in;
    tui_style fo = tui_scheme_style(TUI_ROLE_INPUT_FOCUS);
    if (fo.fg != TUI_COLOR_DEFAULT) in.fg = fo.fg;
    if (fo.bg != TUI_COLOR_DEFAULT) in.bg = fo.bg;
    in.attrs = (uint16_t)(in.attrs | fo.attrs);
    return in;
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
    return same_style(st, tui_selection_style());
}
