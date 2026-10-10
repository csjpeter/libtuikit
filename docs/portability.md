# What binds libtuikit to Linux

libtuikit is built, tested and packaged for **Linux (Debian) only**
(policies/platform-scope-model.md). Nothing here is a plan and nothing is
promised for another platform: this is the list of what would have to be
resolved on the day one becomes a target, and roughly how much work each item
is.

## In the library

| Dependency | Where | Would need |
|---|---|---|
| `termios`: `tcgetattr`/`tcsetattr`, raw-mode flags | `src/tty.c` (`tui_tty_raw_enter`), `src/term.c`, `src/internal.h` (`tui__make_raw`, `tui__apply_mode`) | Windows: `GetConsoleMode`/`SetConsoleMode` with the input flags cleared and virtual-terminal processing on the output. macOS, Android: none (same termios) |
| Window size by `ioctl(TIOCGWINSZ)` | `src/term.c`, `src/tty.c` | Windows: `GetConsoleScreenBufferInfo`, the `srWindow` rectangle (not the buffer size) |
| Signals: `sigaction` for `SIGWINCH`, `SIGTSTP`, `SIGCONT`, `SIGTERM`, `SIGHUP`, `SIGINT`, `SIGQUIT`; `raise(SIGTSTP)` to suspend; a self-pipe (`pipe`, `fcntl`) to wake the wait | `src/term.c`, `src/tty.c` (the password prompt) | Windows: no `SIGWINCH` or `SIGTSTP`; a resize arrives as a console input record, `SetConsoleCtrlHandler` replaces the fatal signals, and suspend has no equivalent. Roughly the whole of `tui_term` |
| `poll(2)` on descriptors, and `struct pollfd` **in the public API** (`tui_term_wait`'s extra descriptors) | `src/term.c`, `src/tty.c`, `src/lineedit.c`, `include/tuikit/term.h` | Windows: a handle-based wait, and an API change, which is a SOVERSION change |
| `read(2)` and `write(2)` of the byte stream, `isatty`, `<unistd.h>` | `src/tty.c`, `src/term.c`, `src/lineedit.c` | Windows: `_read`/`_write`/`_isatty` and `ReadConsoleInputW`, with UTF-16 surrogate pairs turned into UTF-8 before `tui_decoder` |
| Output as xterm escape sequences (CUP, SGR, ED, alternate screen, DECTCEM, no-wrap, synchronized output, bracketed paste) | `src/screen.c`, `src/term.c`, `src/decoder.c` | Pure bytes, so portable to any xterm-compatible terminal. Windows needs `ENABLE_VIRTUAL_TERMINAL_PROCESSING` and a UTF-8 output code page |
| `clock_gettime(CLOCK_MONOTONIC)` | `src/tty.c` (`tui__now_ms`) | One function: `QueryPerformanceCounter` or `timespec_get` |
| Compiler extensions behind `#if defined(__GNUC__)`: `visibility("default")` (`TUI_API`), `format(printf)`, `cleanup` (`TUI_RAII_RAW`) | `include/tuikit/api.h`, `buf.h`, `tty.h` | Empty expansions for another compiler; the shared library would also need an export mechanism there |
| ELF shared object with a SONAME and hidden visibility | `CMakeLists.txt` | Another library format and naming |

The `#ifdef`s of the code are reviewed: the `_WIN32` branches that used to be in
`src/term.c`, `src/tty.c`, `src/lineedit.c`, `src/internal.h` and `api.h` were
untested stubs for a platform nobody builds, a port in disguise, and were
removed (TUI-33). What remains is the `__GNUC__` guard of the compiler
attributes in the last-but-one row: it isolates one small thing and is listed
here.

## In the width table

| Dependency | Where | Would need |
|---|---|---|
| The table is generated from glibc's `wcwidth()` in the `C.UTF-8` locale | `tools/gen-width.c`, the `regen-width` target | None for using the table, which is plain data. Regenerating it needs glibc: a maintenance step on Linux regardless |
| `test-text` compares the committed table with the running glibc | `tests/unit/test_text.c` | A glibc with `C.UTF-8`, or an explicit skip with a message on another libc |

## In the build, the tests and the packages

None of these need resolving for another platform to *use* the library; they
say what the build and test host is.

| Dependency | Where | Would need |
|---|---|---|
| `libptytest`, built on `openpty(3)` (`-lutil` on older glibc), emulates the terminal | `tests/` | Nothing: the test host is Linux regardless |
| libptytest as a pinned submodule (`deps/libptytest`, `WORKSPACE_LIBS=bundled`, the default) or as `libptytest-dev` (`system`); both are exercised: the default gates build the first, `tests/functional/workspace_libs.sh` configures, builds and runs a test against the second (and says so when the package is not installed) | `CMakeLists.txt`, `manage.sh`, `tests/functional/workspace_libs.sh` | Nothing: both are Linux-hosted; the consumer gate builds the submodule into a prefix of its own and uses it as a package |
| Link-time fault injection with `-Wl,--wrap` (GNU ld, lld) | `tests/common/faults.c`, `tests/CMakeLists.txt` | Apple's ld64 does not support `--wrap`; another way to inject faults |
| AddressSanitizer and UBSan, Valgrind, lcov (`./manage.sh test`, `valgrind`, `coverage`) | `manage.sh`, `CMakeLists.txt` | Nothing: the test host is Linux regardless |
| CPack `.deb` packages, `dpkg`, `lintian`, `apt` staging | `CMakeLists.txt`, `manage.sh`, `cmake/deb-post-build.cmake` | Nothing: the packages are Debian packages |
| man pages checked with `groff`, `readelf`, `nm` | `tests/unit/check_api.sh`, `check_abi.sh` | Nothing: the check host is Linux regardless |
| `manage.sh` in bash | `manage.sh` | Nothing |
