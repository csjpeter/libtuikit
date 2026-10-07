# tuikit-demo: the elements and the tests that check them

The inventory policies/testing-model.md asks for ("Every user-interface
element is tested", "A test of an element checks all of it"): every element
of `tuikit-demo` a user can see or operate, its states, and the PTY test in
`tests/pty/test_demo.c` that checks it through a real terminal at 80x24.

"Text" is a cell-exact comparison (a full screen, or a region where the rest
of the screen is checked elsewhere); "style" is a style map of attributes,
foreground and background for each cell, in the palette of
policies/tui-interface-model.md. Every full screen is also a position check.

## Reader

| Element | States | Checked | Test |
|---|---|---|---|
| Header bar | unfiltered (`n messages`), filtered (`n of N · filter: x`), 90 columns wide | text, style (bold white on blue, full width) | `test_reader_at_rest`, `test_filter`, `test_too_small_and_resize` |
| Messages pane frame | focused (accent), not focused (plain) | text, style | `test_reader_at_rest`, `test_reader_list_and_body` |
| List column header `Subject` | at rest | text, style (bold) | `test_reader_at_rest` |
| List row | selected and focused (selection), selected not focused (bold), not selected (plain) | text, style | `test_reader_at_rest`, `test_reader_list_and_body`, `test_filter` |
| List keys | j, k, Up, Down, ctrl-f, ctrl-b, g, G, Home, End | selection row and style after each | `test_reader_list_and_body` |
| Empty list | filter with no match: `Nothing matches.` and `c clears the filter.` | text, style (plain, hint); Enter, j, G change nothing | `test_filter` |
| Message pane | focused (accent frame), not focused; title is the subject | text, style | `test_reader_list_and_body` |
| Message body | top, a line down (j), a page (ctrl-f), the end (G), back (g, ctrl-b) | text of the first row after each | `test_reader_list_and_body` |
| Going in and out of the body | Enter, Tab, l, Right in; Esc, q, Tab, h, Left back | frame styles after each | `test_reader_list_and_body` |
| Status line | hints of the list, hints of the body, a plain message, an ok message, cleared by the next key | text, style (hint yellow, plain, bold green) | `test_reader_at_rest`, `test_reader_list_and_body`, `test_compose_refusals_and_discard`, `test_compose_and_send` |
| Unbound keys, q on the top level | nothing changes, the program keeps running | the next key still works | `test_reader_at_rest` |
| Esc on the top level | quits with status 0 | exit status | `test_reader_at_rest` |
| Help (`?`) | of the list, of the body; j, x and Enter do nothing; q, Backspace and Esc go back | text, style (accent frame, bold keys), the list unmoved | `test_help` |
| Filter prompt (`/`) | typed, cancelled (filter kept), confirmed | text, style (accent frame), cursor | `test_filter` |
| `c` | clears a filter, keeps the cursor on the same message; nothing without a filter | header text, selection style | `test_filter` |
| Too small notice | below 80 columns, below 24 rows, keys ignored, Esc quits; back to 80x24 | text, style (bold red title, hint) | `test_too_small_and_resize` |
| ctrl-z | stops the program, gives the terminal back; fg repaints the same view | shell's report, selection style after | `test_suspend` |
| ctrl-c | quits at once from the reader, the help, the prompt, the fields and the body | exit status | `test_reader_list_and_body`, `test_ctrl_c_everywhere` |
| Not a terminal | the subjects as plain text | full screen, exit status | `test_not_a_tty` |

## Compose

| Element | States | Checked | Test |
|---|---|---|---|
| Header bar | `New message` | text, style | `test_compose_and_send` |
| Field labels | To focused (accent), Subject focused (accent), none focused while the body has the cursor | style | `test_compose_and_send` |
| To field | empty, typed, completed, two addresses | text, cursor | `test_compose_and_send` |
| Completion drop-down | opened with nothing chosen, first and second chosen (selection), one candidate; Tab writes the shared prefix (nothing, or the one candidate), Enter takes the chosen one; no drop-down without a match | text, style | `test_compose_and_send`, `test_compose_refusals_and_discard` |
| Subject field | typed; Tab from it goes to the body | text, cursor | `test_compose_and_send` |
| Refusal | ctrl-s with no recipient: message under the fields, focus on To | text, style (bold red) | `test_compose_refusals_and_discard` |
| Body editor | typed lines and a tab, scrolled down to the cursor, back to the top, sideways along a long line | text, cursor | `test_compose_and_send`, `test_compose_body_editor` |
| Body hints | line and column, `limit reached` | text, style (hint) | `test_compose_and_send`, `test_compose_body_editor` |
| shift-Tab / Enter | from the body to the field it left; Enter in a field to the body | cursor | `test_compose_body_editor` |
| Labels checklist | at rest (first row selected), a row ticked, cancelled (back to the form), confirmed | text, style (accent frame, selection) | `test_compose_and_send`, `test_compose_refusals_and_discard` |
| Discard | nothing typed: at once; typed: confirmation (No focused), No keeps the text, Yes discards; from the fields and from the body | text, style, status | `test_compose_refusals_and_discard` |
| Sent message | added to the list and selected, the list's header counts it, the body shows To and Labels | full screen, style, status ok | `test_compose_and_send` |

## Not checked, and why

- **Colours on a terminal without colour.** libptytest emulates a colour
  terminal; that every state also differs by an attribute or a glyph is
  checked through the maps (bold selection, bold frames, `[x]`, `▶`), not on
  a monochrome emulator.
