/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Declarations shared between the library's own sources; not installed. */

#ifndef LIBTUIKIT_INTERNAL_H
#define LIBTUIKIT_INTERNAL_H

/*
 * Shared between the library's own sources; not installed.
 */

#include <stddef.h>
#include <stdint.h>

#include <termios.h>

/* The raw-mode transformation of a termios state, as tui_tty_raw_enter(3)
 * documents it.  Used by tui_tty and tui_term alike. */
void tui__make_raw(struct termios *t, unsigned flags);

/* Applies @p want and reads it back; the terminal accepted it only if the
 * flags we changed read back as requested.  0, or -1 with errno (EIO when
 * the terminal silently ignored part of the change). */
int tui__apply_mode(int fd, const struct termios *want);

/* write(2) of all @p n bytes, retrying on EINTR and polling on EAGAIN. */
int tui__write_all(int fd, const void *p, size_t n);

/* Whether @p cp is a bidirectional control (U+061C, U+200E/F,
 * U+202A..202E, U+2066..2069): text drawn from an untrusted source drops
 * it, as a terminal that implements bidi would reorder what is around it. */
int tui__cp_is_bidi_control(uint32_t cp);

/* Milliseconds of a monotonic clock. */
int64_t tui__now_ms(void);

#include "tuikit/event.h"
#include "tuikit/screen.h"

/* The one selection of the library (policies/tui-interface-model.md,
 * "Palette" and "Lists"), drawn by the table, the dialog menus and the
 * fieldset's candidate lists alike (src/selection.c). */

/* The colour depth the terminal advertises: 256, 16 or 8, from COLORTERM
 * and TERM. */
int tui__colors_from_env(void);

/* The selection's base for a colour depth: a dark blue background at 256
 * and 16 colours, bold at 8. */
tui_style tui__selection_base(int colors);

/* The style of a span: the row's under the span's own, and when
 * @p selected, the selection's @p sel background (a row's own background
 * gives way to it, a span's own does not); text that has no colour of its
 * own is white on it, and a colour that would be lost on the dark blue is
 * its bright variant, in bold: the hue, and so the meaning, stays. */
tui_style tui__selection_compose(tui_style sel, tui_style row, tui_style span, int selected);

/* Whether @p st is the palette's TUI_STYLE_SELECTION, the default of the
 * widgets' selection styles; any other style is the program's own choice. */
int tui__is_default_selection(tui_style st);

/* The movement key an event stands for in a list or a text view: the
 * arrows, the page keys and Home/End themselves, and the workspace's
 * shared letters for them -- j/k, ctrl-f/ctrl-b, g/G
 * (policies/tui-interface-model.md, "Keys that mean the same everywhere").
 * TUI_KEY_CHAR when the event is none of these. */
static inline tui_key tui__nav_key(const tui_event *ev) {
    if (ev->type != TUI_EV_KEY) return TUI_KEY_CHAR;
    if (ev->key != TUI_KEY_CHAR) return ev->mods == 0 ? ev->key : TUI_KEY_CHAR;
    if (ev->mods == TUI_MOD_CTRL) {
        if (ev->cp == 'f') return TUI_KEY_PGDN;
        if (ev->cp == 'b') return TUI_KEY_PGUP;
        return TUI_KEY_CHAR;
    }
    if (ev->mods != 0) return TUI_KEY_CHAR;
    switch (ev->cp) {
    case 'j': return TUI_KEY_DOWN;
    case 'k': return TUI_KEY_UP;
    case 'g': return TUI_KEY_HOME;
    case 'G': return TUI_KEY_END;
    default:  return TUI_KEY_CHAR;
    }
}

#endif /* LIBTUIKIT_INTERNAL_H */
