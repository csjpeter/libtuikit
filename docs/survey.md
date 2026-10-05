# Survey of the existing TUI code

What the four projects with a terminal user interface implement today, what
they repeat, and what each has already learned the hard way. This is the
input to [specification.md](specification.md).

Read on 2026-10-03, read-only. Nothing in these projects was changed.

| Project | Files | Style |
|---|---|---|
| `tg-cli` | `src/tui/` (screen, pane, list_view, history_pane, dialog_pane, status_row, app), `src/platform/posix/terminal.c`, `src/core/readline.c` | Retained mode: double-buffered cell grid, diffed on flip |
| `issue-manager` | `src/app/tui/` (tui_model, tui_view, tui_api) | ncurses; strict model / view / api split |
| `email-cli` | `libemail/src/core/input_line.*`, `libemail/src/platform/*/terminal.c`, `src/main_tui.c` | Immediate mode: `printf` of escape sequences straight to stdout |
| `mamind` | `src/core/line_editor.*` | Inline line editor, no full-screen view |

## What is repeated

| Concern | tg-cli | issue-manager | email-cli | mamind |
|---|---|---|---|---|
| Raw mode (termios) | `terminal_raw_enter/exit` | ncurses `cbreak` + manual `IXON` off | same code as tg-cli (copied) | own `raw_enable/disable`, static state |
| Terminal size | `TIOCGWINSZ`, fallback 80 / 0 | `getmaxyx` | same as tg-cli | — |
| SIGWINCH | flag + `poll` with interval | flag + `ppoll` with SIGWINCH blocked outside the wait | none | — |
| Restore on signal | SIGTERM/HUP/INT handlers | `endwin` on exit paths | none | none |
| Key decoding | `terminal_read_key`: ESC, CSI, `~` forms, VTIME trick | ncurses `keypad` | same code as tg-cli, plus Tab, Shift-Tab, UTF-8 | own, ESC `[` only |
| Character width | `wcwidth(3)`, locale-dependent | ncursesw | `wcwidth(3)` | none (bytes) |
| Line editor | `rl_readline` (history, Ctrl-A/E/K/W) | `tui_input_*` model, ASCII only | `input_line_*` (UTF-8, completion hooks) | `line_editor_read` (history, timeout, interrupt flag) |
| List view | `ListView` (scroll + selection) | `issue_sel` + `issue_scroll` in the model | hand-coded per screen | — |
| Panes / layout | `Pane`, `Layout` (fixed three-pane) | coordinates computed in `draw_*` | absolute rows in `printf` | — |
| Dialogs / modals | — | `tui_modal_t` state machine, form, confirm, message | `compose_dialog`, review prompts | — |
| Drawing | cell grid + diff | ncurses windows | `printf` + `\033[K` | `\r` + `\033[0K` |
| Text sanitising | control chars → U+00B7 | — | — | — |

Four line editors, three key decoders (two of them copies of each other), three
raw-mode implementations, two resize strategies, and one renderer that is
testable without a terminal (tg-cli's, through its `FILE *out`).

## Lessons already paid for

These are bugs found and fixed in one project that the others still carry, or
that a shared library must not reintroduce.

1. **A resize must not be lost between the flag check and the wait**
   (issue-manager BUG-211, BUG-240). A SIGWINCH handler installed with
   `SA_RESTART` restarts the blocking read; one delivered while drawing is
   latched with nothing left to interrupt. issue-manager blocks SIGWINCH
   outside `ppoll(2)`. tg-cli polls with a timeout, so a resize waits for the
   next tick.
2. **Flow control swallows Ctrl-S and Ctrl-Q** (issue-manager BUG-208). `IXON`
   must be cleared, or Ctrl-S freezes the output and looks like a hang. tg-cli,
   email-cli and the ncurses `cbreak()` path all leave it set.
3. **Nothing may write to the terminal behind the TUI's back** (issue-manager
   BUG-213). A log line at the bottom scrolls every row and the next diff draws
   into the wrong places. A library therefore never prints, and must offer a
   full repaint after foreign output.
4. **A hung-up terminal makes the loop spin** (issue-manager, `POLLHUP`). A read
   returning 0 or `POLLHUP` without `POLLIN` means the terminal is gone.
5. **Width depends on the locale** (issue-manager BUG-200). Without
   `setlocale(LC_ALL, "")`, glibc's `wcwidth` returns -1 for every non-ASCII
   code point. Measured: in the C locale `wcwidth(U+00E1) = -1`,
   `wcwidth(U+4E2D) = -1`; in C.UTF-8, 1 and 2.
6. **stdio and `VMIN=0` do not mix** (tg-cli, email-cli comment). A timed-out
   `getchar()` sets the EOF flag for good. Read with `read(2)`.
7. **A typed accented letter must be inserted as its UTF-8 bytes**, not as a
   flag value (email-cli `input_line.c` comment).
8. **Untrusted text can carry escape sequences** (tg-cli SEC-01). Control
   characters, DEL and the 8-bit CSI U+009B are replaced before they reach a
   cell.

## Defects found in the other projects

Recorded as tickets in the issue manager under the owning project's key (see
`policies/issue-tracking-model.md`); not fixed here, and nothing written into
those projects' trees. Re-checked against their sources on 2026-10-05, the
measured ones by compiling the project's own file with a small driver.

| # | Project | Where | Defect | Ticket |
|---|---|---|---|---|
| 1 | tg-cli | `src/tui/screen.c` `screen_put_str_n` + `platform/posix/terminal.c` `terminal_wcwidth` | On POSIX `terminal_wcwidth` calls libc `wcwidth` (the project's own `core_wcwidth` is used only on Windows), and tg-cli never calls `setlocale`, so every non-ASCII code point gets -1 and `screen_put_str_n` skips it. Measured: "árvíz" draws 3 columns, "中文" none. | TG-2 |
| 2 | tg-cli, email-cli | `terminal_read_key`, `case '1'` | `ESC [ 1 ; 5 A` (Ctrl+Up, and every modified arrow/Home/End) consumes `1` and `;` only; `5A` stays in the input and arrives as two typed characters. Measured in both. | TG-3, EMAIL-3 |
| 3 | tg-cli, email-cli | `terminal_raw_enter` | The `tcsetattr` result is ignored and `active` is set regardless; a failure is reported as success. `IXON` is left on (lesson 2). | TG-4, EMAIL-4 |
| 4 | email-cli | `terminal_read_key`, ESC branch | ESC followed by anything but `[` is a bare ESC, and the third byte of an SS3 sequence stays in the input: F1 (`ESC O P`) is Esc plus a typed "P". Measured. (tg-cli decodes `ESC O H/F` and consumes other SS3 sequences whole: not affected — corrected 2026-10-05.) | EMAIL-2 |
| 5 | mamind | `line_editor.c` | Not UTF-8 aware: Backspace removes one byte of a multi-byte character, and the cursor arithmetic in `refresh_line` counts bytes as columns. `ESC [ 3 ~` (Delete) leaves `~` to be inserted as text. | none — mamind has no key in the tracker; reported to the human |
| 6 | tg-cli | `terminal_wait_key` | `return (errno == EINTR) ? -1 : -1;` — both branches equal, so an interrupted wait cannot be told from an error. | TG-5 |
| 7 | tg-cli, email-cli | `terminal_read_password` | The prompt is written before echo is turned off: keys typed as soon as the prompt appears are echoed — the start of the password can show on the screen. Found when carrying tg-cli's password test over (libtuikit turns echo off first). Ctrl-C at the prompt also leaves the terminal without echo. | TG-6, EMAIL-5 |

Withdrawn on 2026-10-05: an earlier entry said email-cli redraws only when a
screen is next drawn after a resize. That is what its specification promises
(`docs/spec/output-formats.md`: the layout is recomputed on each redraw), so it
is not a defect.

## Name collisions to avoid

The library's prefix is `tui_` (`tk_` collides with Tcl/Tk). Two of the
projects already use `tui_` for their own symbols, so a project linking
libtuikit before it has migrated would collide on these:

- tg-cli: `tui_app_*`, `TUI_EVENT_*`, `TUI_FOCUS_*`, `TUI_MIN_*`, `TUI_MAX_*`,
  include guards `TUI_SCREEN_H`, `TUI_PANE_H`, `TUI_LIST_VIEW_H`, …
- issue-manager: `tui_model_*`, `tui_input_*`, `tui_form_*`, `tui_api_*`,
  `tui_run`, `tui_view_t`, `tui_modal_t`, `TUI_KEY_MAX`, `TUI_INPUT_MAX`,
  `TUI_VIEW_*`, `TUI_MODAL_*`, `TUI_FORM_*`, `TUI_SUB_*`.

libtuikit defines none of these names (see the specification, *Naming*).
