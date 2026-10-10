/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Unit tests of tui_text: UTF-8, widths, sanitising and fitting. */

/* Unit tests of tui_text: UTF-8, widths, sanitising, fitting. */

#define _XOPEN_SOURCE 700
#include "test_helpers.h"
#include "tuikit/text.h"

#include <locale.h>
#include <wchar.h>

int g_tests_run = 0;
int g_tests_failed = 0;

typedef struct {
    const char *bytes;
    size_t      n;
    uint32_t    cp;
    int         used;
} DecodeCase;

static void test_decode_cases(void) {
    static const DecodeCase cases[] = {
        { "A", 1, 'A', 1 },
        { "\xC3\xA1", 2, 0xE1, 2 },              /* á */
        { "\xE4\xB8\xAD", 3, 0x4E2D, 3 },        /* 中 */
        { "\xF0\x9F\x98\x80", 4, 0x1F600, 4 },   /* 😀 */
        { "\xF4\x8F\xBF\xBF", 4, 0x10FFFF, 4 },  /* highest code point */
        { "\x80", 1, 0xFFFD, 1 },                /* lone continuation */
        { "\xC0\xAF", 2, 0xFFFD, 1 },            /* overlong '/' */
        { "\xC1\xBF", 2, 0xFFFD, 1 },            /* overlong */
        { "\xE0\x80\xAF", 3, 0xFFFD, 1 },        /* overlong 3-byte */
        { "\xED\xA0\x80", 3, 0xFFFD, 1 },        /* surrogate U+D800 */
        { "\xF0\x80\x80\xAF", 4, 0xFFFD, 1 },    /* overlong 4-byte */
        { "\xF4\x90\x80\x80", 4, 0xFFFD, 1 },    /* above U+10FFFF */
        { "\xF5\x80\x80\x80", 4, 0xFFFD, 1 },    /* invalid lead */
        { "\xFF", 1, 0xFFFD, 1 },
        { "\xC3", 1, 0xFFFD, 1 },                /* cut short by n */
        { "\xE4\xB8", 2, 0xFFFD, 2 },            /* cut short by n */
        { "\xE4" "A", 2, 0xFFFD, 1 },            /* broken by ASCII */
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        uint32_t cp = 0;
        int used = tui_utf8_decode(cases[i].bytes, cases[i].n, &cp);
        char what[64];
        snprintf(what, sizeof(what), "case %zu: code point", i);
        ASSERT_EQ_INT(cp, cases[i].cp, what);
        snprintf(what, sizeof(what), "case %zu: bytes used", i);
        ASSERT_EQ_INT(used, cases[i].used, what);
    }
    uint32_t cp = 7;
    ASSERT_EQ_INT(tui_utf8_decode("", 0, &cp), 0, "empty input");
    ASSERT_EQ_INT(tui_utf8_decode(NULL, 3, &cp), 0, "NULL input");
    ASSERT_EQ_INT(tui_utf8_decode("x", 1, NULL), 1, "NULL output pointer");
}

static void test_encode_roundtrip_every_code_point(void) {
    int bad = 0;
    uint32_t first_bad = 0;
    for (uint32_t cp = 0; cp <= 0x10FFFF; cp++) {
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;
        char b[4];
        int n = tui_utf8_encode(cp, b);
        uint32_t back = 0;
        int k = tui_utf8_decode(b, (size_t)n, &back);
        int expect_len = cp < 0x80 ? 1 : cp < 0x800 ? 2 : cp < 0x10000 ? 3 : 4;
        if (back != cp || k != n || n != expect_len) {
            if (!bad) first_bad = cp;
            bad++;
        }
    }
    char what[64];
    snprintf(what, sizeof(what), "code points failing the round trip (first U+%04X)", first_bad);
    ASSERT_EQ_INT(bad, 0, what);
    char b[4];
    ASSERT_EQ_INT(tui_utf8_encode(0xD800, b), 3, "surrogate encodes as U+FFFD");
    ASSERT(memcmp(b, "\xEF\xBF\xBD", 3) == 0, "surrogate bytes are U+FFFD");
    ASSERT_EQ_INT(tui_utf8_encode(0x110000, b), 3, "out of range encodes as U+FFFD");
}

static void test_seq_len(void) {
    ASSERT_EQ_INT(tui_utf8_seq_len('a'), 1, "ASCII");
    ASSERT_EQ_INT(tui_utf8_seq_len(0xC3), 2, "2-byte lead");
    ASSERT_EQ_INT(tui_utf8_seq_len(0xE4), 3, "3-byte lead");
    ASSERT_EQ_INT(tui_utf8_seq_len(0xF0), 4, "4-byte lead");
    ASSERT_EQ_INT(tui_utf8_seq_len(0x80), 1, "continuation");
    ASSERT_EQ_INT(tui_utf8_seq_len(0xC0), 1, "overlong lead");
    ASSERT_EQ_INT(tui_utf8_seq_len(0xF5), 1, "invalid lead");
}

static void test_widths(void) {
    ASSERT_EQ_INT(tui_cp_width('a'), 1, "a");
    ASSERT_EQ_INT(tui_cp_width(0xE1), 1, "á");
    ASSERT_EQ_INT(tui_cp_width(0x0151), 1, "ő");
    ASSERT_EQ_INT(tui_cp_width(0x4E2D), 2, "中");
    ASSERT_EQ_INT(tui_cp_width(0x1F600), 2, "😀");
    ASSERT_EQ_INT(tui_cp_width(0x0301), 0, "combining acute");
    ASSERT_EQ_INT(tui_cp_width(0x200B), 0, "zero width space");
    ASSERT_EQ_INT(tui_cp_width(0x0378), 1, "unassigned: never zero, never dropped");
    ASSERT_EQ_INT(tui_cp_width(0x00), -1, "NUL");
    ASSERT_EQ_INT(tui_cp_width(0x1B), -1, "ESC");
    ASSERT_EQ_INT(tui_cp_width(0x7F), -1, "DEL");
    ASSERT_EQ_INT(tui_cp_width(0x9B), -1, "8-bit CSI");
    ASSERT_EQ_INT(tui_cp_width(0xA0), 1, "no-break space");
    ASSERT_EQ_INT(tui_cp_width(0x10FFFF), 1, "last code point");
}

/* The committed table against the running glibc: a Unicode update in glibc
 * shows here, rather than being silently carried or silently missed. */
static void test_table_matches_glibc(void) {
    const char *loc = setlocale(LC_CTYPE, "C.UTF-8");
    ASSERT(loc != NULL, "the C.UTF-8 locale is available (a fixture, not optional)");
    int diff = 0;
    uint32_t first = 0;
    int first_ours = 0, first_glibc = 0;
    for (uint32_t cp = 0xA0; cp <= 0x10FFFF; cp++) {
        if (cp >= 0xD800 && cp <= 0xDFFF) continue;
        int g = wcwidth((wchar_t)cp);
        int expect = (g == 0 || g == 2) ? g : 1;
        int ours = tui_cp_width(cp);
        if (ours != expect) {
            if (!diff) { first = cp; first_ours = ours; first_glibc = g; }
            diff++;
        }
    }
    setlocale(LC_CTYPE, "C");
    char what[160];
    snprintf(what, sizeof(what),
             "code points whose width differs from glibc (first U+%04X: ours %d, "
             "glibc %d; regenerate with the regen-width target)",
             first, first_ours, first_glibc);
    ASSERT_EQ_INT(diff, 0, what);
}

/* In the "C" locale glibc reports -1 for every non-ASCII character; the
 * library must not depend on the program's locale (lesson 5 of the survey). */
static void test_width_independent_of_locale(void) {
    setlocale(LC_CTYPE, "C");
    ASSERT_EQ_INT(tui_cp_width(0xE1), 1, "á in the C locale");
    ASSERT_EQ_INT(tui_cp_width(0x4E2D), 2, "中 in the C locale");
    ASSERT_EQ_INT(tui_str_width("\xC3\xA1rv\xC3\xADzt\xC5\xB1r\xC5\x91"), 9, "árvíztűrő in the C locale");
}

static void test_sanitize(void) {
    ASSERT_EQ_INT(tui_cp_sanitize(0x1B), TUI_CP_SANITIZED, "ESC");
    ASSERT_EQ_INT(tui_cp_sanitize('\n'), TUI_CP_SANITIZED, "newline");
    ASSERT_EQ_INT(tui_cp_sanitize('\t'), TUI_CP_SANITIZED, "tab");
    ASSERT_EQ_INT(tui_cp_sanitize(0x7F), TUI_CP_SANITIZED, "DEL");
    ASSERT_EQ_INT(tui_cp_sanitize(0x9B), TUI_CP_SANITIZED, "8-bit CSI");
    ASSERT_EQ_INT(tui_cp_sanitize(0x85), TUI_CP_SANITIZED, "NEL");
    ASSERT_EQ_INT(tui_cp_sanitize('a'), 'a', "a");
    ASSERT_EQ_INT(tui_cp_sanitize(0xA0), 0xA0, "no-break space");
    ASSERT_EQ_INT(tui_cp_sanitize(0x4E2D), 0x4E2D, "中");
}

static void test_str_width(void) {
    ASSERT_EQ_INT(tui_str_width(""), 0, "empty");
    ASSERT_EQ_INT(tui_str_width(NULL), 0, "NULL");
    ASSERT_EQ_INT(tui_str_width("hello"), 5, "ASCII");
    ASSERT_EQ_INT(tui_str_width("\xE4\xB8\xAD\xE6\x96\x87"), 4, "中文");
    ASSERT_EQ_INT(tui_str_width("e\xCC\x81"), 1, "e + combining acute");
    ASSERT_EQ_INT(tui_str_width("a\x1b[31mb"), 7, "ESC drawn as one column");
    ASSERT_EQ_INT(tui_str_width("\xFF\xFE"), 2, "two malformed bytes, two U+FFFD");
    ASSERT_EQ_INT(tui_strn_width("\xE4\xB8\xAD\xE6\x96\x87", 3), 2, "first 3 bytes");
    ASSERT_EQ_INT(tui_strn_width("abc", 0), 0, "zero bytes");
    ASSERT_EQ_INT(tui_strn_width("ab\0cd", 5), 2, "stops at NUL");
}

static void test_str_fit(void) {
    int used = -1;
    ASSERT_EQ_INT(tui_str_fit("hello", 3, &used), 3, "bytes of hello in 3");
    ASSERT_EQ_INT(used, 3, "columns used");
    ASSERT_EQ_INT(tui_str_fit("\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97", 3, &used), 3,
                  "a wide character is not cut");
    ASSERT_EQ_INT(used, 2, "columns of one wide character");
    ASSERT_EQ_INT(tui_str_fit("e\xCC\x81x", 1, &used), 3, "combining mark kept with its base");
    ASSERT_EQ_INT(used, 1, "columns of e + mark");
    ASSERT_EQ_INT(tui_str_fit("abc", 0, &used), 0, "zero columns");
    ASSERT_EQ_INT(used, 0, "zero columns used");
    ASSERT_EQ_INT(tui_str_fit("abc", 10, NULL), 3, "everything fits");
    ASSERT_EQ_INT(tui_str_fit(NULL, 10, &used), 0, "NULL string");
}

static void test_fit_ellipsis(void) {
    char out[64];
    ASSERT_EQ_INT(tui_str_fit_ellipsis("hello world", 8, out, sizeof(out)), 8, "columns");
    ASSERT_EQ_STR(out, "hello w\xE2\x80\xA6", "shortened");
    ASSERT_EQ_INT(tui_str_fit_ellipsis("hello", 5, out, sizeof(out)), 5, "exact fit columns");
    ASSERT_EQ_STR(out, "hello", "exact fit is unchanged");
    ASSERT_EQ_INT(tui_str_fit_ellipsis("\xE4\xB8\xAD\xE6\x96\x87\xE5\xAD\x97", 4, out, sizeof(out)), 3,
                  "wide: columns");
    ASSERT_EQ_STR(out, "\xE4\xB8\xAD\xE2\x80\xA6", "wide: 中…");
    ASSERT_EQ_INT(tui_str_fit_ellipsis("abc", 1, out, sizeof(out)), 1, "one column");
    ASSERT_EQ_STR(out, "\xE2\x80\xA6", "one column is the ellipsis");
    ASSERT_EQ_INT(tui_str_fit_ellipsis("abc", 0, out, sizeof(out)), 0, "zero columns");
    ASSERT_EQ_STR(out, "", "zero columns: empty");
    char small[4];
    ASSERT_EQ_INT(tui_str_fit_ellipsis("abcdef", 10, small, sizeof(small)), -1, "buffer too small");
    ASSERT_EQ_STR(small, "abc", "as much as fits");
    ASSERT_EQ_INT(tui_str_fit_ellipsis("abcdef", 5, small, sizeof(small)), -1,
                  "buffer too small for the ellipsis");
    ASSERT_EQ_STR(small, "abc", "prefix without a cut character");
    ASSERT_EQ_INT(tui_str_fit_ellipsis("\xE4\xB8\xAD\xE6\x96\x87", 10, small, sizeof(small)), -1,
                  "wide text, small buffer");
    ASSERT_EQ_STR(small, "\xE4\xB8\xAD", "never half a character");
    ASSERT_EQ_INT(tui_str_fit_ellipsis("x", 3, NULL, 0), -1, "no buffer");
    ASSERT_EQ_INT(tui_str_fit_ellipsis(NULL, 3, out, sizeof(out)), 0, "NULL string");
}

static const TestCase tests[] = {
    TEST_CASE(test_decode_cases),
    TEST_CASE(test_encode_roundtrip_every_code_point),
    TEST_CASE(test_seq_len),
    TEST_CASE(test_widths),
    TEST_CASE(test_table_matches_glibc),
    TEST_CASE(test_width_independent_of_locale),
    TEST_CASE(test_sanitize),
    TEST_CASE(test_str_width),
    TEST_CASE(test_str_fit),
    TEST_CASE(test_fit_ellipsis),
};

int main(int argc, char **argv) {
    return RUN_SUITE("text", tests, 110, 60);
}
