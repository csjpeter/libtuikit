/* SPDX-License-Identifier: BSD-2-Clause */
/* Copyright 2026 Péter Császár */

#ifndef LIBTUIKIT_WIDTH_TABLE_H
#define LIBTUIKIT_WIDTH_TABLE_H

/* Sorted, non-overlapping ranges of code points whose width is 0 or 2.
 * Generated into width_table.c by tools/gen-width.c. */

#include <stdint.h>

typedef struct {
    uint32_t lo, hi;
    int      width;
} tui_width_range;

extern const tui_width_range tui__width_ranges[];
extern const unsigned        tui__width_range_count;

#endif /* LIBTUIKIT_WIDTH_TABLE_H */
