/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-10-03 */
/* Public API: tui_lineedit, the single-line editor, its history and readline. */

#ifndef LIBTUIKIT_LINEEDIT_H
#define LIBTUIKIT_LINEEDIT_H

/**
 * @file tuikit/lineedit.h
 * @brief A single-line text editor, a history of lines, and an inline
 *        readline built on them.
 *
 * tui_lineedit is a pure model: tui_lineedit_handle() applies one event,
 * tui_lineedit_render() draws it on a canvas.  Around it:
 *
 *   - tui_lineedit_run(): email-cli's input_line_run(), a blocking loop
 *     drawing directly at a terminal row, with its completion hooks;
 *   - tui_readline(): tg-cli's rl_readline() and mamind's
 *     line_editor_read(), editing a line in a terminal that is not
 *     full-screen, with history, an interrupt flag and an idle timeout,
 *     and plain line input when the input is not a terminal.
 *
 * Everything is UTF-8 aware: the cursor moves and deletes by character,
 * and columns are display columns.  See tui_lineedit_init(3) and
 * tui_readline(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"

#include <signal.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── History ─────────────────────────────────────────────────────────── */

/** @brief Lines entered earlier, oldest first.  The members are private. */
typedef struct tui_history {
    char **items;
    int    count;
    int    max;
} tui_history;

/** @brief An empty history keeping at most @p max lines (<= 0: 256). */
TUI_API void tui_history_init(tui_history *h, int max);

/** @brief Frees every line. */
TUI_API void tui_history_free(tui_history *h);

/**
 * @brief Adds a copy of @p line.  An empty line, or one equal to the most
 *        recent, is not added; when full, the oldest line goes.
 * @return 0, or -1 with errno ENOMEM.
 */
TUI_API int tui_history_add(tui_history *h, const char *line);

/** @brief Number of lines. */
TUI_API int tui_history_count(const tui_history *h);

/** @brief Line @p i, 0 the oldest; NULL when out of range. */
TUI_API const char *tui_history_get(const tui_history *h, int i);

/* ── The editor ──────────────────────────────────────────────────────── */

/** @brief What an event did to the line. */
typedef enum tui_edit_result {
    TUI_EDIT_IGNORED = 0,  /**< not an editing event; the program may use it */
    TUI_EDIT_CHANGED,      /**< text or cursor changed (or a key was used) */
    TUI_EDIT_ACCEPT,       /**< Enter */
    TUI_EDIT_CANCEL,       /**< Esc or Ctrl-C */
    TUI_EDIT_EOF           /**< Ctrl-D on an empty line */
} tui_edit_result;

/** @brief Editor state.  The members are readable; @c tab_fn,
 *         @c shift_tab_fn, @c render_below and @c user may be set. */
typedef struct tui_lineedit {
    char        *buf;     /**< the caller's buffer */
    size_t       size;    /**< its capacity, NUL included */
    size_t       len;     /**< bytes of text */
    size_t       cur;     /**< cursor, a byte offset on a character boundary */
    int          scroll;  /**< first text column shown (horizontal scroll) */
    int          trow;    /**< terminal row of tui_lineedit_run(), 1-based */
    tui_history *history; /**< Up/Down browse it when set */
    int          hist_pos;   /* private: browsing position */
    char        *hist_saved; /* private: the line being edited before browsing */
    char        *kill;       /* private: text removed by Ctrl-K/U/W, for Ctrl-Y */
    /** Tab: forward completion (email-cli).  May be NULL. */
    void (*tab_fn)(struct tui_lineedit *le);
    /** Shift-Tab: reverse completion.  May be NULL. */
    void (*shift_tab_fn)(struct tui_lineedit *le);
    /** Called after each draw by tui_lineedit_run(), e.g. to show a list
     *  of completions below the line.  May be NULL. */
    void (*render_below)(const struct tui_lineedit *le);
    void        *user;    /**< for the hooks */
} tui_lineedit;

/**
 * @brief Starts editing in @p buf (capacity @p size) with @p initial text
 *        (may be NULL), the cursor at its end.  Hooks are cleared.
 */
TUI_API void tui_lineedit_init(tui_lineedit *le, char *buf, size_t size,
                               const char *initial);

/** @brief Frees the editor's own memory (not the buffer). */
TUI_API void tui_lineedit_free(tui_lineedit *le);

/**
 * @brief Replaces bytes [@p start, @p end) of the text with @p utf8 (as
 *        tui_lineedit_insert() inserts it) and puts the cursor after it.
 *        The range is clamped to the text and to character boundaries.
 *        A completion replaces the token being typed with it.
 */
TUI_API void tui_lineedit_replace(tui_lineedit *le, size_t start, size_t end,
                                  const char *utf8);

/** @brief Replaces the text, the cursor at its end (for completion hooks). */
TUI_API void tui_lineedit_set_text(tui_lineedit *le, const char *text);

/**
 * @brief Inserts text at the cursor.  Controls are dropped (a tab or a
 *        newline becomes a space); what does not fit in the buffer is not
 *        inserted, never half a character.
 * @return 1 if anything was inserted.
 */
TUI_API int tui_lineedit_insert(tui_lineedit *le, const char *utf8);

/**
 * @brief Applies one event.
 *
 * Characters and pastes are inserted.  Left/Right and Ctrl-B/Ctrl-F move
 * by character; Ctrl+Left/Right and Alt-B/Alt-F by word; Home, End,
 * Ctrl-A, Ctrl-E to the ends.  Backspace and Ctrl-H delete before the
 * cursor, Delete and Ctrl-D at it (Ctrl-D on an empty line is
 * TUI_EDIT_EOF).  Ctrl-K kills to the end, Ctrl-U to the start, Ctrl-W
 * and Alt-Backspace the word before; Ctrl-Y yanks the last kill.  Up and
 * Down browse the history; Tab and Shift-Tab call the hooks.  Enter
 * accepts; Esc and Ctrl-C cancel.
 */
TUI_API tui_edit_result tui_lineedit_handle(tui_lineedit *le, const tui_event *ev);

/**
 * @brief Draws @p prompt and the text on row 0 of @p c, scrolled so the
 *        cursor is visible, and puts the terminal cursor at it.
 * @return The canvas column of the cursor.
 */
TUI_API int tui_lineedit_render(tui_lineedit *le, tui_canvas c, const char *prompt,
                                tui_style st);

/** @brief Display columns of the text before the cursor. */
TUI_API int tui_lineedit_cursor_col(const tui_lineedit *le);

/**
 * @brief email-cli's input_line_run(): edits on terminal row @p row
 *        (1-based), drawing directly to @p out_fd and reading keys from
 *        @p in_fd, until Enter or Esc/Ctrl-C.
 *
 * The terminal is expected to be in raw mode.  Each draw clears the row,
 * writes @p prompt from column @p col (1-based) followed by the text,
 * calls @c render_below, then puts the cursor on the row and shows it.
 * On return the row below is cleared and the cursor hidden.
 * @return 1 on Enter, 0 on Esc or Ctrl-C, -1 when the input ended or an
 *         error occurred.
 */
TUI_API int tui_lineedit_run(tui_lineedit *le, int in_fd, int out_fd, int row, int col,
                             const char *prompt);

/* ── Inline readline ─────────────────────────────────────────────────── */

/** @brief Results of tui_readline() besides a length. */
enum {
    TUI_READLINE_EOF       = -1,  /**< end of input, Ctrl-D, or an error */
    TUI_READLINE_CANCEL    = -2,  /**< Ctrl-C or Esc (signal keys off) */
    TUI_READLINE_INTERRUPT = -3,  /**< *interrupt became non-zero */
    TUI_READLINE_TIMEOUT   = -4   /**< no key for timeout_sec seconds */
};

/** @brief Options of tui_readline(). */
typedef struct tui_readline_opts {
    int                    in_fd, out_fd;  /**< default 0 and 1 */
    tui_history           *history;        /**< may be NULL */
    volatile sig_atomic_t *interrupt;      /**< checked while waiting; may be NULL */
    int                    timeout_sec;    /**< idle timeout; 0: none */
    unsigned               flags;          /**< TUI_TERM_KEEP_ISIG: Ctrl-C stays SIGINT */
} tui_readline_opts;

/** @brief Defaults: stdin/stdout, no history, no interrupt, no timeout. */
TUI_API void tui_readline_opts_init(tui_readline_opts *o);

/**
 * @brief Reads one line with editing, on the current line of a terminal
 *        that is not full-screen.
 *
 * Writes @p prompt, enters raw mode, edits until Enter (then writes a
 * newline and adds the line to the history), and restores the mode.
 * When the input is not a terminal it writes the prompt and reads a plain
 * line.  @p o may be NULL for the defaults.
 * @return The length of the line in @p buf (NUL-terminated, truncated to
 *         @p size - 1 bytes), or one of TUI_READLINE_*.
 */
TUI_API int tui_readline(const char *prompt, char *buf, size_t size,
                         const tui_readline_opts *o);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_LINEEDIT_H */
