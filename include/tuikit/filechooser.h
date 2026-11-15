/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-15 */
/* Public API: tui_filechooser, the file and directory chooser dialog. */

#ifndef LIBTUIKIT_FILECHOOSER_H
#define LIBTUIKIT_FILECHOOSER_H

/**
 * @file tuikit/filechooser.h
 * @brief A modal dialog to open a file, choose a directory or name a file to save.
 *
 * A typing field with the path (Tab completes it) and, under it, the entries
 * of the current directory: directories first, then files, with size and date.
 * The field and the list follow each other.  The directory is read in a
 * background thread, so a slow mount does not stop the keys.
 *
 * Like tui_dialog it does not block and does not read the terminal: the
 * program routes events to it first while it is open (tui_filechooser_handle()),
 * draws it last (tui_filechooser_render()) and reads the outcome once its state
 * is no longer TUI_FC_ACTIVE.  @c notify_fd becomes readable when a directory
 * has been read: give it to tui_term_wait() as an extra descriptor and call
 * tui_filechooser_poll() when it is ready.  See tui_filechooser_open(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief What the chooser asks for. */
typedef enum tui_fc_mode {
    TUI_FC_OPEN = 0,   /**< an existing file */
    TUI_FC_DIR,        /**< a directory (only directories are listed) */
    TUI_FC_SAVE        /**< a file name to write; an existing file is asked about first */
} tui_fc_mode;

/** @brief Where the chooser stands. */
typedef enum tui_fc_state {
    TUI_FC_ACTIVE = 0, /**< still waiting for the user */
    TUI_FC_OK,         /**< chosen: see @c path */
    TUI_FC_CANCELLED   /**< Esc or Ctrl-C */
} tui_fc_state;

/** @brief A file chooser.  Read @c state, @c path and @c notify_fd. */
typedef struct tui_filechooser {
    tui_fc_mode  mode;
    tui_fc_state state;
    const char  *path;       /**< state OK: the absolute path chosen, without a
                                  trailing slash; valid until tui_filechooser_free() */
    int          notify_fd;  /**< readable when a background read has finished */
    void        *priv;       /* private */
} tui_filechooser;

/**
 * @brief Opens a chooser.
 * @param title   the frame's title; NULL: "Open file", "Choose directory" or "Save as"
 * @param mode    what is asked for
 * @param start   the directory to start in; NULL: the current directory
 * @param filter  a shell pattern (fnmatch(3)) the file names must match, e.g.
 *                "*.eml"; directories always show; NULL: every file
 * @return 0, or -1 with errno (EINVAL, ENOMEM, or what socketpair(2) says).
 */
TUI_API int tui_filechooser_open(tui_filechooser *fc, const char *title, tui_fc_mode mode,
                                 const char *start, const char *filter);

/** @brief Shows (non-zero) or hides (0, the default) the names that start with a dot.
 *         The user toggles it with Alt-H. */
TUI_API void tui_filechooser_set_hidden(tui_filechooser *fc, int show);

/** @brief 1 while a directory is being read. */
TUI_API int tui_filechooser_loading(const tui_filechooser *fc);

/**
 * @brief Takes in a finished background read.
 * @return 1 when the list changed (draw again), 0 otherwise.
 */
TUI_API int tui_filechooser_poll(tui_filechooser *fc);

/** @brief The directory the list shows (or is reading), absolute, with a trailing slash. */
TUI_API const char *tui_filechooser_dir(const tui_filechooser *fc);

/**
 * @brief Gives the chooser one event.
 * @return The state afterwards.  Events after it closed are ignored.
 */
TUI_API tui_fc_state tui_filechooser_handle(tui_filechooser *fc, const tui_event *ev);

/** @brief Draws the chooser centred in @p c and puts the cursor in the typing field. */
TUI_API void tui_filechooser_render(tui_filechooser *fc, tui_canvas c);

/** @brief Stops waiting for a read in progress and frees everything.  Safe after a failed open. */
TUI_API void tui_filechooser_free(tui_filechooser *fc);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_FILECHOOSER_H */
