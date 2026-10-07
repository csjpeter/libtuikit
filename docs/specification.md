# libtuikit — specification

**Status: approved on 2026-10-03, implemented (M1–M6) on 2026-10-03.**
The headers in `include/tuikit/` and the man pages are authoritative for
signatures; where they differ from the sketches below, they win. The
differences, all additions or clarifications:

- `tui_buf_puts()`; `tui_buf_write_fd()` empties the buffer in every case.
- `tui_term_wait()` takes `struct pollfd *` (it fills in `revents`).
  Fatal signals and SIGTSTP are taken over only while they have their
  default action; a handler the program installed (mamind's interrupt
  flag) or an ignored signal is left alone; SIGWINCH and SIGCONT chain to
  a previous handler. `TUI_TERM_KEEP_IXON` keeps flow control.
- The decoder has `tui_decoder_free()`, `tui_decoder_pending()` and
  `tui_event_name()`; Shift-Tab is `TUI_KEY_TAB` with `TUI_MOD_SHIFT`;
  Ctrl+letter is `TUI_KEY_CHAR` with the letter and `TUI_MOD_CTRL`, and
  carries no `utf8` text; `CSI … u` is understood too.
- `tui_tty_read_key()` returns 0 on end of input; a bracketed paste
  arrives without text there (use `tui_term_wait()` for pastes).
- `tui_screen_render()` returns `long`; `tui_screen_rows/cols()`,
  `tui_screen_set_sync()` added; a cell keeps two combining marks.
- Canvas: `tui_rect_intersect()`, `tui_canvas_clear/hline/vline()`; a
  sub-canvas clipped at the top or left starts at its first visible cell.
- Text view: `tui_textview_append()` follows the end only while the view
  is at the end; it never scrolls past the last line.
- Line editor: `tui_lineedit_run()` takes a start column (email-cli used
  3) and also returns -1 on end of input; `tui_readline()` (§10.2) is
  implemented with `TUI_READLINE_*` results, an interrupt flag and an
  idle timeout; Esc is ignored in it, as in tg-cli.
- Synchronized output (§10.3) is on by default; libptytest ignores it
  (verified by the renderer tests).
- Valgrind does not emulate SIGTSTP stops (measured); that one test says
  so under Valgrind and runs in full natively and under ASAN.

Added after the first implementation (2026-10-03):

- `tui_fieldset` (`tuikit/fieldset.h`): text and choice fields, a
  completion drop-down per token, validation -- email-cli's compose dialog
  and issue-manager's issue form.  (Not `tui_form_*`: issue-manager uses
  that family, §3.)
- `tui_tty_wait()`: tg-cli's `terminal_wait_key()`, with EINTR reported.
- `tui_textedit` (TUI-2, `tuikit/textedit.h`): a multi-line editor that
  keeps newlines and tabs -- issue-manager's `tui_textarea`, generalised to
  display widths and tab stops, with a one-line summary for form rows.  (Not
  `tui_textarea`: issue-manager keeps that name for its own, §3.)
- `tui_dialog_checklist()` (TUI-1): a menu in which several rows are ticked
  with Space; the set is read back on OK, and `answer` is the row Enter was
  pressed on -- issue-manager's status filter.
- `tui_dialog_help()` (TUI-12): the help screen of
  policies/tui-interface-model.md -- a box over the view, two columns, the
  keys in bold; Esc, q and Backspace close it, the movement keys scroll it
  when it does not fit, other keys do nothing (TUI-18; the policy's "any
  key closes it" is changed by POL-28). Its rows are `items` read in pairs, so `tui_dialog` keeps its
  size.
- `tui_lineedit_replace()`, for completions.
- `tui_tty_read_password()` turns echo off before writing the prompt, and
  restores the terminal if SIGINT/SIGTERM/SIGHUP/SIGQUIT ends the process
  during it (survey defect 7; TG-6, EMAIL-5).
- A lone ESC held for its delay is delivered before the end of input or a
  hang-up, on every reading path.
- email-cli's 34 `input_line` unit cases and its PTY scenarios, and tg-cli's
  readline, password and key tests, run here as regression tests
  (`tests/unit/test_compat.c`, `tests/pty/test_compat_pty.c`).  One
  deliberate difference: email-cli read `ESC x` arriving together as Esc
  (dropping the `x`); libtuikit reads it as Alt+x.
- Every line of `src/` is run by a test; the coverage gate is 100 %.
  Failure paths are reached by link-time fault injection in the test
  binaries only (`tests/common/faults.c`).

libtuikit is a C11 library for full-screen terminal programs. It exists so
that the workspace's projects stop rewriting the same terminal code; what they
rewrite today, and what they learned doing it, is in [survey.md](survey.md).

**Class: Library** — `libtuikit0` (shared library) and `libtuikit-dev`
(headers, static library, pkg-config and CMake files, man pages).

**License: BSD-2-Clause** (the FreeBSD license).

## 0. The reference implementation: email-cli

Where tg-cli and email-cli solve the same problem differently, **email-cli
is the reference**: it is the more thoroughly tested in practice. And
libtuikit may only **add** to what email-cli's terminal code can do, never
take a capability away. Every email-cli capability therefore has a
libtuikit equivalent:

| email-cli (`terminal.h`, `input_line.h`) | libtuikit |
|---|---|
| `terminal_is_tty(fd)` | `tui_tty_is(fd)` (§4.3) |
| `terminal_cols()` — 80 when unknown | `tui_tty_cols(fd)` — 80 when unknown |
| `terminal_rows()` — 0 when unknown | `tui_tty_rows(fd)` — 0 when unknown |
| `terminal_raw_enter/exit`, `RAII_TERM_RAW` | `tui_tty_raw_enter/exit`, `TUI_RAII_RAW` — usable briefly, without a full-screen session |
| `terminal_read_key()` (blocking) | `tui_tty_read_key(fd, &ev)` (blocking) |
| `TERM_KEY_*` incl. `TAB`, `SHIFT_TAB` | `TUI_KEY_*`, a superset (plus tg-cli's Ctrl keys, F-keys, modifiers) |
| `terminal_last_printable()`, `terminal_last_utf8()` | `ev.cp`, `ev.utf8` of the same event |
| bare-ESC timeout 100 ms | default `esc_delay_ms` = 100 |
| `terminal_wcwidth()` — non-ASCII with width <= 0 counted as 1 | `tui_cp_width()`: a printable code point is never dropped; unknown counts as 1 |
| `terminal_read_password()` incl. non-TTY fallback | `tui_tty_read_password()`, same fallback |
| `input_line_init/run` — row, prompt, initial text, cursor at end, Enter → 1, ESC/Ctrl-C → 0 | `tui_lineedit_init` / `tui_lineedit_run` — same contract |
| `InputLine.tab_fn`, `shift_tab_fn`, `render_below` hooks | the same three hooks on `tui_lineedit` |
| immediate-mode drawing (escape sequences at a row, real terminal cursor) | `tui_lineedit_run` draws directly at a terminal row; the retained `tui_screen` is optional, never required |
| builds on Windows (MinGW) with a stub terminal layer | builds on Windows with the same stub semantics (§1) |

A unit test per row of this table keeps the promise checkable.

Resolved tg-cli / email-cli differences: key set — the union; ESC timeout —
email-cli's 100 ms; width of an unknown code point — email-cli's 1 (tg-cli
drops it); text sanitising — tg-cli's, as an addition.

## 1. Goals and non-goals

Goals:

- One correct implementation of: raw mode and its restoration, terminal size
  and resize notification, key decoding, character width, a diffed cell
  screen, clipped drawing, list view, text view, line editor, modal dialogs.
- Every piece testable without a terminal; the whole testable through one.
- Behaviour a caller can rely on in the failure cases the survey lists:
  lost resizes, flow control, hang-up, signals, foreign output, untrusted
  text.

Non-goals:

- **Not a framework.** The library has no main loop it forces on the program
  and no application object. The program owns its loop; the library gives it
  events, state machines and drawing. (tg-cli and issue-manager both keep
  their model separate from their view, and should be able to keep doing so.)
- No ncurses, no terminfo, no dependency beyond libc. The target terminal is
  xterm-compatible (`xterm-256color`); sequences outside the common
  xterm/VT220 subset are not emitted.
- No Windows console implementation. The library does **build** on Windows
  (MinGW) with email-cli's stub semantics — not a TTY, 80 columns, 0 rows,
  raw mode unavailable, width 1 — so that email-cli's Windows build keeps
  compiling after it migrates.
- No mouse in the first version (see *Open questions*).

## 2. Design principles

1. **Independent of the test instrument.** libtuikit shares no code and no
   logic with libptytest: not the width table, not the colour encoding, not
   the cell model. If the renderer and the emulator shared a wrong rule, a
   test would pass on the bug. libptytest is a build dependency of the tests
   only. Where both must implement the same external rule (code-point widths,
   SGR), libtuikit derives it from an independent source — glibc's C.UTF-8
   locale for widths, xterm's *ctlseqs* for sequences — and its unit tests
   additionally assert the exact bytes, not only the emulated screen.
2. **Pure cores, thin I/O.** Decoding, editing, list navigation, layout and
   rendering are functions over plain data. Only `tui_term`, `tui_tty` and
   `tui_lineedit_run` touch file descriptors; only `tui_term` installs
   signal handlers.
3. **Output to an abstract sink.** The screen renders into a byte buffer
   (`tui_buf`); the program, or `tui_term`, writes that buffer to an fd. A
   unit test feeds the same buffer to `pty_feed()` and compares the result
   with `ASSERT_REGION_EQ`.
4. **The library never prints.** No `stderr`, no logging. Errors are return
   values with `errno` set. (issue-manager BUG-213.)
5. **Every exit path restores the terminal**: normal close, `atexit`, fatal
   signals, and suspend (Ctrl-Z) / resume.
6. **Untrusted text is safe to draw.** Every drawing call replaces C0/C1
   controls and DEL before they reach a cell.

## 3. Naming

- Functions and types: `tui_<module>_<verb>`, types `tui_<module>` (no `_t`
  suffix: POSIX reserves it). Constants: `TUI_<MODULE>_<NAME>`.
- Include guards: `LIBTUIKIT_<FILE>_H`. Headers live in
  `include/tuikit/*.h`, with `tuikit/tuikit.h` including all of them.
- **Names the library never defines**, because tg-cli or issue-manager
  already do (survey, *Name collisions*): `tui_app_*`, `tui_model_*`,
  `tui_input_*`, `tui_form_*`, `tui_api_*`, `tui_textarea*`,
  `tui_text_summary`, `tui_run`, `tui_view_t`,
  `tui_modal_t`, and the macro families `TUI_EVENT_*`, `TUI_FOCUS_*`,
  `TUI_VIEW_*`, `TUI_MODAL_*`, `TUI_FORM_*`, `TUI_SUB_*`, `TUI_MIN_*`,
  `TUI_MAX_*`, `TUI_KEY_MAX`, `TUI_INPUT_MAX`. A unit test greps the installed
  headers for them.
- The shared library exports only `TUI_API`-marked symbols (hidden visibility
  by default), as libptytest does.

## 4. Modules

```
  program
    │  events                         drawing
    ▼                                  │
  tui_term ──► tui_decoder ──► tui_event     tui_canvas ──► tui_screen ──► tui_buf ──► fd
   (fds, termios,  (bytes → keys)          (clipped rect)  (cells, diff)  (bytes)
    signals, size)
                         widgets: tui_listview  tui_textview  tui_lineedit  tui_dialog
                         helpers: tui_text (UTF-8, width, sanitise)  tui_rect (layout)
```

| Header | Module | Pure? | Milestone |
|---|---|---|---|
| `tuikit/term.h` | full-screen session: mode, size, resize, signals, waiting | no All milestones below are done; `./manage.sh check` passes every gate.

| **M1** |
| `tuikit/tty.h` | fd-level helpers in email-cli's style: size, raw mode, key, password | no | **M1** (keys: M2) |
| `tuikit/text.h` | UTF-8, display width, sanitising, fitting | yes | M2 |
| `tuikit/event.h` | `tui_event`, `tui_decoder` | yes | M2 |
| `tuikit/buf.h` | growable byte buffer | yes | M1 |
| `tuikit/screen.h` | cells, styles, diff renderer | yes | M3 |
| `tuikit/canvas.h` | `tui_rect`, layout splits, clipped drawing, boxes | yes | M4 |
| `tuikit/listview.h` | scroll + selection, row rendering | yes | M4 |
| `tuikit/textview.h` | wrapped, scrollable text | yes | M4 |
| `tuikit/lineedit.h` | single-line editor model, history | yes | M5 |
| `tuikit/dialog.h` | message, confirm, prompt, menu | yes | M6 |

### 4.1 `tui_buf` — byte sink

```c
typedef struct { char *data; size_t len, cap; int failed; } tui_buf;
void tui_buf_init(tui_buf *b);
void tui_buf_free(tui_buf *b);
void tui_buf_reset(tui_buf *b);                       /* len = 0, keeps memory */
int  tui_buf_append(tui_buf *b, const void *p, size_t n);
int  tui_buf_printf(tui_buf *b, const char *fmt, ...);
int  tui_buf_write_fd(tui_buf *b, int fd);            /* all of it, EINTR/EAGAIN-safe */
```

An allocation failure sets `failed`, makes later appends no-ops, and is
reported by `tui_buf_write_fd` (-1, `ENOMEM`) so a half-built frame is never
written.

### 4.2 `tui_term` — the terminal (M1)

```c
typedef struct tui_term tui_term;                     /* opaque */

typedef struct {
    int in_fd, out_fd;        /* default: STDIN_FILENO, STDOUT_FILENO       */
    unsigned flags;           /* TUI_TERM_ALTSCREEN | TUI_TERM_HIDE_CURSOR
                                 | TUI_TERM_KEEP_ISIG | TUI_TERM_PASTE
                                 | TUI_TERM_NO_SIGNALS                      */
    int esc_delay_ms;         /* bare-ESC timeout; 0 = default (100 ms, as email-cli) */
} tui_term_opts;

void tui_term_opts_init(tui_term_opts *o);            /* the defaults above */
tui_term *tui_term_open(const tui_term_opts *o);      /* NULL + errno       */
int   tui_term_close(tui_term *t);                    /* restores, frees    */

int   tui_term_size(tui_term *t, int *rows, int *cols);
int   tui_term_write(tui_term *t, const void *p, size_t n);
int   tui_term_flush_buf(tui_term *t, tui_buf *b);    /* write + reset      */

int   tui_term_suspend(tui_term *t);                  /* restore mode, keep state */
int   tui_term_resume(tui_term *t);                   /* re-enter, report resize  */

int   tui_term_wait(tui_term *t, int timeout_ms,
                    const struct pollfd *extra, int n_extra,
                    tui_event *ev);
int   tui_term_fd(const tui_term *t);                 /* for the caller's own poll */
```

**Opening** checks that `in_fd` is a terminal (`ENOTTY` otherwise — the
program decides its non-TTY fallback, e.g. email-cli's help text), saves the
termios state, and enters raw mode:

- `c_lflag`: clear `ICANON ECHO IEXTEN`; clear `ISIG` unless
  `TUI_TERM_KEEP_ISIG` (mamind keeps Ctrl-C as SIGINT).
- `c_iflag`: clear `IXON IXOFF ICRNL INLCR IGNCR ISTRIP BRKINT INPCK`
  (Ctrl-S/Ctrl-Q reach the program: issue-manager BUG-208);
  `TUI_TERM_KEEP_IXON` keeps flow control for a program that wants it.
- `c_oflag`: leave `OPOST` alone (the renderer never relies on `\n`).
- `VMIN = 1, VTIME = 0`; every `tcsetattr` is checked and the mode read back
  to confirm it applied. Failure undoes everything done so far.

Then, as flagged: alternate screen (`CSI ? 1049 h`), hidden cursor
(`CSI ? 25 l`), bracketed paste (`CSI ? 2004 h`).

**Closing** writes the inverse sequences, restores the saved termios with
`TCSAFLUSH`, removes the signal handlers it installed (restoring the
previous ones), and frees. It is idempotent with the `atexit` and signal
paths: whichever runs first restores, the others see nothing to do.

**Size** is `TIOCGWINSZ` on `out_fd`, then `in_fd`. If both fail or report
0, `tui_term_size` fails with `ENOTTY`, because a full-screen session cannot
draw into an unknown size. The email-cli fallback (80 columns, 0 rows) is
kept unchanged in `tui_tty_cols` / `tui_tty_rows` (§4.3), for output that
is not full-screen.

**Resize** uses the self-pipe pattern: the SIGWINCH handler writes one byte
to a non-blocking pipe, and `tui_term_wait` polls the pipe together with
`in_fd`. A resize arriving at any moment — during drawing, between the check
and the wait — leaves a byte in the pipe and wakes the next wait. This
closes the race of issue-manager BUG-240 without requiring the program to
manage the signal mask. Several resizes before a wait collapse into one
`TUI_EV_RESIZE` carrying the current size.

**Signals.** Unless `TUI_TERM_NO_SIGNALS`:

- `SIGTERM`, `SIGHUP`, `SIGINT`, `SIGQUIT`: restore the terminal with
  async-signal-safe calls only (`tcsetattr`, `write` of a precomputed
  restore string), reinstall the previous disposition, re-raise — so the
  exit status says what killed the program.
- `SIGTSTP` (Ctrl-Z, which only arrives if `ISIG` is kept or the program
  raises it): restore, stop; on `SIGCONT`: re-enter raw mode and queue a
  `TUI_EV_RESIZE` so the program repaints everything.
- `SIGWINCH`: as above.
- Only one `tui_term` with signal handling may be open per process (signal
  dispositions are process-wide); a second fails with `EBUSY`.

**Waiting.** `tui_term_wait` returns 1 with an event, 0 on timeout
(negative timeout: wait forever), -1 on error. Event types it produces:
`TUI_EV_KEY`, `TUI_EV_PASTE`, `TUI_EV_RESIZE`, `TUI_EV_FD` (one of
`extra` is ready — tg-cli's network socket, issue-manager's agent stream),
`TUI_EV_HANGUP` (read returned 0, or `POLLHUP` without `POLLIN`: the
terminal is gone, lesson 4). `EINTR` is retried internally. Input is read
with `read(2)`, never stdio. A pending bare ESC is resolved after
`esc_delay_ms` by the decoder's timeout, not by changing `VTIME`.

**Foreign output.** If anything writes to the terminal outside the library
(a child process, a log line), the program calls
`tui_screen_invalidate()` and the next frame repaints every cell.

### 4.3 `tui_tty` — fd-level helpers, no session needed (M1, keys M2)

email-cli enters raw mode for a single keypress, sizes paged CLI output,
and asks for passwords — none of it full-screen. These functions keep that
style available beside `tui_term`:

```c
int  tui_tty_is(int fd);
int  tui_tty_cols(int fd);                 /* 80 when unknown (email-cli) */
int  tui_tty_rows(int fd);                 /* 0 when unknown (email-cli)  */

typedef struct tui_tty_raw tui_tty_raw;    /* opaque saved state */
tui_tty_raw *tui_tty_raw_enter(int fd, unsigned flags);   /* NULL + errno */
void tui_tty_raw_exit(tui_tty_raw **state);               /* NULL-safe, sets NULL */
#if defined(__GNUC__)
#define TUI_RAII_RAW __attribute__((cleanup(tui_tty_raw_exit)))
#endif

int  tui_tty_read_key(int fd, tui_event *ev);     /* blocking, one key; 1 / -1 */
int  tui_tty_read_password(int in_fd, int out_fd, const char *prompt,
                           char *buf, size_t size);   /* echo off; non-TTY: plain line */
```

`tui_tty_raw_enter` applies the same termios changes as `tui_term_open`
(and checks them), without alternate screen, cursor or signal handling.
`tui_tty_read_key` uses the same decoder as `tui_term_wait`.

### 4.3a `tui_text` — UTF-8 and width (M2)

```c
int    tui_utf8_decode(const char *s, size_t n, uint32_t *cp); /* bytes used; U+FFFD on error */
int    tui_utf8_encode(uint32_t cp, char out[4]);
int    tui_cp_width(uint32_t cp);              /* 0, 1 or 2; -1 for controls */
int    tui_str_width(const char *s);           /* columns of a UTF-8 string  */
size_t tui_str_fit(const char *s, int cols, int *used);   /* bytes that fit  */
int    tui_str_fit_ellipsis(const char *s, int cols, char *out, size_t outsz);
uint32_t tui_cp_sanitize(uint32_t cp);         /* controls/DEL/C1 → U+00B7   */
```

Widths come from a table generated at development time from glibc's
C.UTF-8 `wcwidth()` by `tools/gen-width.c`, and committed as
`src/width_table.c`. The program's locale therefore has no effect (lesson
5: in the C locale glibc reports -1 for everything non-ASCII). A unit test
re-derives the table from the running glibc and reports any difference, so
a Unicode update is noticed rather than silently carried. Combining marks
are width 0 and attach to the previous cell. A printable code point the
table does not know counts as width 1 and is drawn, never dropped
(email-cli's rule; tg-cli drops it).

### 4.4 `tui_event` and `tui_decoder` (M2)

```c
typedef enum { TUI_EV_NONE, TUI_EV_KEY, TUI_EV_PASTE, TUI_EV_RESIZE,
               TUI_EV_FD, TUI_EV_HANGUP } tui_event_type;

typedef enum {               /* values above 0x10FFFF: never a code point */
    TUI_KEY_CHAR = 0,        /* printable: see .cp and .utf8 */
    TUI_KEY_ENTER = 0x110000, TUI_KEY_TAB, TUI_KEY_BACKSPACE, TUI_KEY_ESC,
    TUI_KEY_UP, TUI_KEY_DOWN, TUI_KEY_LEFT, TUI_KEY_RIGHT,
    TUI_KEY_HOME, TUI_KEY_END, TUI_KEY_PGUP, TUI_KEY_PGDN,
    TUI_KEY_INSERT, TUI_KEY_DELETE, TUI_KEY_F1 /* … TUI_KEY_F12 */,
} tui_key;

enum { TUI_MOD_SHIFT = 1, TUI_MOD_ALT = 2, TUI_MOD_CTRL = 4 };

typedef struct {
    tui_event_type type;
    tui_key  key;
    unsigned mods;
    uint32_t cp;            /* TUI_KEY_CHAR, and Ctrl+letter as 'a'..'z' + TUI_MOD_CTRL */
    char     utf8[5];
    int      rows, cols;    /* TUI_EV_RESIZE */
    int      fd_index;      /* TUI_EV_FD */
    const char *paste; size_t paste_len;   /* TUI_EV_PASTE, valid until next wait */
} tui_event;

typedef struct tui_decoder tui_decoder;      /* fixed-size, caller-allocated */
void tui_decoder_init(tui_decoder *d);
void tui_decoder_feed(tui_decoder *d, const char *bytes, size_t n);
int  tui_decoder_next(tui_decoder *d, tui_event *ev);       /* 1 = event */
int  tui_decoder_timeout(tui_decoder *d, tui_event *ev);    /* resolve pending ESC */
int  tui_decoder_pending_ms(const tui_decoder *d, int esc_delay_ms);
```

A byte state machine that understands, by construction rather than by a
list of special cases: CSI with any number of parameters and the xterm
modifier parameter (`CSI 1 ; 5 A` = Ctrl+Up — survey defect 2), `~` forms,
SS3 (`ESC O A`, application cursor mode — defect 4), `ESC x` as Alt+x,
Shift-Tab (`CSI Z`), bracketed paste (`CSI 200 ~ … CSI 201 ~`), UTF-8 with
U+FFFD for malformed input, Ctrl+letter, `\r` and `\n` as Enter, 0x7F and
0x08 as Backspace. A sequence split across two reads is assembled. An
unknown complete sequence is consumed whole and dropped; it never leaks as
typed characters. Mapping keys to actions (`q`, `/`) stays in the
program; the movement keys shared by every TUI of the workspace (`j`/`k`,
`ctrl-f`/`ctrl-b`, `g`/`G`, policies/tui-interface-model.md) are applied by
`tui_listview_handle` and `tui_textview_handle` beside the arrows and the
page keys.

### 4.5 `tui_screen` — cells and rendering (M3)

```c
typedef enum { TUI_ATTR_BOLD = 1, TUI_ATTR_DIM = 2, TUI_ATTR_ITALIC = 4,
               TUI_ATTR_UNDERLINE = 8, TUI_ATTR_REVERSE = 16 } tui_attr;

typedef uint32_t tui_color;    /* TUI_COLOR_DEFAULT, TUI_COLOR_16(n),
                                  TUI_COLOR_256(n), TUI_COLOR_RGB(r,g,b) */
typedef struct { tui_color fg, bg; uint16_t attrs; } tui_style;

/* The palette of policies/tui-interface-model.md, the widgets' defaults:
 * TUI_STYLE_HEADER (bold white on blue), TUI_STYLE_SELECTION (bold black
 * on cyan), TUI_STYLE_ACCENT (bold cyan), TUI_STYLE_OK (bold green),
 * TUI_STYLE_ERROR (bold red), TUI_STYLE_HINT (yellow),
 * TUI_STYLE_PROGRESS (italic yellow). */

typedef struct tui_screen tui_screen;            /* opaque: owns two grids */
tui_screen *tui_screen_new(int rows, int cols);
void tui_screen_free(tui_screen *s);
int  tui_screen_resize(tui_screen *s, int rows, int cols);  /* forces full repaint */
void tui_screen_clear(tui_screen *s, tui_style st);
int  tui_screen_put(tui_screen *s, int row, int col, int max_cols,
                    const char *utf8, tui_style st);        /* columns used */
void tui_screen_fill(tui_screen *s, int row, int col, int n, uint32_t cp, tui_style st);
void tui_screen_set_cursor(tui_screen *s, int row, int col, int visible);
void tui_screen_invalidate(tui_screen *s);
int  tui_screen_render(tui_screen *s, tui_buf *out);       /* diff → bytes */
```

Double-buffered as in tg-cli: drawing changes the back grid, `render` emits
only what differs from the front grid and then copies. Rendering rules:
absolute cursor positioning (`CUP`) before each changed run; SGR emitted as
a full reset plus the needed attributes whenever the style changes; colours
as `30–37/90–97`, `38;5;n`, `38;2;r;g;b` (and the `4x/10x/48` forms); a
wide character occupies two cells, and overwriting either half blanks the
other; nothing is written to the last cell of the last row in a way that
scrolls (auto-wrap is disabled with `CSI ? 7 l` while the screen is active,
and restored on close). The frame ends with SGR reset and the requested
cursor position/visibility. Every frame is wrapped in synchronized-update
mode (`CSI ? 2026 h/l`), which terminals that lack it ignore — see *Open
questions*.

### 4.6 `tui_canvas` — rectangles, layout, clipping (M4)

```c
typedef struct { int row, col, rows, cols; } tui_rect;
typedef struct { tui_screen *screen; tui_rect clip; } tui_canvas;

tui_canvas tui_canvas_of(tui_screen *s);                      /* whole screen */
tui_canvas tui_canvas_sub(tui_canvas c, tui_rect r);          /* relative, clipped */
int  tui_canvas_put(tui_canvas c, int row, int col, const char *utf8, tui_style st);
void tui_canvas_fill(tui_canvas c, tui_rect r, tui_style st);
void tui_canvas_box(tui_canvas c, tui_rect r, const char *title, tui_style st);
int  tui_canvas_put_aligned(tui_canvas c, int row, const char *utf8,
                            int align /* left, center, right */, tui_style st);

/* Layout: cut a rectangle into two. */
void tui_rect_split_h(tui_rect r, int left_cols, tui_rect *left, tui_rect *right);
void tui_rect_split_v(tui_rect r, int top_rows, tui_rect *top, tui_rect *bottom);
tui_rect tui_rect_inset(tui_rect r, int n);
tui_rect tui_rect_center(tui_rect r, int rows, int cols);    /* for dialogs */
```

Coordinates are relative to the canvas; everything is clipped to it, so a
widget cannot draw outside its pane (tg-cli's `pane_put_str`). Boxes use
Unicode line-drawing characters (U+2500 block), not the DEC special graphics
set. A program's fixed layout (tg-cli's three panes) is a few split calls,
not a library type.

### 4.7 Widgets (M4–M6)

All widgets are plain structs plus functions: `handle(event) → result`,
`render(canvas)`. None blocks, none reads the terminal; a modal is simply a
widget the program routes events to first.

- **`tui_listview`** — tg-cli's `ListView` (count, selected, scroll_top,
  rows_visible; up/down/page/home/end, reveal, set_count, set_viewport),
  plus `tui_listview_handle(lv, ev)` for the standard keys and
  `tui_listview_render(lv, canvas, draw_row_cb, ctx)` which calls back per
  visible row with the row's canvas and whether it is selected.
- **`tui_textview`** — word-wrapped, scrollable text (message history, mail
  body, issue description): set text, wrap to width on render, scroll by
  line/page, "at end" query for follow-the-tail.
- **`tui_lineedit`** — single-line editor over a caller buffer: UTF-8-aware
  cursor movement and deletion, Home/End, Ctrl-A/E/K/U/W, Left/Right by
  code point and Ctrl+Left/Right by word, horizontal scrolling when the text
  is wider than the field, optional history (caller-owned ring), optional
  `tab_fn` / `shift_tab_fn` completion hooks and a `render_below` hook
  (email-cli's completion list). `handle` returns `TUI_EDIT_ACCEPT`,
  `TUI_EDIT_CANCEL`, `TUI_EDIT_CHANGED` or `TUI_EDIT_IGNORED`; `render`
  draws on a canvas and returns the cursor cell for `tui_screen_set_cursor`.
  `tui_lineedit_run(le, in_fd, out_fd, row, prompt)` is email-cli's
  `input_line_run`: a blocking loop that draws directly at a terminal row
  with the real cursor, calls the hooks, and returns 1 on Enter, 0 on
  ESC/Ctrl-C.
- **`tui_dialog`** — centred modal box: message (OK), confirm (yes/no),
  prompt (one `tui_lineedit`), menu (one `tui_listview`). Same
  `handle`/`render` shape, result read from the struct.

### 4.7a Drop-down completion — one rule for every TUI

Every TUI of the workspace completes a field the same way, through
`tui_fieldset`; `policies/tui-interface-model.md` refers here for the
details (POL-29). The rule is the owner's decision of 2026-10-07 (TUI-17);
it replaced email-cli's, under which the first candidate was chosen at once
and Tab or Enter accepted it.

- **What is completed.** The *token* at the cursor: the text from the last
  of the field's `separators` (e.g. `";,"` for addresses) up to the cursor,
  leading spaces skipped. The program's `complete` callback returns the
  candidates for it (`text` to insert, `label` to show); what matches is the
  program's choice (email-cli and the demo match case-insensitively
  anywhere in an address or a name). At most `TUI_FIELDSET_CAND_MAX` (16)
  are kept.
- **When the drop-down is open.** After every edit that leaves the token
  with candidates; it lists them under the field, one row each, marked `▶`.
  Typing asks again; an empty token has none.
- **Nothing is chosen when it opens** (`cand_sel` is -1): the cursor stays
  in the field, and no row is in the selection style.
- **Down** moves onto the first candidate and then down the list (it stops
  at the last); **Up** moves up, and from the first candidate back into the
  field.
- **Enter** replaces the token with the chosen candidate's `text` and closes
  the drop-down. With none chosen, Enter is the form's own Enter (submit,
  through the validation).
- **Tab** replaces the token with the longest prefix that every candidate's
  `text` shares, when that is longer than the token, and stays in the field
  with the drop-down open on the new token; the prefix is cut back to a
  whole UTF-8 character. When one candidate is left it is written out whole
  and the drop-down closes. When nothing longer is shared, Tab changes
  nothing. (With no drop-down, Tab is the next field.)
- **Shift-Tab** plays no part in completion: it is the previous field, as
  everywhere in a form.
- **Esc** (and Ctrl-C) closes the drop-down and keeps the text; only an Esc
  with no drop-down open cancels the form.
- **Looks.** The chosen row in `TUI_STYLE_SELECTION`, the others plain; the
  focused field's label in `TUI_STYLE_ACCENT`.

`tui_lineedit` keeps email-cli's own completion hooks (`tab_fn`,
`shift_tab_fn`, `render_below`) for programs that draw in place with
`tui_lineedit_run()`; a full-screen TUI uses the drop-down above.

### 4.8 Typical loop

```c
tui_term_opts o; tui_term_opts_init(&o);
tui_term *t = tui_term_open(&o);              /* NULL: not a TTY → fallback */
int rows, cols; tui_term_size(t, &rows, &cols);
tui_screen *scr = tui_screen_new(rows, cols);
tui_buf out; tui_buf_init(&out);

for (int quit = 0; !quit; ) {
    draw(&model, tui_canvas_of(scr));          /* program's own view */
    tui_screen_render(scr, &out);
    tui_term_flush_buf(t, &out);

    tui_event ev;
    if (tui_term_wait(t, -1, NULL, 0, &ev) < 0) break;
    switch (ev.type) {
    case TUI_EV_RESIZE: tui_screen_resize(scr, ev.rows, ev.cols); break;
    case TUI_EV_HANGUP: quit = 1; break;
    case TUI_EV_KEY:    quit = handle_key(&model, &ev); break;
    default: break;
    }
}
tui_buf_free(&out); tui_screen_free(scr); tui_term_close(t);
```

## 5. Errors, memory, threads

- Functions return 0 / a count on success and -1 with `errno` on failure;
  constructors return NULL with `errno`. Drawing calls that only clip never
  fail.
- Opaque objects own their memory; everything else is caller-allocated.
  No global state except the per-process signal bookkeeping of `tui_term`.
- Not thread-safe: one thread drives the terminal. Other threads can wake
  the loop through an fd passed in `extra`.

## 6. Repository layout

```
include/tuikit/   public headers
src/              implementation (term.c, buf.c, text.c, width_table.c, decoder.c,
                  screen.c, canvas.c, listview.c, textview.c, lineedit.c, dialog.c)
tools/            gen-width.c (development-time table generator)
man/man3/         one page per function group; man/man7/libtuikit.7
tests/unit/       pure modules; tui_term over an openpty() pair, in-process
tests/pty/        real programs through libptytest
examples/         tuikit-demo.c, its PTY test, a consumer build
cmake/            package config, pkg-config template, deb post-build
docs/             this specification and the survey
manage.sh, CMakeLists.txt, README.md, CLAUDE.md, LICENSE
```

## 7. Testing

Per `policies/testing-model.md`:

- **unit** (`./manage.sh test`): every pure module in-process. Rendering is
  checked twice, independently: the exact bytes against sequences written
  out from xterm's documentation, and the bytes fed through `pty_feed()`
  and compared cell by cell with `ASSERT_REGION_EQ` / `ASSERT_REGION_STYLE`.
  `tui_term` is tested in-process over an `openpty()` pair: termios flags
  read back from the slave after open and after close, `TIOCSWINSZ` +
  `SIGWINCH` producing exactly one `TUI_EV_RESIZE`, a resize raised during
  "drawing" not being lost, hang-up detection when the master closes.
- **pty** (`./manage.sh pty`): the example and small fixture programs run
  through libptytest: a full-screen cell-exact comparison of each view,
  `pty_resize` repaint, Ctrl-S reaching the program, terminal modes restored
  after normal exit, after `SIGTERM` and after `SIGHUP` (the fixture reports
  its termios state after the library has let go).
- **functional** (`./manage.sh functional`): `manage.sh` itself, on a copy
  of the tree, with mocks of our own (packaging: see section 8). The
  library has no binaries of its own to drive; the example's PTY test is
  its consumer check.
- Own runner (as libptytest's `test_helpers.h`): one process per suite,
  alarm, name filter, assertion floor. ASAN+UBSan, Valgrind, coverage on
  `src/` with a failing threshold of 100 % (above the 80 % floor).
- Every test is one `project_test(<category> <name> SOURCES|SCRIPT ...)`
  line in `tests/CMakeLists.txt` and is built into
  `build/tests/<category>/bin/<name>`, a script as a small generated
  wrapper, so that each file there runs by its path alone. The test
  `unit/check-tests` fails on a declared test missing from `bin/` and on an
  undeclared file in it. Programs the tests drive (`term-fixture`,
  `compat-harness`, `tuikit-demo`) are not tests and are not in `bin/`.

## 8. Packaging and documentation

As libptytest, per `policies/packaging-and-release-model.md`: CPack from
`CMakeLists.txt`, version from `project()` refined by commits since the last
tag, `XB-Git-Commit` / `XB-Build-Date`, `libtuikit0` + `libtuikit-dev`,
pkg-config `libtuikit`, CMake `find_package(tuikit)` → `tuikit::tuikit`,
gzipped man pages, copyright and changelog, ldconfig trigger, lintian
clean, `umask 022`. `./manage.sh` answers to `deps build debug test pty
functional coverage valgrind check package clean`, plus `consumer`.

`./manage.sh package` empties `build/packages/`, builds into it and checks
the `.deb` files with lintian: after a run the directory holds exactly that
run's packages, after a failed run nothing. `tests/functional/package_output.sh`
checks both on a copy of the tree, with a lintian mock for the failure.

The runtime package's `shlibs` reads `libtuikit 0 libtuikit0 (>= x.y.z)`:
within one SOVERSION the ABI only grows, so a dependant needs *at least* the
version it was built against. CPack's default, `=`, pins every dependant to
one version and makes apt remove them to install an upgrade (TUI-6); the
lintian gate of `./manage.sh check` refuses it.

Every exported function has a man page in section 3 (grouped pages with
symlinked aliases); `libtuikit(7)` describes the architecture, the loop, the
signal behaviour and the sequences the library emits.

## 9. Milestones

| | Content | Done when |
|---|---|---|
All milestones below are done; `./manage.sh check` passes every gate.

| **M1** | Skeleton (CMake, CPack, manage.sh, runner, CI-equivalent `check`), `tui_buf`, `tui_tty` size/raw-mode helpers, `tui_term`: raw mode, restore paths, size, resize events, hang-up, waiting on extra fds (key bytes delivered raw until M2) | unit + pty tests, man pages `tui_term_open(3)`, `tui_term_wait(3)`, `tui_tty_is(3)`, `tui_buf_init(3)`, `libtuikit(7)`, lintian clean |
| M2 | `tui_text`, width table generator, `tui_decoder`; `tui_term_wait` returns decoded keys | decoder tests over every sequence in §4.4 |
| M3 | `tui_screen` and styles | byte-exact and cell-exact renderer tests |
| M4 | canvas, layout, listview, textview; the example becomes a two-pane list + text viewer | full-screen PTY comparison of the example |
| M5 | lineedit | |
| M6 | dialogs; migration notes for each of the four projects | |

## 10. Open questions

1. ~~License~~ — decided: BSD-2-Clause.
2. **Inline line editing.** mamind and tg-cli's `rl_readline` edit a line
   in a normal (not full-screen) terminal. Should libtuikit also offer an
   inline `tui_readline()` built on `tui_lineedit`, so that those two can
   migrate too? Proposed: yes, as part of M5.
3. **Synchronized updates** (`CSI ? 2026`): proposed on by default; the
   libptytest emulator must ignore it — to be checked against
   `libtuikit(7)`/`libptytest(7)` before M3.
4. **Mouse**: not in the first version. Proposed to revisit after the
   projects have migrated.
5. **Name collisions**: the library avoids the names in §3, so projects can
   link it before they migrate. Alternatively the projects rename their own
   `tui_*` symbols when they migrate; that is their owners' call.
