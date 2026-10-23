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
    TUI_ATTR_REVERSE   = 16,
    TUI_ATTR_STRIKE    = 32,
    TUI_ATTR_BLINK     = 64
};

/** @brief How a cell is drawn. */
typedef struct tui_style {
    tui_color fg;
    tui_color bg;
    uint16_t  attrs;
} tui_style;

/** @brief The terminal's default colours, no attribute. */
#define TUI_STYLE_DEFAULT ((tui_style){ TUI_COLOR_DEFAULT, TUI_COLOR_DEFAULT, 0 })

/** @brief The roles; the key in a scheme file is tui_role_name().  A role
 *         is a style a widget or a program asks for by meaning; the active
 *         colour scheme (tuikit/scheme.h) says how it looks. */
typedef enum tui_role {
    TUI_ROLE_TEXT,
    TUI_ROLE_HEADER,
    TUI_ROLE_STATUS,
    TUI_ROLE_SELECTION,
    TUI_ROLE_CURSOR,
    TUI_ROLE_ACCENT,
    TUI_ROLE_FRAME,
    TUI_ROLE_INPUT,
    TUI_ROLE_INPUT_FOCUS,
    TUI_ROLE_TAB_ACTIVE,
    TUI_ROLE_TAB_INACTIVE,
    TUI_ROLE_OK,
    TUI_ROLE_ERROR,
    TUI_ROLE_HINT,
    TUI_ROLE_PROGRESS,
    TUI_ROLE_COUNT
} tui_role;

/** @brief The style of a role in the active scheme (tuikit/scheme.h). */
TUI_API tui_style tui_scheme_style(tui_role role);

/**
 * @brief The selection role as a style value: the scheme's own selection
 *        style, and for the built-in selection bold reverse video (what a
 *        button in focus always was; the built-in dark blue row is drawn by
 *        the widgets that own a selection, by colour depth).  This is what
 *        TUI_STYLE_SELECTION is.
 */
TUI_API tui_style tui_selection_style(void);

/**
 * @name The workspace palette
 * The roles of policies/tui-interface-model.md, "Palette": a program uses
 * these and no other styles for these meanings.  Each is an expression that
 * looks the role up in the active colour scheme at run time (the default
 * scheme holds the colours named below), so it is a value and not a
 * constant: it cannot initialise a static object.  All but the hints also
 * carry an attribute in the default scheme, so that a selection, a frame or
 * an error is told apart without colour.  The widgets draw with the roles.
 * @{
 */
/** @brief The header bar, role "header": bold white on blue by default. */
#define TUI_STYLE_HEADER    tui_scheme_style(TUI_ROLE_HEADER)
/** @brief The row or choice under the cursor, role "selection" (a dark blue
 *  background by default, bold reverse video as a style value).  tui_table, the menus of tui_dialog and the
 *  candidate lists of tui_fieldset draw it as the one selection of the
 *  policy ('>' in the first column, the colours composed with the row's)
 *  when a style is left at this value; the buttons of a dialog draw it in
 *  bold, and in reverse video by default. */
#define TUI_STYLE_SELECTION tui_selection_style()
/** @brief Role "accent": the focused field's label, a quick key; bold cyan. */
#define TUI_STYLE_ACCENT    tui_scheme_style(TUI_ROLE_ACCENT)
/** @brief Role "ok", the message after a write that worked: bold green. */
#define TUI_STYLE_OK        tui_scheme_style(TUI_ROLE_OK)
/** @brief Role "error", a refusal, a failure, an invalid field: bold red. */
#define TUI_STYLE_ERROR     tui_scheme_style(TUI_ROLE_ERROR)
/** @brief Role "hint", key hints and secondary columns: yellow. */
#define TUI_STYLE_HINT      tui_scheme_style(TUI_ROLE_HINT)
/** @brief Role "progress", what is going on while the user waits: italic yellow. */
#define TUI_STYLE_PROGRESS  tui_scheme_style(TUI_ROLE_PROGRESS)
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
