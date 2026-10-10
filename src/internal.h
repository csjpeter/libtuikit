/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
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
