# Functional coverage of the capabilities

Source of truth for policies/testing-model.md, "Every capability has a
functional test".  libtuikit is a library, so a capability is an entry point of
its public interface: every function the shared library exports, and every
inline function of the installed headers.  The terminal-facing and drawing
behaviour (the widgets, the terminal input and output functions, the width
table) is reached through those entry points.

## How a capability is tested

The functional tests are `tests/functional/cap_*.c`.  Each is a program built
like a consumer's: it links the **shared** library and includes the public
headers only, so no internal header or private member is used.  It shares no
code with the unit tests or with libptytest.  The checks are on results,
errors included (a refused open, a failed allocation-free path, an out of range
index), and on what is drawn: the bytes the screen sends, with the escape
sequences stripped for the text and kept for the colours and attributes.  The
terminal functions run on a real pseudo-terminal, with a second process typing
as the user.

The PTY tests of the demo and of the compatibility harness drive programs built
on the library; they do not replace these and are not listed here.

## The check

`tests/unit/check_coverage_map.sh` (test `unit.check-coverage-map`) lists the
exported functions with `nm -D` and the inline functions from the headers, and
fails when

- a function has no row below;
- a row names a function that no longer exists (a removed capability removes
  its row and its test);
- the test file of a row is missing, or does not call the function.

A new capability therefore cannot be added without its row, and a row cannot
be added without a test that calls it.

## Rows

One row per capability: the function, the test file and the phase (the
function of that file that exercises it).  A row with no test would say so and
name the ticket that closes the gap; there is none now.

## Version

| Capability | Functional test | Phase |
|---|---|---|
| `tui_version` | `tests/functional/cap_text.c` | `cap_version` |

## Output buffer

| Capability | Functional test | Phase |
|---|---|---|
| `tui_buf_append` | `tests/functional/cap_text.c` | `cap_buf` |
| `tui_buf_free` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_buf_init` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_buf_printf` | `tests/functional/cap_text.c` | `cap_buf` |
| `tui_buf_puts` | `tests/functional/cap_terminal.c` | `cap_term` |
| `tui_buf_reset` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_buf_write_fd` | `tests/functional/cap_text.c` | `cap_buf` |

## Text, UTF-8 and widths

| Capability | Functional test | Phase |
|---|---|---|
| `tui_cp_sanitize` | `tests/functional/cap_text.c` | `cap_width` |
| `tui_cp_width` | `tests/functional/cap_text.c` | `cap_width` |
| `tui_str_fit` | `tests/functional/cap_text.c` | `cap_width` |
| `tui_str_fit_ellipsis` | `tests/functional/cap_text.c` | `cap_width` |
| `tui_strn_width` | `tests/functional/cap_text.c` | `cap_width` |
| `tui_str_width` | `tests/functional/cap_text.c` | `cap_width` |
| `tui_utf8_decode` | `tests/functional/cap_text.c` | `cap_utf8` |
| `tui_utf8_encode` | `tests/functional/cap_text.c` | `cap_utf8` |
| `tui_utf8_seq_len` | `tests/functional/cap_text.c` | `cap_utf8` |

## Input events and decoder

| Capability | Functional test | Phase |
|---|---|---|
| `tui_decoder_feed` | `tests/functional/cap_text.c` | `cap_decoder` |
| `tui_decoder_free` | `tests/functional/cap_text.c` | `cap_decoder` |
| `tui_decoder_init` | `tests/functional/cap_text.c` | `cap_decoder` |
| `tui_decoder_next` | `tests/functional/cap_text.c` | `cap_decoder` |
| `tui_decoder_pending` | `tests/functional/cap_text.c` | `cap_decoder` |
| `tui_decoder_timeout` | `tests/functional/cap_text.c` | `cap_decoder` |
| `tui_event_name` | `tests/functional/cap_text.c` | `cap_event_name` |

## Screen, styles and rectangles

| Capability | Functional test | Phase |
|---|---|---|
| `tui_rect_center` | `tests/functional/cap_draw.c` | `cap_rect` |
| `tui_rect_inset` | `tests/functional/cap_draw.c` | `cap_rect` |
| `tui_rect_intersect` | `tests/functional/cap_draw.c` | `cap_rect` |
| `tui_rect_split_h` | `tests/functional/cap_draw.c` | `cap_rect` |
| `tui_rect_split_v` | `tests/functional/cap_draw.c` | `cap_rect` |
| `tui_screen_clear` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_screen_cols` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_screen_fill` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_screen_free` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_screen_invalidate` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_screen_new` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_screen_put` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_screen_render` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_screen_resize` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_screen_rows` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_screen_set_cursor` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_screen_set_sync` | `tests/functional/cap_draw.c` | `cap_screen` |
| `tui_rect_is_empty` (inline) | `tests/functional/cap_draw.c` | `cap_rect` |
| `tui_rect_make` (inline) | `tests/functional/cap_draw.c` | `cap_rect` |
| `tui_style_attrs` (inline) | `tests/functional/cap_draw.c` | `cap_style` |
| `tui_style_eq` (inline) | `tests/functional/cap_draw.c` | `cap_style` |
| `tui_style_make` (inline) | `tests/functional/cap_draw.c` | `cap_style` |

## Canvas

| Capability | Functional test | Phase |
|---|---|---|
| `tui_canvas_box` | `tests/functional/cap_draw.c` | `cap_canvas` |
| `tui_canvas_clear` | `tests/functional/cap_draw.c` | `cap_canvas` |
| `tui_canvas_fill` | `tests/functional/cap_draw.c` | `cap_canvas` |
| `tui_canvas_hline` | `tests/functional/cap_draw.c` | `cap_canvas` |
| `tui_canvas_of` | `tests/functional/cap_draw.c` | `cap_style` |
| `tui_canvas_put` | `tests/functional/cap_draw.c` | `cap_style` |
| `tui_canvas_put_aligned` | `tests/functional/cap_draw.c` | `cap_canvas` |
| `tui_canvas_set_cursor` | `tests/functional/cap_draw.c` | `cap_canvas` |
| `tui_canvas_sub` | `tests/functional/cap_draw.c` | `cap_canvas` |
| `tui_canvas_vline` | `tests/functional/cap_draw.c` | `cap_canvas` |
| `tui_canvas_cols` (inline) | `tests/functional/cap_draw.c` | `cap_canvas` |
| `tui_canvas_rect` (inline) | `tests/functional/cap_draw.c` | `cap_canvas` |
| `tui_canvas_rows` (inline) | `tests/functional/cap_draw.c` | `cap_canvas` |

## Terminal descriptors (tui_tty)

| Capability | Functional test | Phase |
|---|---|---|
| `tui_tty_cols` | `tests/functional/cap_terminal.c` | `cap_tty_size_and_mode` |
| `tui_tty_is` | `tests/functional/cap_terminal.c` | `cap_tty_size_and_mode` |
| `tui_tty_raw_enter` | `tests/functional/cap_terminal.c` | `cap_tty_size_and_mode` |
| `tui_tty_raw_exit` | `tests/functional/cap_terminal.c` | `cap_tty_size_and_mode` |
| `tui_tty_read_key` | `tests/functional/cap_terminal.c` | `cap_tty_input` |
| `tui_tty_read_password` | `tests/functional/cap_terminal.c` | `cap_tty_password` |
| `tui_tty_read_password_masked` | `tests/functional/cap_terminal.c` | `cap_tty_password` |
| `tui_tty_rows` | `tests/functional/cap_terminal.c` | `cap_tty_size_and_mode` |
| `tui_tty_wait` | `tests/functional/cap_terminal.c` | `cap_tty_input` |

## Terminal session (tui_term)

| Capability | Functional test | Phase |
|---|---|---|
| `tui_term_close` | `tests/functional/cap_terminal.c` | `cap_term` |
| `tui_term_fd` | `tests/functional/cap_terminal.c` | `cap_term` |
| `tui_term_flush_buf` | `tests/functional/cap_terminal.c` | `cap_term` |
| `tui_term_open` | `tests/functional/cap_terminal.c` | `cap_term` |
| `tui_term_opts_init` | `tests/functional/cap_terminal.c` | `cap_term` |
| `tui_term_resume` | `tests/functional/cap_terminal.c` | `cap_term` |
| `tui_term_size` | `tests/functional/cap_terminal.c` | `cap_term` |
| `tui_term_suspend` | `tests/functional/cap_terminal.c` | `cap_term` |
| `tui_term_wait` | `tests/functional/cap_terminal.c` | `cap_term` |
| `tui_term_write` | `tests/functional/cap_terminal.c` | `cap_term` |

## Line history and editor

| Capability | Functional test | Phase |
|---|---|---|
| `tui_history_add` | `tests/functional/cap_edit.c` | `cap_history` |
| `tui_history_count` | `tests/functional/cap_edit.c` | `cap_history` |
| `tui_history_free` | `tests/functional/cap_edit.c` | `cap_history` |
| `tui_history_get` | `tests/functional/cap_edit.c` | `cap_history` |
| `tui_history_init` | `tests/functional/cap_edit.c` | `cap_history` |
| `tui_lineedit_cursor_col` | `tests/functional/cap_edit.c` | `cap_lineedit` |
| `tui_lineedit_free` | `tests/functional/cap_edit.c` | `cap_lineedit` |
| `tui_lineedit_handle` | `tests/functional/cap_edit.c` | `cap_lineedit` |
| `tui_lineedit_init` | `tests/functional/cap_edit.c` | `cap_lineedit` |
| `tui_lineedit_insert` | `tests/functional/cap_edit.c` | `cap_lineedit` |
| `tui_lineedit_render` | `tests/functional/cap_edit.c` | `cap_lineedit` |
| `tui_lineedit_replace` | `tests/functional/cap_edit.c` | `cap_lineedit` |
| `tui_lineedit_run` | `tests/functional/cap_terminal.c` | `cap_lineedit_run` |
| `tui_lineedit_set_text` | `tests/functional/cap_edit.c` | `cap_lineedit` |
| `tui_readline` | `tests/functional/cap_terminal.c` | `cap_readline` |
| `tui_readline_opts_init` | `tests/functional/cap_terminal.c` | `cap_readline` |

## Multi-line editor

| Capability | Functional test | Phase |
|---|---|---|
| `tui_textedit_cursor` | `tests/functional/cap_edit.c` | `cap_textedit` |
| `tui_textedit_handle` | `tests/functional/cap_edit.c` | `cap_textedit` |
| `tui_textedit_init` | `tests/functional/cap_edit.c` | `cap_textedit` |
| `tui_textedit_insert` | `tests/functional/cap_edit.c` | `cap_textedit` |
| `tui_textedit_lines` | `tests/functional/cap_edit.c` | `cap_textedit` |
| `tui_textedit_render` | `tests/functional/cap_edit.c` | `cap_textedit` |
| `tui_textedit_set_text` | `tests/functional/cap_edit.c` | `cap_textedit` |
| `tui_textedit_summary` | `tests/functional/cap_edit.c` | `cap_textedit` |

## History view

| Capability | Functional test | Phase |
|---|---|---|
| `tui_histview_free` | `tests/functional/cap_edit.c` | `cap_histview` |
| `tui_histview_handle` | `tests/functional/cap_edit.c` | `cap_histview` |
| `tui_histview_init` | `tests/functional/cap_edit.c` | `cap_histview` |
| `tui_histview_modal` | `tests/functional/cap_edit.c` | `cap_histview` |
| `tui_histview_notice` | `tests/functional/cap_edit.c` | `cap_histview` |
| `tui_histview_render` | `tests/functional/cap_edit.c` | `cap_histview` |
| `tui_histview_selected` | `tests/functional/cap_edit.c` | `cap_histview` |
| `tui_histview_set_failed` | `tests/functional/cap_edit.c` | `cap_histview` |
| `tui_histview_set_rows` | `tests/functional/cap_edit.c` | `cap_histview` |

## Tree

| Capability | Functional test | Phase |
|---|---|---|
| `tui_tree_close_all` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_free` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_handle` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_init` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_is_open` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_marker` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_open_all` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_render` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_row_at` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_select_row` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_selected_row` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_set_open` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_set_rows` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_show_counts` | `tests/functional/cap_edit.c` | `cap_tree` |
| `tui_tree_show_matches` | `tests/functional/cap_edit.c` | `cap_tree` |

## Text view

| Capability | Functional test | Phase |
|---|---|---|
| `tui_textview_append` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_at_end` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_free` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_handle` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_init` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_lines` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_render` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_scroll` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_scroll_end` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_scroll_to` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_set_text` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_top` | `tests/functional/cap_edit.c` | `cap_textview` |
| `tui_textview_wrap` | `tests/functional/cap_edit.c` | `cap_textview` |

## List view

| Capability | Functional test | Phase |
|---|---|---|
| `tui_listview_end` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_handle` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_home` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_init` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_is_visible` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_move_down` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_move_up` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_page_down` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_page_up` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_render` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_reveal_selected` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_select` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_set_count` | `tests/functional/cap_edit.c` | `cap_listview` |
| `tui_listview_set_viewport` | `tests/functional/cap_edit.c` | `cap_listview` |

## Table

| Capability | Functional test | Phase |
|---|---|---|
| `tui_table_cell_prefix` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_cell_put` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_clear_marks` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_first_visible` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_format_header` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_format_row` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_free` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_handle` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_init` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_invert_marks` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_last_visible` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_mark_all` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_marked` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_marked_count` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_next_marked` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_render` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_render_header` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_render_row` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_row_kind` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_row_mark` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_row_style` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_row_text` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_select` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_selected` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_set_colors` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_set_count` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_set_marked` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_set_message` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_set_row_id` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_set_total` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_toggle_mark` | `tests/functional/cap_widgets.c` | `cap_table` |
| `tui_table_widths` | `tests/functional/cap_widgets.c` | `cap_table` |

## Status line

| Capability | Functional test | Phase |
|---|---|---|
| `tui_statusline_begin_prompt` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_clear_message` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_clear_wait` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_format` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_free` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_handle` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_hint_at` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_hint_count` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_hints_fit` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_init` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_message` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_prompt_active` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_prompt_text` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_render` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_set_help` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_set_hints` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_set_message` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_set_position` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_set_position_index` | `tests/functional/cap_widgets.c` | `cap_statusline` |
| `tui_statusline_set_wait` | `tests/functional/cap_widgets.c` | `cap_statusline` |

## Dialogs

| Capability | Functional test | Phase |
|---|---|---|
| `tui_dialog_checklist` | `tests/functional/cap_widgets.c` | `cap_dialog` |
| `tui_dialog_confirm` | `tests/functional/cap_widgets.c` | `cap_dialog` |
| `tui_dialog_free` | `tests/functional/cap_widgets.c` | `cap_dialog` |
| `tui_dialog_handle` | `tests/functional/cap_widgets.c` | `cap_dialog` |
| `tui_dialog_help` | `tests/functional/cap_widgets.c` | `cap_dialog` |
| `tui_dialog_is_checked` | `tests/functional/cap_widgets.c` | `cap_dialog` |
| `tui_dialog_menu` | `tests/functional/cap_widgets.c` | `cap_dialog` |
| `tui_dialog_message` | `tests/functional/cap_widgets.c` | `cap_dialog` |
| `tui_dialog_prompt` | `tests/functional/cap_widgets.c` | `cap_dialog` |
| `tui_dialog_render` | `tests/functional/cap_widgets.c` | `cap_dialog` |

## Fields and forms

| Capability | Functional test | Phase |
|---|---|---|
| `tui_field_choice` | `tests/functional/cap_widgets.c` | `cap_fieldset` |
| `tui_fieldset_focus` | `tests/functional/cap_widgets.c` | `cap_fieldset` |
| `tui_fieldset_free` | `tests/functional/cap_widgets.c` | `cap_fieldset` |
| `tui_fieldset_handle` | `tests/functional/cap_widgets.c` | `cap_fieldset` |
| `tui_fieldset_init` | `tests/functional/cap_widgets.c` | `cap_fieldset` |
| `tui_fieldset_render` | `tests/functional/cap_widgets.c` | `cap_fieldset` |
| `tui_fieldset_set_error` | `tests/functional/cap_widgets.c` | `cap_fieldset` |
| `tui_fieldset_update_completion` | `tests/functional/cap_widgets.c` | `cap_fieldset` |
| `tui_field_text` | `tests/functional/cap_widgets.c` | `cap_fieldset` |

## Key table and action menu

| Capability | Functional test | Phase |
|---|---|---|
| `tui_actionmenu_free` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_actionmenu_handle` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_actionmenu_open` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_actionmenu_render` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_actionmenu_run` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_key_reserved` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_keycode_name` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_keycode_of` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_keyhelp_free` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_keyhelp_open` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_keytable_find` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_keytable_init` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_nav_key` | `tests/functional/cap_widgets.c` | `cap_actions` |
| `tui_registry_verb` | `tests/functional/cap_widgets.c` | `cap_actions` |

## Gaps

None.  A capability without a functional test is listed here with the ticket
that closes it.
