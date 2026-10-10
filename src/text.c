/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* UTF-8, display widths, sanitising and fitting of text. */

#include "tuikit/text.h"
#include "width_table.h"

#include <string.h>

int tui_utf8_seq_len(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 1;   /* continuation byte, C0/C1 overlong lead, F5..FF */
}

int tui_utf8_decode(const char *s, size_t n, uint32_t *cp) {
    uint32_t dummy;
    if (!cp) cp = &dummy;
    if (!s || n == 0) {
        *cp = 0;
        return 0;
    }
    const unsigned char *p = (const unsigned char *)s;
    unsigned char c = p[0];
    if (c < 0x80) {
        *cp = c;
        return 1;
    }
    int len = tui_utf8_seq_len(c);
    if (len == 1) {               /* not a valid lead byte */
        *cp = TUI_CP_REPLACEMENT;
        return 1;
    }
    uint32_t v = (len == 2) ? (uint32_t)(c & 0x1F)
               : (len == 3) ? (uint32_t)(c & 0x0F)
               :              (uint32_t)(c & 0x07);
    for (int i = 1; i < len; i++) {
        if ((size_t)i >= n) {
            /* Cut short by the end of the input: what is there is one
             * broken character. */
            *cp = TUI_CP_REPLACEMENT;
            return i;
        }
        unsigned char cc = p[i];
        if ((cc & 0xC0) != 0x80) {
            *cp = TUI_CP_REPLACEMENT;
            return 1;
        }
        /* The second byte's range excludes overlong forms, surrogates and
         * values above U+10FFFF (RFC 3629, section 4). */
        if (i == 1) {
            if ((c == 0xE0 && cc < 0xA0) || (c == 0xED && cc > 0x9F) ||
                (c == 0xF0 && cc < 0x90) || (c == 0xF4 && cc > 0x8F)) {
                *cp = TUI_CP_REPLACEMENT;
                return 1;
            }
        }
        v = (v << 6) | (uint32_t)(cc & 0x3F);
    }
    *cp = v;
    return len;
}

int tui_utf8_encode(uint32_t cp, char out[4]) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = TUI_CP_REPLACEMENT;
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static int is_control(uint32_t cp) {
    return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F);
}

int tui_cp_width(uint32_t cp) {
    if (is_control(cp)) return -1;
    if (cp < 0x300) return 1;     /* the table starts at U+0300 */
    unsigned lo = 0, hi = tui__width_range_count;
    while (lo < hi) {
        unsigned mid = lo + (hi - lo) / 2;
        const tui_width_range *r = &tui__width_ranges[mid];
        if (cp < r->lo) hi = mid;
        else if (cp > r->hi) lo = mid + 1;
        else return r->width;
    }
    return 1;
}

uint32_t tui_cp_sanitize(uint32_t cp) {
    return is_control(cp) ? TUI_CP_SANITIZED : cp;
}

/* Width as drawn: a control is drawn as its one-column replacement. */
static int drawn_width(uint32_t cp) {
    int w = tui_cp_width(cp);
    return w < 0 ? 1 : w;
}

int tui_strn_width(const char *s, size_t n) {
    if (!s) return 0;
    int cols = 0;
    size_t i = 0;
    while (i < n && s[i]) {
        uint32_t cp;
        const char *nul = memchr(s + i, '\0', n - i);
        size_t rest = nul ? (size_t)(nul - (s + i)) : n - i;
        int k = tui_utf8_decode(s + i, rest, &cp);
        if (k <= 0) break;
        cols += drawn_width(cp);
        i += (size_t)k;
    }
    return cols;
}

int tui_str_width(const char *s) {
    return s ? tui_strn_width(s, strlen(s)) : 0;
}

size_t tui_str_fit(const char *s, int cols, int *used) {
    int u = 0;
    size_t i = 0;
    if (s && cols > 0) {
        size_t n = strlen(s);
        while (i < n) {
            uint32_t cp;
            int k = tui_utf8_decode(s + i, n - i, &cp);
            int w = drawn_width(cp);
            if (u + w > cols) break;
            u += w;
            i += (size_t)k;
        }
    }
    if (used) *used = u;
    return i;
}

/* Copies the longest prefix of the first @p n bytes of @p s that ends on a
 * character boundary and fits, with its NUL, into @p outsz bytes. */
static void copy_prefix(const char *s, size_t n, char *out, size_t outsz) {
    size_t cut = 0;
    while (cut < n) {
        int k = tui_utf8_decode(s + cut, n - cut, NULL);
        if (k <= 0 || cut + (size_t)k + 1 > outsz) break;
        cut += (size_t)k;
    }
    memcpy(out, s, cut);
    out[cut] = '\0';
}

int tui_str_fit_ellipsis(const char *s, int cols, char *out, size_t outsz) {
    static const char ellipsis[] = "\xE2\x80\xA6";   /* U+2026, one column */
    if (!out || outsz == 0) return -1;
    out[0] = '\0';
    if (!s || cols <= 0) return 0;

    int whole;
    size_t n = strlen(s);
    if (tui_str_fit(s, cols, &whole) == n) {
        if (n + 1 > outsz) {
            copy_prefix(s, n, out, outsz);
            return -1;
        }
        memcpy(out, s, n + 1);
        return whole;
    }

    /* Does not fit: keep cols - 1 columns and add the ellipsis. */
    int used;
    size_t keep = tui_str_fit(s, cols - 1, &used);
    if (keep + sizeof(ellipsis) > outsz) {
        copy_prefix(s, keep, out, outsz);
        return -1;
    }
    memcpy(out, s, keep);
    memcpy(out + keep, ellipsis, sizeof(ellipsis));
    return used + 1;
}
