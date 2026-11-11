/* SPDX-License-Identifier: GPL-3.0-only */
/* Copyright (c) 2026 Peter Csaszar (Császár Péter) */
/* Created: 2026-11-11 */
/* Public API: tui_cmdline, the vim-like command line opened by ':'. */

#ifndef LIBTUIKIT_CMDLINE_H
#define LIBTUIKIT_CMDLINE_H

/**
 * @file tuikit/cmdline.h
 * @brief The command line of policies/tui-interface-model.md: one input
 *        line at the bottom of the screen, opened by ':'.
 *
 * A program registers its commands (name, help text, handler); until it
 * registers one, ':' stays unbound (tui_cmdline_handle() ignores it).  While
 * the line is open it takes every event: Esc (or Backspace on an empty line)
 * cancels, Enter runs the command, Up and Down browse the lines run before,
 * Tab completes the command name from the registered commands.  The result
 * of a command is a message line, shown by tui_cmdline_render() until the
 * line is opened again.  See tui_cmdline_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/event.h"
#include "tuikit/lineedit.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Size of the line and of a message, NUL included. */
#define TUI_CMDLINE_MAX 256

/**
 * @brief A command's handler.  @p args is the text after the name (leading
 *        blanks removed, may be empty).  The handler writes the message to
 *        show into @p msg (capacity @p size) and returns 0 for success
 *        (shown as ok) or non-zero for failure (shown as an error).
 */
typedef int (*tui_cmdline_fn)(void *user, const char *args, char *msg, size_t size);

/** @brief What tui_cmdline_handle() did with an event. */
typedef enum tui_cmdline_result {
    TUI_CMDLINE_IGNORED = 0,  /**< the line is closed and the event is not ':' (or nothing is registered) */
    TUI_CMDLINE_EDITING,      /**< the line is open and used the event */
    TUI_CMDLINE_RAN,          /**< Enter: the command ran (or was unknown); see tui_cmdline_message() */
    TUI_CMDLINE_CANCELLED     /**< the line was closed without running anything */
} tui_cmdline_result;

/** @brief The command line.  The members are private. */
typedef struct tui_cmdline {
    void        *cmds;                 /* private: the registered commands */
    int          ncmds;
    int          open;
    int          msg_ok;
    tui_history  history;
    tui_lineedit edit;
    char         buf[TUI_CMDLINE_MAX];
    char         msg[TUI_CMDLINE_MAX];
    char         hint[TUI_CMDLINE_MAX];  /* the candidates of an ambiguous Tab */
} tui_cmdline;

/** @brief An empty, closed command line with no commands: ':' is unbound. */
TUI_API void tui_cmdline_init(tui_cmdline *cl);

/** @brief Frees everything the command line owns. */
TUI_API void tui_cmdline_free(tui_cmdline *cl);

/**
 * @brief Registers a command.  @p name is one word without blanks; @p help
 *        is a short description (may be NULL).  Both are copied.  Registering
 *        a name again replaces it.
 * @return 0, or -1 with errno EINVAL (bad name) or ENOMEM.
 */
TUI_API int tui_cmdline_register(tui_cmdline *cl, const char *name, const char *help,
                                 tui_cmdline_fn fn, void *user);

/** @brief Number of registered commands (0: ':' is unbound). */
TUI_API int tui_cmdline_count(const tui_cmdline *cl);

/** @brief The help text of command @p i (registration order), or NULL. */
TUI_API const char *tui_cmdline_help(const tui_cmdline *cl, int i);

/** @brief The name of command @p i (registration order), or NULL. */
TUI_API const char *tui_cmdline_name(const tui_cmdline *cl, int i);

/**
 * @brief Applies one event: ':' opens the line (when a command is
 *        registered), then every event edits it.  See tui_cmdline_result.
 */
TUI_API tui_cmdline_result tui_cmdline_handle(tui_cmdline *cl, const tui_event *ev);

/** @brief 1 while the line is open and takes every event. */
TUI_API int tui_cmdline_is_open(const tui_cmdline *cl);

/**
 * @brief The result message of the last command ("" when none), and in
 *        @p ok (may be NULL) whether it succeeded.
 */
TUI_API const char *tui_cmdline_message(const tui_cmdline *cl, int *ok);

/**
 * @brief Draws the last row of @p c: the open line (":" and the text, the
 *        terminal cursor in it), else the message in the ok or error role,
 *        else nothing.  When the line is open and a Tab found several
 *        candidates, they are listed on the row above, in the hint role.
 */
TUI_API void tui_cmdline_render(tui_cmdline *cl, tui_canvas c);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_CMDLINE_H */
