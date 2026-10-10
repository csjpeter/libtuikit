/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-09 */
/* Prints the size and alignment of every public struct, for check_abi.sh. */

/*
 * Prints the size and alignment of every public struct a program
 * allocates itself, for unit/check_abi.sh: a program built against one
 * release lays these out, the library of a later release writes into them,
 * so within one SOVERSION they never change (TUI-15).  A struct added to
 * the headers is added here; check_api.sh-style drift is caught because
 * check_abi.sh compares this list with the record both ways.
 */

#include <tuikit/tuikit.h>

#include <stdalign.h>
#include <stdio.h>

#define SHOW(t) printf("size %s %zu %zu\n", #t, sizeof(t), alignof(t))

int main(void) {
    SHOW(tui_buf);
    SHOW(tui_canvas);
    SHOW(tui_completion);
    SHOW(tui_decoder);
    SHOW(tui_dialog);
    SHOW(tui_event);
    SHOW(tui_field);
    SHOW(tui_fieldset);
    SHOW(tui_history);
    SHOW(tui_lineedit);
    SHOW(tui_listview);
    SHOW(tui_readline_opts);
    SHOW(tui_rect);
    SHOW(tui_style);
    SHOW(tui_term_opts);
    SHOW(tui_textedit);
    SHOW(tui_textview);
    return 0;
}
