#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-23
# Fails if a widget of the library draws with a fixed colour instead of a scheme role.

#
# A widget takes its colours from the roles of the active scheme (TUI-65).
# Only src/scheme.c (the built-in scheme) and src/selection.c (the built-in
# selection by colour depth, and its reduction) name a colour; the rest of
# src/ must not: no named colour constant, no TUI_COLOR_256() or
# TUI_COLOR_RGB() literal, no tui_style_make() with a colour argument.
#
#   check_roles.sh <source directory>
set -euo pipefail

src="$1"
failed=0 run=0
fail() { failed=$((failed + 1)); echo "FAIL: $1" >&2; }

cd "$src/src"
colour='TUI_(BLACK|RED|GREEN|YELLOW|BLUE|MAGENTA|CYAN|WHITE|BRIGHT_[A-Z]+)\b|TUI_COLOR_(256|RGB)\('

files=0
for f in *.c; do
    case "$f" in scheme.c|selection.c) continue ;; esac
    files=$((files + 1))
    run=$((run + 1))
    hits="$(grep -nE "$colour" "$f" || true)"   # no hit is the good case: grep exits 1
    [[ -z "$hits" ]] || fail "$f draws with a fixed colour (use tui_scheme_style(TUI_ROLE_*)): $hits"
    run=$((run + 1))
    hits="$(grep -nE 'tui_style_make\([^)]*TUI_' "$f" | grep -v 'TUI_COLOR_DEFAULT' || true)"
    [[ -z "$hits" ]] || fail "$f makes a style with a fixed colour: $hits"
done

run=$((run + 1))
[[ "$files" -ge 10 ]] || fail "only $files source files were checked: is the directory right?"

echo "check_roles: $run checks, $failed failed ($files files)"
[[ "$failed" -eq 0 ]]
