/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* The terminal input decoder: bytes in, key events out. */

/*
 * Terminal input decoder: bytes in, key events out.
 *
 * The grammar is taken from xterm's "Control Sequences" document (PC-style
 * function keys, modifyCursorKeys, bracketed paste), not from any terminal
 * emulator's source: the test instrument must not share this logic.
 *
 * A CSI sequence is parsed by its general form -- parameter bytes
 * 0x30-0x3F, intermediate bytes 0x20-0x2F, one final byte 0x40-0x7E -- so
 * a sequence that is not understood is still consumed whole and never
 * leaks into the input as typed characters.  (Survey defect: "ESC [ 1 ; 5
 * A" decoded byte by byte leaves "5A" behind.)
 */

#include "tuikit/event.h"
#include "tuikit/text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ESC 0x1B

enum { PARSE_INCOMPLETE = -1, PARSE_DROPPED = 0, PARSE_EVENT = 1 };

_Static_assert(TUI_DECODER_SEQ_MAX < sizeof(((tui_decoder *)0)->in),
               "an incomplete sequence must fit in the input queue");

static const char paste_end[] = "\x1b[201~";
#define PASTE_END_LEN (sizeof(paste_end) - 1)

void tui_decoder_init(tui_decoder *d) {
    if (!d) return;
    memset(d, 0, sizeof(*d));
}

void tui_decoder_free(tui_decoder *d) {
    if (!d) return;
    free(d->paste);
    tui_decoder_init(d);
}

static void compact(tui_decoder *d) {
    if (d->in_pos == 0) return;
    memmove(d->in, d->in + d->in_pos, d->in_len - d->in_pos);
    d->in_len -= d->in_pos;
    d->in_pos = 0;
}

size_t tui_decoder_feed(tui_decoder *d, const char *bytes, size_t n) {
    if (!d || !bytes) return 0;
    compact(d);
    size_t room = sizeof(d->in) - d->in_len;
    if (n > room) n = room;
    memcpy(d->in + d->in_len, bytes, n);
    d->in_len += n;
    return n;
}

int tui_decoder_pending(const tui_decoder *d) {
    if (!d) return 0;
    return d->in_pos < d->in_len || d->in_paste || d->skip_csi;
}

/* ── Event constructors ─────────────────────────────────────────────── */

static void key_event(tui_event *ev, tui_key key, unsigned mods) {
    memset(ev, 0, sizeof(*ev));
    ev->type = TUI_EV_KEY;
    ev->key = key;
    ev->mods = mods;
}

static void char_event(tui_event *ev, uint32_t cp, unsigned mods) {
    key_event(ev, TUI_KEY_CHAR, mods);
    ev->cp = cp;
    /* Text only for what would be typed as text: a Ctrl or Alt chord is a
     * command, and inserting its utf8 would be a bug in the caller. */
    if (!(mods & (TUI_MOD_CTRL | TUI_MOD_ALT))) {
        int k = tui_utf8_encode(cp, ev->utf8);
        ev->utf8[k] = '\0';
    }
}

/* xterm's modifier parameter: 1 + (shift 1 | alt 2 | ctrl 4 | meta 8). */
static unsigned mods_from_param(long p) {
    if (p < 2 || p > 16) return 0;
    unsigned m = (unsigned)(p - 1);
    unsigned mods = 0;
    if (m & 1) mods |= TUI_MOD_SHIFT;
    if (m & (2 | 8)) mods |= TUI_MOD_ALT;
    if (m & 4) mods |= TUI_MOD_CTRL;
    return mods;
}

/* ── Single bytes and UTF-8 ─────────────────────────────────────────── */

/* A key that is not an escape sequence: one control byte, ASCII, or one
 * UTF-8 character. */
static int parse_plain(const unsigned char *p, size_t n, tui_event *ev,
                       size_t *used) {
    unsigned char c = p[0];
    *used = 1;
    if (c == '\r' || c == '\n') { key_event(ev, TUI_KEY_ENTER, 0); return PARSE_EVENT; }
    if (c == '\t')              { key_event(ev, TUI_KEY_TAB, 0); return PARSE_EVENT; }
    if (c == 0x7F || c == 0x08) { key_event(ev, TUI_KEY_BACKSPACE, 0); return PARSE_EVENT; }
    if (c == 0x00)              { char_event(ev, ' ', TUI_MOD_CTRL); return PARSE_EVENT; }
    if (c >= 0x01 && c <= 0x1A) { char_event(ev, (uint32_t)('a' + c - 1), TUI_MOD_CTRL); return PARSE_EVENT; }
    if (c >= 0x1C && c <= 0x1F) { char_event(ev, (uint32_t)(c + 0x40), TUI_MOD_CTRL); return PARSE_EVENT; }
    if (c < 0x80)               { char_event(ev, c, 0); return PARSE_EVENT; }

    int len = tui_utf8_seq_len(c);
    if (len > 1 && n < (size_t)len) {
        /* Wait for the rest, but only if what is here can still become a
         * valid character. */
        uint32_t cp;
        int k = tui_utf8_decode((const char *)p, n, &cp);
        if ((size_t)k == n) return PARSE_INCOMPLETE;
    }
    uint32_t cp;
    int k = tui_utf8_decode((const char *)p, n, &cp);
    *used = (size_t)k;
    if (cp >= 0x80 && cp <= 0x9F) return PARSE_DROPPED;   /* C1 control */
    char_event(ev, cp, 0);
    return PARSE_EVENT;
}

/* ── CSI ────────────────────────────────────────────────────────────── */

static tui_key tilde_key(long p) {
    switch (p) {
    case 1: case 7: return TUI_KEY_HOME;
    case 2:         return TUI_KEY_INSERT;
    case 3:         return TUI_KEY_DELETE;
    case 4: case 8: return TUI_KEY_END;
    case 5:         return TUI_KEY_PGUP;
    case 6:         return TUI_KEY_PGDN;
    case 11: return TUI_KEY_F1;  case 12: return TUI_KEY_F2;
    case 13: return TUI_KEY_F3;  case 14: return TUI_KEY_F4;
    case 15: return TUI_KEY_F5;  case 17: return TUI_KEY_F6;
    case 18: return TUI_KEY_F7;  case 19: return TUI_KEY_F8;
    case 20: return TUI_KEY_F9;  case 21: return TUI_KEY_F10;
    case 23: return TUI_KEY_F11; case 24: return TUI_KEY_F12;
    default: return TUI_KEY_CHAR;   /* not a key */
    }
}

/* The keys a final byte names in both CSI and SS3 form. */
static tui_key final_key(unsigned char f) {
    switch (f) {
    case 'A': return TUI_KEY_UP;
    case 'B': return TUI_KEY_DOWN;
    case 'C': return TUI_KEY_RIGHT;
    case 'D': return TUI_KEY_LEFT;
    case 'H': return TUI_KEY_HOME;
    case 'F': return TUI_KEY_END;
    case 'P': return TUI_KEY_F1;
    case 'Q': return TUI_KEY_F2;
    case 'R': return TUI_KEY_F3;
    case 'S': return TUI_KEY_F4;
    default:  return TUI_KEY_CHAR;
    }
}

#define MAX_PARAMS 8

/* p points at "ESC [".  Interprets a complete CSI sequence. */
static int parse_csi(tui_decoder *d, const unsigned char *p, size_t n,
                     tui_event *ev, size_t *used) {
    size_t i = 2;
    size_t params_start = i;
    while (i < n && p[i] >= 0x30 && p[i] <= 0x3F) i++;
    size_t params_end = i;
    while (i < n && p[i] >= 0x20 && p[i] <= 0x2F) i++;
    size_t inter_end = i;
    if (i >= n) {
        if (n >= TUI_DECODER_SEQ_MAX) {
            /* Too long to be a key: skip it, to its final byte. */
            d->skip_csi = 1;
            *used = n;
            return PARSE_DROPPED;
        }
        return PARSE_INCOMPLETE;
    }
    unsigned char f = p[i];
    if (f < 0x40 || f > 0x7E) {
        /* Not a well-formed CSI: drop what was read, keep the odd byte. */
        *used = i;
        return PARSE_DROPPED;
    }
    *used = i + 1;
    /* Whether it arrived whole or in pieces, a sequence this long is not a
     * key: the same input must decode the same way however it was read. */
    if (i + 1 > TUI_DECODER_SEQ_MAX) return PARSE_DROPPED;

    /* Private-marker forms ("<", "=", ">", "?") are reports and mouse
     * input, never keys; intermediates do not occur in key sequences. */
    if (inter_end != params_end) return PARSE_DROPPED;
    if (params_end > params_start && p[params_start] >= 0x3C) return PARSE_DROPPED;

    long param[MAX_PARAMS];
    int np = 0;
    long cur = -1;
    for (size_t k = params_start; k < params_end; k++) {
        unsigned char c = p[k];
        if (c >= '0' && c <= '9') {
            if (cur < 0) cur = 0;
            if (cur < 100000) cur = cur * 10 + (c - '0');
        } else if (c == ';') {
            if (np < MAX_PARAMS) param[np++] = cur;
            cur = -1;
        } else if (c == ':') {
            /* Sub-parameters (kitty's alternate keys): ignored, but the
             * main value before them stays. */
            while (k + 1 < params_end && p[k + 1] != ';') k++;
        } else {
            return PARSE_DROPPED;
        }
    }
    if (params_end > params_start && np < MAX_PARAMS) param[np++] = cur;

    long p1 = np > 0 && param[0] >= 0 ? param[0] : 1;
    long p2 = np > 1 && param[1] >= 0 ? param[1] : 1;
    unsigned mods = mods_from_param(p2);

    if (f == '~') {
        if (p1 == 200 && np <= 1) {
            d->in_paste = 1;
            d->paste_len = 0;
            d->paste_truncated = 0;
            return PARSE_DROPPED;     /* the paste event comes at its end */
        }
        tui_key k = tilde_key(p1);
        if (k == TUI_KEY_CHAR) return PARSE_DROPPED;
        key_event(ev, k, mods);
        return PARSE_EVENT;
    }
    if (f == 'Z') {
        key_event(ev, TUI_KEY_TAB, TUI_MOD_SHIFT | mods);
        return PARSE_EVENT;
    }
    if (f == 'u') {
        /* CSI code ; mods u -- the "fixterms"/kitty form some terminals
         * send for keys they cannot otherwise express. */
        if (np < 1 || param[0] < 0) return PARSE_DROPPED;
        switch (p1) {
        case 13:  key_event(ev, TUI_KEY_ENTER, mods); return PARSE_EVENT;
        case 9:   key_event(ev, TUI_KEY_TAB, mods); return PARSE_EVENT;
        case 27:  key_event(ev, TUI_KEY_ESC, mods); return PARSE_EVENT;
        case 127: key_event(ev, TUI_KEY_BACKSPACE, mods); return PARSE_EVENT;
        default:
            if (p1 < 0x20 || p1 > 0x10FFFF || (p1 >= 0x7F && p1 <= 0x9F))
                return PARSE_DROPPED;
            char_event(ev, (uint32_t)p1, mods);
            return PARSE_EVENT;
        }
    }
    tui_key k = final_key(f);
    if (k == TUI_KEY_CHAR) return PARSE_DROPPED;
    /* With a first parameter other than 1 these finals are something else:
     * "CSI row ; col R" is a cursor position report, not F3. */
    if (p1 != 1) return PARSE_DROPPED;
    key_event(ev, k, mods);
    return PARSE_EVENT;
}

/* p points at "ESC O": SS3, sent for cursor keys in application mode and
 * for F1-F4. */
static int parse_ss3(const unsigned char *p, size_t n, tui_event *ev,
                     size_t *used) {
    size_t i = 2;
    unsigned mods = 0;
    if (i < n && p[i] >= '0' && p[i] <= '9') {   /* "ESC O 5 A" (rare) */
        mods = mods_from_param(p[i] - '0');
        i++;
    }
    if (i >= n) return PARSE_INCOMPLETE;
    unsigned char f = p[i];
    *used = i + 1;
    if (f == 'M') {                              /* keypad Enter */
        key_event(ev, TUI_KEY_ENTER, mods);
        return PARSE_EVENT;
    }
    tui_key k = final_key(f);
    if (k == TUI_KEY_CHAR) {
        if (f < 0x40 || f > 0x7E) *used = i;     /* keep the odd byte */
        return PARSE_DROPPED;
    }
    key_event(ev, k, mods);
    return PARSE_EVENT;
}

/* ── Dispatch ───────────────────────────────────────────────────────── */

static int parse_one(tui_decoder *d, const unsigned char *p, size_t n,
                     tui_event *ev, size_t *used) {
    if (p[0] != ESC) return parse_plain(p, n, ev, used);
    if (n < 2) return PARSE_INCOMPLETE;
    if (p[1] == '[') return parse_csi(d, p, n, ev, used);
    if (p[1] == 'O') {
        if (n < 3) return PARSE_INCOMPLETE;
        return parse_ss3(p, n, ev, used);
    }
    if (p[1] == ESC) {
        /* Two ESC presses in one read: the first is a key of its own. */
        key_event(ev, TUI_KEY_ESC, 0);
        *used = 1;
        return PARSE_EVENT;
    }
    /* ESC followed by a key is that key with Alt (xterm's metaSendsEscape). */
    size_t k;
    int r = parse_plain(p + 1, n - 1, ev, &k);
    if (r == PARSE_INCOMPLETE) return r;
    *used = k + 1;
    if (r == PARSE_EVENT) {
        unsigned mods = ev->mods | TUI_MOD_ALT;
        if (ev->key == TUI_KEY_CHAR) char_event(ev, ev->cp, mods);
        else ev->mods = mods;
    }
    return r;
}

/* Appends paste text, keeping at most TUI_DECODER_PASTE_MAX bytes. */
static void paste_append(tui_decoder *d, const unsigned char *p, size_t n) {
    if (d->paste_len + n > TUI_DECODER_PASTE_MAX) {
        n = TUI_DECODER_PASTE_MAX - d->paste_len;
        d->paste_truncated = 1;
    }
    if (n == 0) return;
    if (d->paste_len + n + 1 > d->paste_cap) {
        size_t cap = d->paste_cap ? d->paste_cap : 1024;
        while (cap < d->paste_len + n + 1) cap *= 2;
        char *q = realloc(d->paste, cap);
        if (!q) {
            /* Out of memory: the paste is cut here rather than lost whole. */
            d->paste_truncated = 1;
            return;
        }
        d->paste = q;
        d->paste_cap = cap;
    }
    memcpy(d->paste + d->paste_len, p, n);
    d->paste_len += n;
    d->paste[d->paste_len] = '\0';
}

static void paste_event(tui_decoder *d, tui_event *ev) {
    memset(ev, 0, sizeof(*ev));
    ev->type = TUI_EV_PASTE;
    ev->paste = d->paste ? d->paste : "";
    ev->paste_len = d->paste_len;
    d->in_paste = 0;
}

/* Consumes paste text; 1 when the end marker was found (event filled). */
static int next_paste(tui_decoder *d, tui_event *ev) {
    const unsigned char *p = d->in + d->in_pos;
    size_t n = d->in_len - d->in_pos;
    for (size_t i = 0; i < n; i++) {
        if (p[i] != ESC) continue;
        size_t m = n - i < PASTE_END_LEN ? n - i : PASTE_END_LEN;
        if (memcmp(p + i, paste_end, m) != 0) continue;
        if (m < PASTE_END_LEN) {
            /* Possibly the start of the end marker: wait for more. */
            paste_append(d, p, i);
            d->in_pos += i;
            return 0;
        }
        paste_append(d, p, i);
        d->in_pos += i + PASTE_END_LEN;
        paste_event(d, ev);
        return 1;
    }
    paste_append(d, p, n);
    d->in_pos = d->in_len;
    return 0;
}

int tui_decoder_next(tui_decoder *d, tui_event *ev) {
    if (!d || !ev) return 0;
    for (;;) {
        if (d->in_paste) {
            if (next_paste(d, ev)) return 1;
            return 0;
        }
        if (d->in_pos >= d->in_len) {
            d->in_pos = d->in_len = 0;
            return 0;
        }
        if (d->skip_csi) {
            /* Drop the rest of an over-long sequence, final byte included. */
            while (d->in_pos < d->in_len) {
                unsigned char c = d->in[d->in_pos++];
                if (c >= 0x40 && c <= 0x7E) { d->skip_csi = 0; break; }
                if (c < 0x20 || c > 0x7E) { d->skip_csi = 0; d->in_pos--; break; }
            }
            continue;
        }
        size_t used = 0;
        int r = parse_one(d, d->in + d->in_pos, d->in_len - d->in_pos, ev, &used);
        /* An incomplete sequence is short -- at most an ESC, a CSI below
         * TUI_DECODER_SEQ_MAX (longer ones are skipped), or five bytes of
         * Alt+UTF-8 -- so it never fills the queue and feeding can go on. */
        if (r == PARSE_INCOMPLETE) return 0;
        d->in_pos += used;
        if (r == PARSE_EVENT) return 1;
    }
}

int tui_decoder_timeout(tui_decoder *d, tui_event *ev) {
    if (!d || !ev) return 0;
    if (tui_decoder_next(d, ev)) return 1;
    if (d->in_paste) {
        /* The end marker never came: deliver what arrived. */
        paste_append(d, d->in + d->in_pos, d->in_len - d->in_pos);
        d->in_pos = d->in_len = 0;
        paste_event(d, ev);
        return 1;
    }
    if (d->skip_csi) {
        d->skip_csi = 0;
        return 0;
    }
    const unsigned char *p = d->in + d->in_pos;
    size_t n = d->in_len - d->in_pos;
    if (n == 0) return 0;
    d->in_pos = d->in_len = 0;
    if (p[0] != ESC) {
        char_event(ev, TUI_CP_REPLACEMENT, 0);       /* half a character */
        return 1;
    }
    if (n == 1) {
        key_event(ev, TUI_KEY_ESC, 0);
        return 1;
    }
    if (n == 2 && (p[1] == '[' || p[1] == 'O')) {
        char_event(ev, p[1], TUI_MOD_ALT);
        return 1;
    }
    if (p[1] >= 0x80) {
        char_event(ev, TUI_CP_REPLACEMENT, TUI_MOD_ALT);
        return 1;
    }
    return 0;    /* an unfinished escape sequence: nothing a user typed */
}

/* ── Names ──────────────────────────────────────────────────────────── */

static const char *key_name(tui_key k) {
    static const char *const names[] = {
        "Enter", "Tab", "Backspace", "Esc", "Up", "Down", "Left", "Right",
        "Home", "End", "PgUp", "PgDn", "Insert", "Delete",
        "F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10", "F11", "F12"
    };
    unsigned i = (unsigned)k - (unsigned)TUI_KEY_ENTER;
    if (k >= TUI_KEY_ENTER && i < sizeof(names) / sizeof(names[0])) return names[i];
    return "?";
}

const char *tui_event_name(const tui_event *ev, char *buf, size_t size) {
    if (!buf || size == 0) return "";
    if (!ev) {
        snprintf(buf, size, "None");
        return buf;
    }
    switch (ev->type) {
    case TUI_EV_NONE:   snprintf(buf, size, "None"); return buf;
    case TUI_EV_RESIZE: snprintf(buf, size, "Resize %dx%d", ev->rows, ev->cols); return buf;
    case TUI_EV_FD:     snprintf(buf, size, "Fd %d", ev->fd_index); return buf;
    case TUI_EV_HANGUP: snprintf(buf, size, "Hangup"); return buf;
    case TUI_EV_PASTE:  snprintf(buf, size, "Paste %zu bytes", ev->paste_len); return buf;
    case TUI_EV_KEY:    break;
    }
    char base[16];
    if (ev->key == TUI_KEY_CHAR) {
        if (ev->cp == ' ') {
            snprintf(base, sizeof(base), "Space");
        } else {
            uint32_t cp = ev->cp;
            if ((ev->mods & TUI_MOD_CTRL) && cp >= 'a' && cp <= 'z') cp -= 0x20;
            char u[5];
            int k = tui_utf8_encode(tui_cp_sanitize(cp), u);
            u[k] = '\0';
            snprintf(base, sizeof(base), "%s", u);
        }
    } else {
        snprintf(base, sizeof(base), "%s", key_name(ev->key));
    }
    snprintf(buf, size, "%s%s%s%s",
             (ev->mods & TUI_MOD_CTRL) ? "Ctrl+" : "",
             (ev->mods & TUI_MOD_ALT) ? "Alt+" : "",
             (ev->mods & TUI_MOD_SHIFT) ? "Shift+" : "",
             base);
    return buf;
}
