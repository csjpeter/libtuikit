#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-29
# Functional test: manage.sh test-base (policies/manage-sh-model.md).
# The project has no test base: the verb says so, exits 0, is idempotent,
# is listed in help, and deps runs it.
# Usage: test_base.sh <source dir> <scratch dir>
set -euo pipefail
SRC="$1"
run=0 failed=0
pass() { run=$((run + 1)); echo "  ok: $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "  FAIL: $1" >&2; }

for i in 1 2; do
    if out="$("$SRC/manage.sh" test-base 2>&1)"; then pass "test-base run $i exits 0"; else fail "test-base run $i exits 0"; fi
    case "$out" in *"no test base"*) pass "run $i says there is none" ;; *) fail "run $i says there is none: $out" ;; esac
done
"$SRC/manage.sh" help | grep -q '^  test-base ' && pass "help lists test-base" || fail "help lists test-base"
grep -q '^    test_base$' "$SRC/manage.sh" && pass "deps calls test_base" || fail "deps calls test_base"
echo "test_base: $run checks, $failed failed"
[ "$failed" -eq 0 ]
