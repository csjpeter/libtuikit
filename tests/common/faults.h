/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Fault injection for the tests, by the linker. */

#ifndef TUIKIT_FAULTS_H
#define TUIKIT_FAULTS_H

/*
 * Fault injection for the tests, by the linker: a test binary linked with
 * -Wl,--wrap=<function> for the functions below sends the library's calls
 * through these wrappers, which pass them on unchanged until a test arms a
 * failure.  The shipped library contains none of this.
 *
 * The raise() wrapper writes the coverage counters (in a coverage build)
 * before the signal is raised, so the lines a fatal-signal handler ran are
 * counted although the process dies of the signal.
 */

#include <stddef.h>

typedef enum {
    F_MALLOC, F_CALLOC, F_REALLOC, F_PIPE, F_FCNTL, F_TCGETATTR, F_TCSETATTR,
    F_POLL, F_READ, F_WRITE, F_COUNT
} fault_fn;

/* Lets @p skip calls of @p fn through, then fails the next @p times calls
 * with errno @p err (the function's error return). */
void fault_arm(fault_fn fn, int skip, int times, int err);

/* tcgetattr succeeds but reports ICANON set: a terminal that silently
 * ignored part of a mode change. */
void fault_tcgetattr_lies(int skip, int times);

/* Disarms everything. */
void fault_reset(void);

/* Calls of @p fn since the last reset. */
int fault_calls(fault_fn fn);

#endif /* TUIKIT_FAULTS_H */
