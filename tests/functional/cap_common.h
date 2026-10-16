/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-16 */
/* Helpers of the capability tests: assertions, key events, screen text, a pty. */

#ifndef TUIKIT_CAP_COMMON_H
#define TUIKIT_CAP_COMMON_H

/*
 * The capability tests (docs/functional-coverage.md) drive libtuikit the way
 * a program does: the shared library and the installed public headers only,
 * no internal header, no private member.  They share no code with the unit
 * tests or with libptytest.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "tuikit/tuikit.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int cap_checks, cap_failed;

#define CHECK(cond, what) do { \
    cap_checks++; \
    if (!(cond)) { \
        cap_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, what); \
    } \
} while (0)

#define CHECK_INT(actual, expected, what) do { \
    cap_checks++; \
    long long _a = (long long)(actual), _e = (long long)(expected); \
    if (_a != _e) { \
        cap_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s: expected %lld, got %lld\n", \
                __FILE__, __LINE__, what, _e, _a); \
    } \
} while (0)

#define CHECK_STR(actual, expected, what) do { \
    cap_checks++; \
    const char *_a = (actual), *_e = (expected); \
    if (!_a || strcmp(_a, _e) != 0) { \
        cap_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s: expected \"%s\", got \"%s\"\n", \
                __FILE__, __LINE__, what, _e, _a ? _a : "(null)"); \
    } \
} while (0)

#define CHECK_HAS(hay, needle, what) do { \
    cap_checks++; \
    const char *_h = (hay), *_n = (needle); \
    if (!_h || !strstr(_h, _n)) { \
        cap_failed++; \
        fprintf(stderr, "FAIL %s:%d: %s: \"%s\" not in \"%s\"\n", \
                __FILE__, __LINE__, what, _n, _h ? _h : "(null)"); \
    } \
} while (0)

static int cap_finish(const char *name) {
    printf("%s: %d checks, %d failed\n", name, cap_checks, cap_failed);
    if (cap_checks < 10) {
        fprintf(stderr, "FAIL: only %d checks ran\n", cap_checks);
        return 1;
    }
    return cap_failed ? 1 : 0;
}

/* One event from the bytes a terminal sends, through the public decoder. */
static inline tui_event cap_ev(const char *bytes) {
    tui_decoder d;
    tui_event ev;
    memset(&ev, 0, sizeof ev);
    tui_decoder_init(&d);
    tui_decoder_feed(&d, bytes, strlen(bytes));
    if (!tui_decoder_next(&d, &ev) && tui_decoder_pending(&d))
        (void)tui_decoder_timeout(&d, &ev);
    tui_decoder_free(&d);
    return ev;
}

/* Feeds every character of @p s as a key to @p handle(obj, &ev). */
#define CAP_TYPE(handle, obj, s) do { \
    for (const char *_p = (s); *_p; ) { \
        char _k[5] = {0}; \
        int _n = tui_utf8_seq_len((unsigned char)*_p); \
        memcpy(_k, _p, (size_t)_n); \
        tui_event _e = cap_ev(_k); \
        (void)handle((obj), &_e); \
        _p += _n; \
    } \
} while (0)

/* What the screen would send a blank terminal, with escape sequences
 * removed: the text, row by row.  Returns a malloc'ed string. */
static inline char *cap_screen_text(tui_screen *s, char **raw_out) {
    tui_buf b;
    tui_buf_init(&b);
    tui_screen_invalidate(s);
    (void)tui_screen_render(s, &b);
    char *out = calloc(b.len + 1, 1);
    size_t o = 0;
    for (size_t i = 0; i < b.len; i++) {
        if (b.data[i] == 0x1B && i + 1 < b.len && b.data[i + 1] == '[') {
            i += 2;
            while (i < b.len && !(b.data[i] >= 0x40 && b.data[i] <= 0x7E))
                i++;
            if (i < b.len && b.data[i] == 'H')
                out[o++] = '|';
            continue;
        }
        if (b.data[i] == 0x1B && i + 1 < b.len) {   /* ESC 7, ESC 8, ... */
            i += 1;
            continue;
        }
        out[o++] = b.data[i];
    }
    if (raw_out) {
        *raw_out = malloc(b.len + 1);
        memcpy(*raw_out, b.data, b.len);
        (*raw_out)[b.len] = '\0';
    }
    tui_buf_free(&b);
    return out;
}

#endif /* TUIKIT_CAP_COMMON_H */
