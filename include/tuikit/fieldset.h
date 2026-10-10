/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Copyright 2026 Péter Császár */

#ifndef LIBTUIKIT_FIELDSET_H
#define LIBTUIKIT_FIELDSET_H

/**
 * @file tuikit/fieldset.h
 * @brief A form of several fields: text fields with completion, choice
 *        fields, focus, validation.
 *
 * The shape of email-cli's compose dialog (To/Cc/Bcc/Subject/Attach with an
 * address drop-down) and of issue-manager's new/edit form (title,
 * description, type, priority, implementer).  Like a dialog it neither
 * blocks nor reads the terminal: the program gives it events and draws it.
 * Behaviour that belongs to one program -- what a separator key does,
 * removing an attachment, checking a file -- stays in the program, which
 * sees every event first and may handle it before the form does.
 * See tui_fieldset_init(3).
 */

#include "tuikit/api.h"
#include "tuikit/canvas.h"
#include "tuikit/dialog.h"
#include "tuikit/event.h"
#include "tuikit/lineedit.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct tui_fieldset;

/** @brief Most completion candidates shown at once. */
#define TUI_FIELDSET_CAND_MAX 16

/** @brief One completion candidate. */
typedef struct tui_completion {
    const char *text;   /**< what replaces the current token */
    const char *label;  /**< what the drop-down shows; NULL: @c text */
} tui_completion;

/**
 * @brief Finds the candidates for @p token (the text being completed, may
 *        be empty) and writes at most @p max of them to @p out.  The
 *        strings must stay valid until the next call.
 * @return The number written.
 */
typedef int (*tui_complete_fn)(void *user, int field, const char *token,
                               tui_completion *out, int max);

/** @brief The kind of a field. */
typedef enum tui_field_kind {
    TUI_FIELD_TEXT = 0,   /**< a line of text */
    TUI_FIELD_CHOICE      /**< one of a fixed list; Left/Right/Space change it */
} tui_field_kind;

/** @brief A field.  Set it up with tui_field_text() or
 *         tui_field_choice(); the members are readable. */
typedef struct tui_field {
    tui_field_kind      kind;
    const char         *label;     /**< e.g. "To:"; not copied */
    tui_lineedit        edit;      /**< TEXT: edits the caller's buffer */
    const char         *separators;/**< TEXT: token separators, e.g. ";,"; NULL: the whole text */
    tui_complete_fn     complete;  /**< TEXT: completion; NULL: none */
    const char *const  *choices;   /**< CHOICE: not copied */
    int                 nchoices;
    int                 choice;    /**< CHOICE: the selected index */
} tui_field;

/** @brief A text field editing @p buf (capacity @p size) with @p initial text. */
TUI_API void tui_field_text(tui_field *fl, const char *label,
                                 char *buf, size_t size, const char *initial);

/** @brief A choice among @p n @p choices, @p selected first. */
TUI_API void tui_field_choice(tui_field *fl, const char *label,
                                   const char *const *choices, int n, int selected);

/**
 * @brief Checks the form on submit.  Returns -1 when it may close, or the
 *        index of the field to put the focus on, having written the reason
 *        into @p err (shown under the fields).
 */
typedef int (*tui_fieldset_validate_fn)(void *user, const struct tui_fieldset *f,
                                    char *err, size_t errsz);

/** @brief The form.  @c state, @c focus and the fields may be read. */
typedef struct tui_fieldset {
    tui_field      *fields;
    int                  nfields;
    int                  focus;     /**< the focused field */
    tui_dialog_state     state;     /**< OPEN until Enter (valid) or Esc */
    tui_fieldset_validate_fn validate;  /**< may be NULL */
    void                *user;      /**< for validate and complete */
    tui_completion       cand[TUI_FIELDSET_CAND_MAX];
    int                  ncand;     /**< candidates shown (0: no drop-down) */
    int                  cand_sel;  /**< the chosen candidate; -1: none (the cursor
                                         is in the field) */
    char                 error[256];
    tui_style            label_style, focus_style, value_style;
    tui_style            cand_style, cand_sel_style, error_style;
} tui_fieldset;

/** @brief A form over @p n caller-owned @p fields, focus on the first. */
TUI_API void tui_fieldset_init(tui_fieldset *f, tui_field *fields, int n);

/** @brief Frees what the fields' editors allocated. */
TUI_API void tui_fieldset_free(tui_fieldset *f);

/** @brief Moves the focus to field @p i (clamped); closes the drop-down. */
TUI_API void tui_fieldset_focus(tui_fieldset *f, int i);

/** @brief Shows @p msg under the fields (NULL or "" clears it).  Typing
 *         clears it too. */
TUI_API void tui_fieldset_set_error(tui_fieldset *f, const char *msg);

/**
 * @brief Asks the focused field's completion again for its current token
 *        and opens the drop-down when there are candidates.  The form does
 *        this itself after every edit; a program calls it after changing a
 *        field's text on its own.
 */
TUI_API void tui_fieldset_update_completion(tui_fieldset *f);

/**
 * @brief Gives the form one event.
 *
 * With the drop-down open, nothing is chosen at first: Down moves onto
 * the candidates and Up back (from the first one, into the field again);
 * Enter accepts a chosen candidate (it replaces the current token) and is
 * the form's Enter when none is chosen; Tab replaces the token with the
 * longest prefix all candidates share, when that is longer, and stays in
 * the field; Esc closes the drop-down.
 * Otherwise: Tab/Shift-Tab and Down/Up move between fields, Enter submits
 * (through @c validate), Esc or Ctrl-C cancel.  A choice field takes
 * Left/Right/Space; a text field takes every key of tui_lineedit_handle(3).
 * @return The state afterwards.
 */
TUI_API tui_dialog_state tui_fieldset_handle(tui_fieldset *f, const tui_event *ev);

/**
 * @brief Draws the form from the top of @p c: one row per field (label
 *        column, then the value), the drop-down under the focused field,
 *        then the error, if any.  Puts the cursor in a focused text field.
 * @return The rows used.
 */
TUI_API int tui_fieldset_render(tui_fieldset *f, tui_canvas c);

#ifdef __cplusplus
}
#endif

#endif /* LIBTUIKIT_FIELDSET_H */
