/* SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Declares the generated table of code-point display widths. */

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
