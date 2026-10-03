/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

#ifndef TUIKIT_TEST_HELPERS_H
#define TUIKIT_TEST_HELPERS_H

/*
 * The project's own test runner: no external test framework.  The
 * assertion counters and the screen assertions are libptytest's
 * (pty_assert.h); libptytest is a dependency of the tests only.
 *
 * One executable per suite, so a crash in one suite cannot take another's
 * results with it.  Each suite:
 *   - runs under an alarm, so a hang fails instead of waiting for CI;
 *   - takes an optional filter argument to run only the tests whose name
 *     contains it, and "-l" to list them;
 *   - reports its assertion count and fails if a full run made fewer
 *     assertions than its floor, so a deleted registration line or an
 *     early return cannot pass unnoticed.
 *
 * Every equality assertion prints the observed value next to the expected
 * one.  See policies/testing-model.md.
 */

#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif

#include "pty_assert.h"

#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern int g_tests_run;
extern int g_tests_failed;

typedef struct {
    const char *name;
    void (*fn)(void);
} TestCase;

#define TEST_CASE(fn) { #fn, fn }

/* Writes @p n bytes of @p p into @p out as a C-like literal, escaping
 * everything that is not printable ASCII. */
static inline void test_escape(const char *p, size_t n, char *out, size_t outsz) {
    size_t o = 0;
    for (size_t i = 0; i < n && o + 5 < outsz; i++) {
        unsigned char c = (unsigned char)p[i];
        if (c == 0x1B)       o += (size_t)snprintf(out + o, outsz - o, "\\e");
        else if (c == '\\')  o += (size_t)snprintf(out + o, outsz - o, "\\\\");
        else if (c == '"')   o += (size_t)snprintf(out + o, outsz - o, "\\\"");
        else if (c < 0x20 || c >= 0x7F)
                             o += (size_t)snprintf(out + o, outsz - o, "\\x%02X", c);
        else                 out[o++] = (char)c;
    }
    out[o < outsz ? o : outsz - 1] = '\0';
}

#define ASSERT_EQ_INT(actual, expected, what) do { \
    g_tests_run++; \
    long long _a = (long long)(actual), _e = (long long)(expected); \
    if (_a != _e) { \
        PTY_ASSERT_FAIL_("%s: expected %lld, got %lld", what, _e, _a); \
        return; \
    } \
} while (0)

#define ASSERT_EQ_STR(actual, expected, what) do { \
    g_tests_run++; \
    const char *_a = (actual), *_e = (expected); \
    if (!_a || strcmp(_a, _e) != 0) { \
        char _ea[512], _ee[512]; \
        test_escape(_a ? _a : "(null)", _a ? strlen(_a) : 6, _ea, sizeof(_ea)); \
        test_escape(_e, strlen(_e), _ee, sizeof(_ee)); \
        PTY_ASSERT_FAIL_("%s: expected \"%s\", got \"%s\"", what, _ee, _ea); \
        return; \
    } \
} while (0)

/* Exact bytes: @p len bytes at @p data against the literal @p expected. */
#define ASSERT_BYTES(data, len, expected, what) do { \
    g_tests_run++; \
    const char *_d = (const char *)(data); size_t _l = (len); \
    const char *_x = (expected); size_t _xl = strlen(_x); \
    if (_l != _xl || (_l && memcmp(_d, _x, _l) != 0)) { \
        char _ea[2048], _ee[2048]; \
        test_escape(_d ? _d : "", _d ? _l : 0, _ea, sizeof(_ea)); \
        test_escape(_x, _xl, _ee, sizeof(_ee)); \
        PTY_ASSERT_FAIL_("%s:\n    expected \"%s\"\n    got      \"%s\"", what, _ee, _ea); \
        return; \
    } \
} while (0)

static inline int test_run_suite(const char *suite, const TestCase *tests,
                                 size_t count, int min_assertions,
                                 unsigned alarm_seconds, int argc, char **argv) {
    const char *filter = argc > 1 ? argv[1] : NULL;

    if (filter && strcmp(filter, "-l") == 0) {
        for (size_t i = 0; i < count; i++) printf("%s\n", tests[i].name);
        return EXIT_SUCCESS;
    }

    /* SIGALRM's default action terminates the process abnormally, which
     * CTest counts as a failure. */
    alarm(alarm_seconds);
    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("--- %s ---\n", suite);
    int selected = 0;
    for (size_t i = 0; i < count; i++) {
        if (filter && !strstr(tests[i].name, filter)) continue;
        selected++;
        int failed_before = g_tests_failed;
        printf("  %s...\n", tests[i].name);
        tests[i].fn();
        if (g_tests_failed != failed_before) printf("  -> %s FAILED\n", tests[i].name);
    }

    printf("\n--- Results: %s ---\n", suite);
    printf("Tests:            %d\n", selected);
    printf("Assertions run:   %d\n", g_tests_run);
    printf("Assertions failed:%d\n", g_tests_failed);

    if (filter && selected == 0) {
        printf("FAIL: no test matches \"%s\"\n", filter);
        return EXIT_FAILURE;
    }
    if (!filter && g_tests_run < min_assertions) {
        printf("FAIL: %d assertions, fewer than the floor of %d\n",
               g_tests_run, min_assertions);
        return EXIT_FAILURE;
    }
    return g_tests_failed > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}

#define RUN_SUITE(name, tests, floor, alarm_s) \
    test_run_suite(name, tests, sizeof(tests) / sizeof(tests[0]), floor, alarm_s, argc, argv)

#endif /* TUIKIT_TEST_HELPERS_H */
