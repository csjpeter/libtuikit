#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-06
# Checks that every declared test is built where it is expected, and nothing else is.

#
# Every test declared in tests/CMakeLists.txt is built into
# <root>/<category>/bin/<name>, and every file there is declared
# (policies/testing-model.md, "How a test is declared, built and found").
#
# Usage: check_tests.sh <tests/CMakeLists.txt> <bin root>
set -euo pipefail

decl_file="$1"
root="$2"
failed=0 run=0
fail() { failed=$((failed + 1)); echo "FAIL: $1" >&2; }

[ -f "$decl_file" ] || { echo "FAIL: no such file: $decl_file" >&2; exit 1; }
[ -d "$root" ] || { echo "FAIL: no such directory: $root" >&2; exit 1; }

# The declarations, as the integrator reads them: a line that starts with
# "project_test(" and its first two words.
mapfile -t decls < <(sed -nE 's/^project_test\([[:space:]]*([^[:space:])]+)[[:space:]]+([^[:space:])]+).*/\1 \2/p' "$decl_file")
if [ "${#decls[@]}" -eq 0 ]; then
    echo "FAIL: no project_test( line in $decl_file" >&2
    exit 1
fi
echo "declared: ${#decls[@]} tests"

declare -A seen
for d in "${decls[@]}"; do
    cat="${d%% *}" name="${d#* }"
    run=$((run + 1))
    case "$cat" in
        unit|functional|pty|integration|e2e) ;;
        *) fail "$name: unknown category '$cat'" ;;
    esac
    if [ -n "${seen[$d]:-}" ]; then
        fail "$cat/$name is declared twice"
    fi
    seen[$d]=1
    f="$root/$cat/bin/$name"
    if [ -f "$f" ] && [ -x "$f" ]; then
        echo "  ok: $cat/bin/$name"
    else
        fail "declared, but no executable $f"
    fi
done

# Everything in a bin/ directory must be one of them.
shopt -s nullglob
for f in "$root"/*/bin/*; do
    run=$((run + 1))
    rel="${f#"$root"/}"
    cat="${rel%%/*}" name="${rel##*/}"
    if [ -z "${seen["$cat $name"]:-}" ]; then
        fail "in bin/, but not declared: $f"
    fi
done

echo "checked: $run, failed: $failed"
[ "$failed" -eq 0 ]
