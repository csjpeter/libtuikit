# libtuikit

**Class: Library** — ships `libtuikit0` (shared library) and `libtuikit-dev`
(headers, static library, pkg-config and CMake files, man pages).
See `policies/packaging-and-release-model.md` in the workspace.

A C11 library for **full-screen terminal programs**, so that the workspace's
projects stop rewriting the same terminal code. It depends on the C library
only — no ncurses, no terminfo — and targets xterm-compatible terminals.

It is a toolkit, not a framework: the program keeps its own loop, model and
view, and the library gives it

- **a terminal session** (`tui_term`): raw mode with every change read back,
  flow control off, the alternate screen, and the terminal given back on
  every way out — close, `exit()`, SIGTERM/SIGHUP/SIGINT/SIGQUIT, Ctrl-Z and
  SIGCONT; resize events that cannot be lost; hang-up detection; the
  program's own descriptors waited on in the same call;
- **decoded keys** (`tui_decoder`): CSI with modifiers, SS3, Alt, UTF-8,
  bracketed paste — a sequence that is not a key never leaks in as text;
- **text** (`tui_text`): UTF-8 and display widths that do not depend on the
  program's locale; control characters made harmless before drawing;
- **a diffed cell screen** (`tui_screen`) rendering into a byte buffer, so it
  is testable without a terminal;
- **layout and clipped drawing** (`tui_canvas`, `tui_rect`), a **list view**,
  a **text view**, a **line editor** with history, **dialogs** — message,
  yes/no, prompt, menu — and **forms** (`tui_fieldset`): text and choice
  fields with a completion drop-down and validation;
- **no-session helpers** in email-cli's style (`tui_tty`, `tui_readline`):
  size with fallbacks, raw mode for one key, waiting with a timeout, a
  password prompt, an inline line editor with an interrupt flag and a
  timeout.

email-cli's terminal code is the reference: the library has an equivalent
for each of its capabilities (`man 7 libtuikit`, "EMAIL-CLI EQUIVALENTS",
and "TG-CLI EQUIVALENTS" for tg-cli). The tests of both projects' terminal
code run here as regression tests.

## Using it

```c
#include <tuikit/tuikit.h>

tui_term *t = tui_term_open(NULL);             /* NULL + ENOTTY: plain output */
int rows, cols;
tui_term_size(t, &rows, &cols);
tui_screen *scr = tui_screen_new(rows, cols);
tui_buf out;
tui_buf_init(&out);
for (int quit = 0; !quit; ) {
    tui_canvas all = tui_canvas_of(scr);
    tui_canvas_clear(all, TUI_STYLE_DEFAULT);
    tui_canvas_put_aligned(all, 0, "Hello", TUI_ALIGN_CENTER, TUI_STYLE_DEFAULT);
    tui_screen_render(scr, &out);
    tui_term_flush_buf(t, &out);

    tui_event ev;
    if (tui_term_wait(t, -1, NULL, 0, &ev) < 0) break;
    if (ev.type == TUI_EV_RESIZE) tui_screen_resize(scr, ev.rows, ev.cols);
    if (ev.type == TUI_EV_HANGUP || TUI_EV_IS_CHAR(&ev, 'q')) quit = 1;
}
tui_buf_free(&out);
tui_screen_free(scr);
tui_term_close(t);
```

Linking:

```sh
cc -o my-program my-program.c $(pkg-config --cflags --libs libtuikit)
```

```cmake
find_package(tuikit 0.1 REQUIRED)
target_link_libraries(my-program PRIVATE tuikit::tuikit)   # or tuikit::tuikit_static
```

or vendored, with `add_subdirectory(path/to/libtuikit)` (builds only the
library). The complete example is `examples/tuikit_demo.c`, a two-pane
message reader that follows the workspace's interface policy (header bar,
status line, palette, shared keys, help on `?`), with a compose view (a
form with address completion, a multi-line editor and a checklist of
labels), with its PTY test
`examples/test_tuikit_demo.c`; `tests/pty/test_demo.c` drives every view.

Documentation: `man 7 libtuikit` (architecture, the loop, signals, the
email-cli equivalents), `man 3 libtuikit` (function overview) and one page
per function group. The design and the survey of the code it replaces are in
`docs/`.

## Layout

```
include/tuikit/   public headers (tuikit.h includes them all)
src/              term.c tty.c decoder.c text.c width_table.c buf.c screen.c
                  canvas.c listview.c textview.c lineedit.c dialog.c fieldset.c
tools/            gen-width.c: regenerates src/width_table.c from glibc
man/              man3/*.3, man7/libtuikit.7
tests/unit/       every module in-process; tui_term over openpty(); failure
                  paths by fault injection; email-cli/tg-cli regression cases
tests/pty/        real programs through a real PTY (libptytest), including
                  the PTY scenarios of email-cli and tg-cli
examples/         tuikit-demo, its PTY test, a consumer build
docs/             specification.md, survey.md
cmake/            package config, pkg-config template, .deb build metadata
```

## Development

```sh
./manage.sh deps        # build and test dependencies (root)
./manage.sh test        # unit tests, ASAN + UBSan
./manage.sh pty         # PTY tests, ASAN + UBSan
./manage.sh valgrind    # all tests under Valgrind, Release build
./manage.sh coverage    # line coverage of src/, fails below 100%
./manage.sh package     # build/packages/: libtuikit0_*.deb, libtuikit-dev_*.deb
./manage.sh consumer    # build and run the example against the .deb content
./manage.sh check       # all of the above, with a summary
```

One test: `build/tests/unit/bin/test-decoder test_paste` (substring filter, `-l`
lists the tests). The tests use `libptytest` as their terminal emulator; the
library itself shares no code with it.

## License

GPL-3.0-only, see `LICENSE`.

A program that links libtuikit is bound by the GPL when it is distributed. The
copyright holder may license his own code to others on other terms.
