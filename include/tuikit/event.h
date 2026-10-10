/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Public API: tui_event and tui_decoder, the terminal input decoder. */

#ifndef LIBTUIKIT_EVENT_H
#define LIBTUIKIT_EVENT_H

/**
 * @file tuikit/event.h
 * @brief Input events, and the decoder that turns terminal bytes into them.
 *
 * The decoder is a pure byte state machine: it reads no file descriptor and
 * keeps no clock.  The caller feeds it bytes and asks for events; when a
 * sequence stays incomplete (a lone ESC, half a UTF-8 character) for longer
 * than the caller's ESC delay, the caller calls tui_decoder_timeout().
 * tui_term_wait() and tui_tty_read_key() do exactly that.
 *
 * See tui_decoder_init(3) for the sequences understood.
 */

#include "tuikit/api.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What happened. */
typedef enum tui_event_type {
    TUI_EV_NONE = 0,  /**< no event */
    TUI_EV_KEY,       /**< a key: see key, mods, cp, utf8 */
    TUI_EV_PASTE,     /**< bracketed paste: see paste, paste_len */
    TUI_EV_RESIZE,    /**< the terminal size changed: see rows, cols */
    TUI_EV_FD,        /**< one of the caller's extra fds is ready: fd_index */
    TUI_EV_HANGUP     /**< the terminal is gone: end of input or hang-up */
} tui_event_type;

/**
 * @brief Key codes.  Values above 0x10FFFF, so never a code point.
 *
 * A printable character is TUI_KEY_CHAR with the code point in @c cp.
 * Ctrl+letter is TUI_KEY_CHAR with the lower-case letter in @c cp and
 * TUI_MOD_CTRL in @c mods (Ctrl-C: cp 'c', mods TUI_MOD_CTRL).
 * Shift-Tab is TUI_KEY_TAB with TUI_MOD_SHIFT.
 */
typedef enum tui_key {
    TUI_KEY_CHAR = 0,
    TUI_KEY_ENTER = 0x110000,
    TUI_KEY_TAB,
    TUI_KEY_BACKSPACE,
    TUI_KEY_ESC,
    TUI_KEY_UP,
    TUI_KEY_DOWN,
    TUI_KEY_LEFT,
    TUI_KEY_RIGHT,
    TUI_KEY_HOME,
    TUI_KEY_END,
    TUI_KEY_PGUP,
    TUI_KEY_PGDN,
    TUI_KEY_INSERT,
    TUI_KEY_DELETE,
    TUI_KEY_F1, TUI_KEY_F2, TUI_KEY_F3,  TUI_KEY_F4,
    TUI_KEY_F5, TUI_KEY_F6, TUI_KEY_F7,  TUI_KEY_F8,
    TUI_KEY_F9, TUI_KEY_F10, TUI_KEY_F11, TUI_KEY_F12
} tui_key;

/** @brief Modifier bits of a key event. */
enum {
    TUI_MOD_SHIFT = 1,
    TUI_MOD_ALT   = 2,
    TUI_MOD_CTRL  = 4
};

/** @brief One input event. */
typedef struct tui_event {
    tui_event_type type;
    tui_key  key;        /**< TUI_EV_KEY */
    unsigned mods;       /**< TUI_MOD_* bits */
    uint32_t cp;         /**< TUI_KEY_CHAR: the code point */
    char     utf8[5];    /**< TUI_KEY_CHAR: cp as NUL-terminated UTF-8 */
    int      rows, cols; /**< TUI_EV_RESIZE: the new size */
    int      fd_index;   /**< TUI_EV_FD: index into the caller's array */
    const char *paste;   /**< TUI_EV_PASTE: the text, valid until the next
                              call on the same decoder or terminal */
    size_t   paste_len;  /**< TUI_EV_PASTE: its length in bytes */
} tui_event;

/** @brief True when @p ev is Ctrl+@p c (a lower-case letter), e.g. 'c'. */
#define TUI_EV_IS_CTRL(ev, c) \
    ((ev)->type == TUI_EV_KEY && (ev)->key == TUI_KEY_CHAR && \
     (ev)->cp == (uint32_t)(c) && ((ev)->mods & TUI_MOD_CTRL))

/** @brief True when @p ev is the plain character @p c (no modifier). */
#define TUI_EV_IS_CHAR(ev, c) \
    ((ev)->type == TUI_EV_KEY && (ev)->key == TUI_KEY_CHAR && \
     (ev)->cp == (uint32_t)(c) && (ev)->mods == 0)

/** @brief Longest escape sequence assembled; a longer one is skipped whole. */
#define TUI_DECODER_SEQ_MAX 64

/** @brief Largest bracketed paste kept; the rest is consumed and dropped. */
#define TUI_DECODER_PASTE_MAX (1024u * 1024u)

/**
 * @brief Decoder state.  Caller-allocated; the members are private.
 */
typedef struct tui_decoder {
    unsigned char in[256];   /* fed, not yet decoded: in[pos, len) */
    size_t        in_len;
    size_t        in_pos;
    int           skip_csi;  /* inside an over-long CSI: drop to its end */
    int           in_paste;  /* between CSI 200~ and CSI 201~ */
    char         *paste;
    size_t        paste_len, paste_cap;
    int           paste_truncated;
} tui_decoder;

/** @brief An empty decoder. */
TUI_API void tui_decoder_init(tui_decoder *d);

/** @brief Releases the paste buffer and re-initialises the decoder. */
TUI_API void tui_decoder_free(tui_decoder *d);

/**
 * @brief Gives the decoder more bytes.
 * @return The number of bytes accepted: fewer than @p n when the internal
 *         queue is full, in which case the caller drains it with
 *         tui_decoder_next() and feeds the rest.
 */
TUI_API size_t tui_decoder_feed(tui_decoder *d, const char *bytes, size_t n);

/**
 * @brief Produces the next complete event.
 * @return 1 with @p ev filled, 0 when no complete event is available.
 */
TUI_API int tui_decoder_next(tui_decoder *d, tui_event *ev);

/**
 * @brief True when bytes are held for an incomplete sequence: the caller
 *        should call tui_decoder_timeout() if no more input arrives within
 *        its ESC delay.
 */
TUI_API int tui_decoder_pending(const tui_decoder *d);

/**
 * @brief Resolves an incomplete sequence: a lone ESC becomes TUI_KEY_ESC,
 *        ESC followed by '[' or 'O' becomes Alt+'[' / Alt+'O', half a UTF-8
 *        character becomes U+FFFD, anything else is dropped.
 * @return 1 with @p ev filled, 0 when nothing was pending or it was dropped.
 */
TUI_API int tui_decoder_timeout(tui_decoder *d, tui_event *ev);

/** @brief A short English name of the event, e.g. "Ctrl+Up", "a", "Resize". */
TUI_API const char *tui_event_name(const tui_event *ev, char *buf, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_EVENT_H */
