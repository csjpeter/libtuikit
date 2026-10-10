/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* tui_buf: the growable output buffer. */

#include "tuikit/buf.h"
#include "internal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void tui_buf_init(tui_buf *b) {
    if (!b) return;
    b->data = NULL;
    b->len = 0;
    b->cap = 0;
    b->failed = 0;
}

void tui_buf_free(tui_buf *b) {
    if (!b) return;
    free(b->data);
    tui_buf_init(b);
}

void tui_buf_reset(tui_buf *b) {
    if (!b) return;
    b->len = 0;
    b->failed = 0;
    if (b->data) b->data[0] = '\0';
}

/* Makes room for @p extra more bytes plus a terminating NUL. */
static int reserve(tui_buf *b, size_t extra) {
    if (b->failed) {
        errno = ENOMEM;
        return -1;
    }
    if (extra > (size_t)-1 - b->len - 1) {
        b->failed = 1;
        errno = ENOMEM;
        return -1;
    }
    size_t need = b->len + extra + 1;
    if (need <= b->cap) return 0;
    size_t cap = b->cap ? b->cap : 256;
    while (cap < need) {
        if (cap > (size_t)-1 / 2) { cap = need; break; }
        cap *= 2;
    }
    char *p = realloc(b->data, cap);
    if (!p) {
        b->failed = 1;
        errno = ENOMEM;
        return -1;
    }
    b->data = p;
    b->cap = cap;
    return 0;
}

int tui_buf_append(tui_buf *b, const void *p, size_t n) {
    if (!b || (!p && n > 0)) {
        errno = EINVAL;
        return -1;
    }
    if (reserve(b, n) != 0) return -1;
    if (n > 0) memcpy(b->data + b->len, p, n);
    b->len += n;
    b->data[b->len] = '\0';
    return 0;
}

int tui_buf_puts(tui_buf *b, const char *s) {
    if (!s) {
        errno = EINVAL;
        return -1;
    }
    return tui_buf_append(b, s, strlen(s));
}

int tui_buf_printf(tui_buf *b, const char *fmt, ...) {
    if (!b || !fmt) {
        errno = EINVAL;
        return -1;
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) {
        b->failed = 1;
        return -1;
    }
    if (reserve(b, (size_t)n) != 0) return -1;
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
    return 0;
}

int tui_buf_write_fd(tui_buf *b, int fd) {
    if (!b) {
        errno = EINVAL;
        return -1;
    }
    if (b->failed) {
        /* A frame with a hole in it would draw garbage; drop it whole. */
        tui_buf_reset(b);
        errno = ENOMEM;
        return -1;
    }
    int rc = tui__write_all(fd, b->data, b->len);
    tui_buf_reset(b);
    return rc;
}
