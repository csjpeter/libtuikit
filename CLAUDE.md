# CLAUDE.md

Guidance for Claude Code working in this repository.

## Policies first

This project follows the workspace policies in `../policies/` — read them
before changing build, test or packaging behaviour, and re-read them
regularly: they change. They are never edited from here (see
`policies/project-boundaries-model.md`); a disagreement is reported to the
human.

`requirements/`, if it exists, is human-authored and never edited by an agent.
The other projects (tg-cli, email-cli, issue-manager, mamind, libptytest) are
read, never changed; a fault found in them is reported to the human.

## Commands

```bash
./manage.sh test       # unit tests (ASAN + UBSan) — run most often
./manage.sh pty        # PTY tests (ASAN + UBSan)
./manage.sh functional # manage.sh itself on a copy of the tree (packaging), with mocks
./manage.sh check      # every gate: unit, pty, functional, valgrind, coverage, package, lintian, consumer
build/tests/unit/bin/test-decoder <substring>   # one test
```

## Architecture

`docs/specification.md` is the design; `man/man7/libtuikit.7` the user-facing
architecture. In short: `tui_term` is the only module with signal handlers,
`tui_tty` and the two line-editing loops the only others that touch
descriptors; everything else is pure and renders into a `tui_buf`.

## Rules specific to this project

- **email-cli is the reference.** Where tg-cli and email-cli differ, email-cli
  wins, and the library may only add to email-cli's terminal capabilities,
  never remove one (its Windows stub is the one exception: Linux is the only
  target, `policies/platform-scope-model.md`). Each one has a row in `libtuikit(7)`, "EMAIL-CLI
  EQUIVALENTS", and a test.
- **Independent of the test instrument.** No code or logic is shared with
  libptytest (widths, colours, cell model). Renderer tests check the exact
  bytes *and* the emulated screen.
- **Prefix `tui_`**, and never the names listed in `docs/specification.md`
  section 3 — tg-cli and issue-manager use them (hence `tui_fieldset`, not
  `tui_form`). `tests/unit/check_api.sh`
  enforces it.
- **Every exported function has a man page** (its own or an alias in
  `CMakeLists.txt`); `check_api.sh` fails otherwise, and on any groff warning.
- **The width table is generated**, never edited: `cmake --build <dir>
  --target regen-width`. `test-text` compares it with the running glibc.
- The library never prints. Errors are return values with `errno`.
- **Every line of `src/` is run by a test** (`./manage.sh coverage` fails
  below 100 %). Failure paths are reached through `tests/common/faults.c`:
  link-time wrappers (`-Wl,--wrap`) in the test binaries, armed per test.
  Never add a test hook to the library itself.
- `tests/unit/test_compat.c` and `tests/pty/test_compat_pty.c` carry
  email-cli's and tg-cli's terminal tests over; a change that breaks one
  takes a capability away from them.
- Source, comments and documentation are in English. C11, libc only.
- **The owner's private domain is in no file** (`policies/private-names-model.md`;
  `check-private-names` fails on it). Packaging contact is the commit identity.
- **Linux is the only target.** No `#ifdef` branch for another platform
  (`check-platform` fails on one); what binds the code to Linux is listed in
  `docs/portability.md`.
- **Every source file and script starts with the workspace header**
  (`policies/source-header-model.md`): SPDX licence tag, copyright line,
  `Created:` date, one purpose line. `check-headers` fails without it; the
  licence declarations are held together by `check-license`.
- The ABI is versioned by `SOVERSION` (= major version). Within one, no
  public struct changes size and no function disappears: `abi/libtuikit<N>.abi`
  records them and `check-abi` enforces it; a new struct or function is
  recorded when it is added (the test prints the line).
