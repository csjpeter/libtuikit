#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-16
# Checks that docs/functional-coverage.md has a row, with a calling test, for every capability.
#
# policies/testing-model.md, "Every capability has a functional test": the
# capabilities of the library are its exported functions (nm -D) and the
# inline functions of its public headers.  Each needs a row of the form
#   | `name` [(inline)] | `tests/functional/cap_x.c` | `phase` |
# whose test file exists and calls the function; a row of a function that is
# gone fails too.
#
# Usage: check_coverage_map.sh <libtuikit.so> <source dir>
set -euo pipefail

lib="$1"
src="$2"
doc="$src/docs/functional-coverage.md"
failed=0 run=0
fail() { failed=$((failed + 1)); echo "FAIL: $1" >&2; }

[ -f "$lib" ] || { echo "FAIL: no such library: $lib" >&2; exit 1; }
[ -f "$doc" ] || { echo "FAIL: no such file: $doc" >&2; exit 1; }

mapfile -t exported < <(nm -D --defined-only "$lib" | awk '$2 == "T" && $3 ~ /^tui_/ { print $3 }' | sort)
mapfile -t inline < <(grep -h -A1 '^static inline' "$src"/include/tuikit/*.h \
    | grep -o 'tui_[a-z0-9_]*(' | tr -d '(' | sort -u)
[ "${#exported[@]}" -gt 0 ] || { echo "FAIL: nm found no exported tui_ function in $lib" >&2; exit 1; }
[ "${#inline[@]}" -gt 0 ] || { echo "FAIL: no inline function found in the headers" >&2; exit 1; }

# The rows: name, test file, phase.
declare -A row_file
while IFS='|' read -r _ cap file phase _; do
    name=$(echo "$cap" | sed -nE 's/^ *`(tui_[a-z0-9_]+)`.*/\1/p')
    [ -n "$name" ] || continue
    f=$(echo "$file" | tr -d ' `')
    p=$(echo "$phase" | tr -d ' `')
    if [ -n "${row_file[$name]:-}" ]; then fail "$name has two rows"; continue; fi
    row_file[$name]="$f"
    [ -n "$p" ] || fail "$name: no phase"
done < <(grep '^| `tui_' "$doc")

declare -A known
for name in "${exported[@]}" "${inline[@]}"; do
    run=$((run + 1))
    known[$name]=1
    f="${row_file[$name]:-}"
    if [ -z "$f" ]; then
        fail "$name: no row in docs/functional-coverage.md"
        continue
    fi
    if [ ! -f "$src/$f" ]; then
        fail "$name: test file $f does not exist"
        continue
    fi
    # A call, not a mention in a comment.  awk reads all its input: with
    # grep -q the reader exits at the first match, the writer then dies of
    # SIGPIPE and pipefail turns a found call into a failure (TUI-56).
    calls=$(grep -v '^[[:space:]]*\(/\*\|\*\|//\)' "$src/$f" \
            | awk -v re="(^|[^A-Za-z0-9_])${name}[(]" '$0 ~ re { n++ } END { print n + 0 }')
    if [ "$calls" -eq 0 ]; then
        fail "$name: $f does not call it"
    fi
done

for name in "${!row_file[@]}"; do
    run=$((run + 1))
    [ -n "${known[$name]:-}" ] || fail "$name: a row of a function that does not exist"
done

echo "checked: $run, failed: $failed (${#exported[@]} exported, ${#inline[@]} inline)"
[ "$failed" -eq 0 ]
