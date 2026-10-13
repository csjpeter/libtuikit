/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Public API: tui_screen, the cell grid, its renderer and the palette styles. */

#ifndef LIBTUIKIT_SCREEN_H
#define LIBTUIKIT_SCREEN_H

/**
 * @file tuikit/screen.h
 * @brief A grid of styled cells, rendered as the bytes that bring the
 *        terminal from what it shows to what the grid holds.
 *
 * Drawing changes the back grid; tui_screen_render() appends to a tui_buf
 * only the cells that differ from the front grid (what the terminal shows)
 * and then makes the two equal.  The screen writes to no descriptor: the
 * program writes the buffer, and a test feeds it to a terminal emulator.
 * See tui_screen_new(3).
 */

#include "tuikit/api.h"
#include "tuikit/buf.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief A colour: the terminal's default, one of 16, one of 256, or RGB. */
typedef uint32_t tui_color;

#define TUI_COLOR_DEFAULT      0u
#define TUI_COLOR_16(n)        (0x01000000u | ((uint32_t)(n) & 0x0Fu))
#define TUI_COLOR_256(n)       (0x02000000u | ((uint32_t)(n) & 0xFFu))
#define TUI_COLOR_RGB(r, g, b) (0x03000000u | (((uint32_t)(r) & 0xFFu) << 16) | \
                                (((uint32_t)(g) & 0xFFu) << 8) | ((uint32_t)(b) & 0xFFu))

#define TUI_BLACK          TUI_COLOR_16(0)
#define TUI_RED            TUI_COLOR_16(1)
#define TUI_GREEN          TUI_COLOR_16(2)
#define TUI_YELLOW         TUI_COLOR_16(3)
#define TUI_BLUE           TUI_COLOR_16(4)
#define TUI_MAGENTA        TUI_COLOR_16(5)
#define TUI_CYAN           TUI_COLOR_16(6)
#define TUI_WHITE          TUI_COLOR_16(7)
#define TUI_BRIGHT_BLACK   TUI_COLOR_16(8)
#define TUI_BRIGHT_RED     TUI_COLOR_16(9)
#define TUI_BRIGHT_GREEN   TUI_COLOR_16(10)
#define TUI_BRIGHT_YELLOW  TUI_COLOR_16(11)
#define TUI_BRIGHT_BLUE    TUI_COLOR_16(12)
#define TUI_BRIGHT_MAGENTA TUI_COLOR_16(13)
#define TUI_BRIGHT_CYAN    TUI_COLOR_16(14)
#define TUI_BRIGHT_WHITE   TUI_COLOR_16(15)

/** @brief Text attributes, combined with |. */
enum {
    TUI_ATTR_BOLD      = 1,
    TUI_ATTR_DIM       = 2,
    TUI_ATTR_ITALIC    = 4,
    TUI_ATTR_UNDERLINE = 8,
    TUI_ATTR_REVERSE   = 16
};

/** @brief How a cell is drawn. */
typedef struct tui_style {
    tui_color fg;
    tui_color bg;
    uint16_t  attrs;
} tui_style;

/** @brief The terminal's default colours, no attribute. */
#define TUI_STYLE_DEFAULT ((tui_style){ TUI_COLOR_DEFAULT, TUI_COLOR_DEFAULT, 0 })

/**
 * @name The workspace palette
 * The roles of policies/tui-interface-model.md, "Palette": a program uses
 * these and no other styles for these meanings.  All but the hints also
 * carry an attribute, so that a selection, a frame or an error is told
 * apart without colour.  The widgets use them as their defaults.
 * @{
 */
/** @brief The header bar: bold white on blue. */
#define TUI_STYLE_HEADER    ((tui_style){ TUI_WHITE, TUI_BLUE, TUI_ATTR_BOLD })
/** @brief The row or choice under the cursor.  As a style value it is bold
 *  reverse video, which is what the buttons of a dialog use; tui_table,
 *  the menus of tui_dialog and the candidate lists of tui_fieldset draw it
 *  as the one selection of the policy (a dark blue row for the colour
 *  depth, '>' in the first column) when a style is left at this value. */
#define TUI_STYLE_SELECTION ((tui_style){ TUI_COLOR_DEFAULT, TUI_COLOR_DEFAULT, TUI_ATTR_BOLD | TUI_ATTR_REVERSE })
/** @brief The active tab, a modal's frame, the focused field's label: bold cyan. */
#define TUI_STYLE_ACCENT    ((tui_style){ TUI_CYAN, TUI_COLOR_DEFAULT, TUI_ATTR_BOLD })
/** @brief The message after a write that worked: bold green. */
#define TUI_STYLE_OK        ((tui_style){ TUI_GREEN, TUI_COLOR_DEFAULT, TUI_ATTR_BOLD })
/** @brief A refusal, a failure, an invalid field: bold red. */
#define TUI_STYLE_ERROR     ((tui_style){ TUI_RED, TUI_COLOR_DEFAULT, TUI_ATTR_BOLD })
/** @brief Key hints and secondary columns: yellow. */
#define TUI_STYLE_HINT      ((tui_style){ TUI_YELLOW, TUI_COLOR_DEFAULT, 0 })
/** @brief What is going on while the user waits: italic yellow. */
#define TUI_STYLE_PROGRESS  ((tui_style){ TUI_YELLOW, TUI_COLOR_DEFAULT, TUI_ATTR_ITALIC })
/** @} */

/** @brief A style from its parts. */
static inline tui_style tui_style_make(tui_color fg, tui_color bg, uint16_t attrs) {
    tui_style s;
    s.fg = fg;
    s.bg = bg;
    s.attrs = attrs;
    return s;
}

/** @brief A style with attributes only. */
static inline tui_style tui_style_attrs(uint16_t attrs) {
    return tui_style_make(TUI_COLOR_DEFAULT, TUI_COLOR_DEFAULT, attrs);
}

/** @brief Two styles are drawn the same. */
static inline int tui_style_eq(tui_style a, tui_style b) {
    return a.fg == b.fg && a.bg == b.bg && a.attrs == b.attrs;
}

/** @brief The screen (opaque). */
typedef struct tui_screen tui_screen;

/**
 * @brief A screen of @p rows x @p cols blank cells.  The first render
 *        draws everything.
 * @return The screen, or NULL with errno (EINVAL, ENOMEM).
 */
TUI_API tui_screen *tui_screen_new(int rows, int cols);

/** @brief Frees the screen.  Safe with NULL. */
TUI_API void tui_screen_free(tui_screen *s);

/**
 * @brief Changes the size.  The content is cleared and the next render
 *        draws everything, as the terminal's content after a resize is
 *        not known.  Call it on every TUI_EV_RESIZE, even with the same
 *        size.
 * @return 0, or -1 with errno (EINVAL, ENOMEM; the screen is unchanged).
 */
TUI_API int tui_screen_resize(tui_screen *s, int rows, int cols);

/** @brief Rows of the screen (0 for NULL). */
TUI_API int tui_screen_rows(const tui_screen *s);

/** @brief Columns of the screen (0 for NULL). */
TUI_API int tui_screen_cols(const tui_screen *s);

/** @brief Fills every cell with a blank in @p style. */
TUI_API void tui_screen_clear(tui_screen *s, tui_style style);

/**
 * @brief Writes UTF-8 text from (@p row, @p col), at most @p max_cols
 *        columns (<= 0: to the right edge), clipped to the screen.
 *
 * Controls are drawn as U+00B7 (tui_cp_sanitize(3)); the bidirectional
 * controls (U+061C, U+200E/F, U+202A..202E, U+2066..2069) are dropped, so
 * that text from an untrusted source cannot reorder what is around it; a wide character
 * that does not fit is not drawn by halves; a combining mark joins the
 * character before it in the same text (a cell keeps two).  Writing over
 * half of a wide character blanks the other half.
 * @return The columns written.
 */
TUI_API int tui_screen_put(tui_screen *s, int row, int col, int max_cols,
                           const char *utf8, tui_style style);

/**
 * @brief Fills @p n cells from (@p row, @p col) with the character @p cp
 *        (a space if it is not one column wide), clipped to the screen.
 */
TUI_API void tui_screen_fill(tui_screen *s, int row, int col, int n,
                             uint32_t cp, tui_style style);

/**
 * @brief Where the terminal cursor is left after a render, and whether it
 *        is shown.  Hidden by default.
 */
TUI_API void tui_screen_set_cursor(tui_screen *s, int row, int col, int visible);

/**
 * @brief Forgets what the terminal shows: the next render draws every
 *        cell.  For when something else wrote to the terminal.
 */
TUI_API void tui_screen_invalidate(tui_screen *s);

/**
 * @brief Wraps each non-empty frame in synchronized-update mode
 *        (CSI ? 2026 h / l), so a terminal that knows it shows the frame
 *        at once.  On by default; terminals that do not know it ignore it.
 */
TUI_API void tui_screen_set_sync(tui_screen *s, int on);

/**
 * @brief Appends to @p out the bytes that make the terminal show the
 *        back grid, then records it as shown.
 * @return The number of bytes appended (0 when nothing changed), or -1
 *         with errno when the buffer failed (the frame is then not
 *         recorded as shown, and the next render draws everything).
 */
TUI_API long tui_screen_render(tui_screen *s, tui_buf *out);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_SCREEN_H */
