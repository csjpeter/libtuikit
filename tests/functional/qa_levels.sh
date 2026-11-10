#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-11-10
# Functional test: manage.sh check is QA 1, check-deep is QA 2.
#
# policies/testing-model.md, "QA levels": check never starts valgrind,
# sanitizers or coverage; a diff of non-code paths only skips the suites; any
# other path, a test file included, runs all of QA 1; check-deep runs
# valgrind, the sanitizer build's tests and coverage.
#
# Runs on a git tree holding only manage.sh, with cmake, ctest, lcov,
# valgrind and pkg-config of our own first in PATH, which log their calls.
#
# Usage: qa_levels.sh <source dir> <scratch dir>
set -euo pipefail

SRC="$1"
TMP="$2"
FIX="$TMP/qalevels-$$"
case "$FIX" in
    /*/qalevels-[0-9]*) ;;
    *) echo "FAIL: unsafe fixture path: '$FIX'" >&2; exit 1 ;;
esac

run=0 failed=0
pass() { run=$((run + 1)); echo "  ok: $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "  FAIL: $1" >&2; }

cleanup() {
    case "$FIX" in
        /*/qalevels-[0-9]*) rm -rf "$FIX" ;;
    esac
}
trap cleanup EXIT

mkdir -p "$FIX/tree" "$FIX/bin"
cp "$SRC/manage.sh" "$FIX/tree/manage.sh"
G=(git -C "$FIX/tree" -c user.name=t -c user.email=t@example.org)
"${G[@]}" init -q -b main
echo "build/" >> "$FIX/tree/.git/info/exclude"
mkdir -p "$FIX/tree/docs" "$FIX/tree/src" "$FIX/tree/tests" "$FIX/tree/man"
echo doc > "$FIX/tree/docs/a.txt"
echo c > "$FIX/tree/src/a.c"
echo m > "$FIX/tree/man/a.3"
"${G[@]}" add .
"${G[@]}" commit -q -m base
"${G[@]}" checkout -q -b work

# Mocks: every call is appended to $FIX/calls.
for t in cmake valgrind lcov genhtml lintian; do
    cat > "$FIX/bin/$t" <<MOCK
#!/usr/bin/env bash
echo "$t \$*" >> "$FIX/calls"
MOCK
done
cat > "$FIX/bin/pkg-config" <<'MOCK'
#!/usr/bin/env bash
exit 0
MOCK
cat > "$FIX/bin/ctest" <<MOCK
#!/usr/bin/env bash
FIX="$FIX"
MOCK
cat >> "$FIX/bin/ctest" <<'MOCK'
mode=run label=""
while [ $# -gt 0 ]; do
    case "$1" in
        -N) mode=list ;;
        -L) label="$2"; shift ;;
        --test-dir) shift ;;
    esac
    shift
done
if [ "$mode" = list ]; then
    echo "Test project /mock"
    case "$label" in *unit*) echo "  Test #1: unit.alpha" ;; *pty*) echo "  Test #1: pty.p1" ;; *functional*) echo "  Test #1: functional.f1" ;; esac
    exit 0
fi
echo "ctest run" >> "$FIX/calls"
echo "1: Assertions run:   5"; echo "1: Assertions failed:0"
MOCK
chmod +x "$FIX/bin/"*
export PATH="$FIX/bin:$PATH"
export WORKSPACE_LIBS=system
# This test runs inside check; the switches of the outer run are not ours.
unset QA1_PLAIN SANITIZE
M="$FIX/tree/manage.sh"
cd "$FIX/tree"

# The package steps need real tools: they are stubbed in a copy of manage.sh.
for fn in do_package run_lintian run_consumer; do
    sed -i "s#^$fn() {#$fn() { echo \"$fn \$*\" >> \"$FIX/calls\"; return 0; }\n${fn}_real() {#" "$M"
done
bash -n "$M" || { echo "FAIL: the stubbed manage.sh does not parse" >&2; exit 1; }
"${G[@]}" commit -q -am stubs
"${G[@]}" branch -f main HEAD
"${G[@]}" checkout -q -b work2
export MAINLINE_REFS=main

calls() { cat "$FIX/calls" 2>/dev/null || true; }
fresh() { : > "$FIX/calls"; }
# grep on the file, not a pipe: with pipefail a closed pipe fails the check.
has_call() { grep -q -e "$1" "$FIX/calls"; }
export MAINLINE_REFS=main

echo "--- check: a docs-only diff skips the suites"
echo more >> docs/a.txt; echo "# x" > NOTES.md
fresh
rc=0; "$M" check > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] && pass "exit 0" || { fail "exit $rc"; cat "$FIX/o.log" >&2; }
grep -q '^no code changed: suites skipped: unit pty functional' "$FIX/o.log" && pass "says so" || fail "no skip line"
! has_call "ctest run" && pass "no suite ran" || fail "a suite ran"
! has_call "do_package" && pass "no package build for docs" || fail "package built"

echo "--- check: a man page also builds and lints the packages"
echo more >> man/a.3
fresh
"$M" check > "$FIX/o.log" 2>&1 && pass "exit 0" || fail "man diff failed"
grep -q '^no code changed' "$FIX/o.log" && pass "still no suites" || fail "suites not skipped"
has_call "do_package" && has_call "run_lintian" && pass "package and lintian ran" || fail "package checks missing"
! has_call "ctest run" && pass "and no suite" || fail "a suite ran"

echo "--- check: a test file runs all of QA 1, no valgrind, no coverage, no sanitizer"
echo t > tests/t.c
fresh
rc=0; "$M" check > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] && pass "exit 0" || { fail "exit $rc"; cat "$FIX/o.log" >&2; }
! grep -q '^no code changed' "$FIX/o.log" && pass "not skipped" || fail "skipped"
[ "$(grep -c '^ctest run' "$FIX/calls")" -eq 3 ] && pass "unit, pty and functional ran" || fail "ctest runs: $(grep -c '^ctest run' "$FIX/calls")"
has_call "do_package" && has_call "run_lintian" && has_call "run_consumer" && pass "package checks ran" || fail "package checks missing"
! has_call "TUIKIT_TEST_WRAPPER=valgrind" && pass "valgrind never started" || fail "valgrind started"
! has_call "^lcov" && pass "lcov never started" || fail "coverage started"
! has_call 'TUIKIT_ENABLE_ASAN=ON' && has_call 'TUIKIT_ENABLE_ASAN=OFF' && pass "build without sanitizers" || fail "sanitizer build in check"
[ ! -e build/result/valgrind.result ] && [ ! -e build/result/coverage.result ] && pass "no QA 2 result files" || fail "QA 2 result files"

echo "--- check: source and docs together run everything"
"${G[@]}" checkout -q -- . 2>/dev/null || true
fresh
"$M" check > "$FIX/o.log" 2>&1
! grep -q '^no code changed' "$FIX/o.log" && has_call "ctest run" && pass "a code path among docs: all of QA 1" || fail "skipped"

echo "--- check: a failing suite fails check"
cat > "$FIX/bin/ctest.fail" <<'MOCK'
MOCK
rm "$FIX/bin/ctest.fail"
sed -i 's/^echo "1: Assertions run:   5"; echo "1: Assertions failed:0"$/[ -z "${MOCK_FAIL:-}" ] || { echo "1: Assertions run:   5"; echo "1: Assertions failed:2"; exit 8; }\n&/' "$FIX/bin/ctest"
rc=0; MOCK_FAIL=1 "$M" check > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -ne 0 ] && grep -q 'gate(s) failed' "$FIX/o.log" && pass "non-zero, reported" || fail "failure hidden"

echo "--- check-deep: sanitizer tests, valgrind and coverage"
fresh
rc=0; "$M" check-deep > "$FIX/o.log" 2>&1 || rc=$?
has_call "TUIKIT_TEST_WRAPPER=valgrind" && pass "valgrind started" || fail "no valgrind"
has_call "^lcov" && pass "coverage measured" || fail "no lcov"
has_call 'TUIKIT_ENABLE_ASAN=ON' && pass "sanitizer build" || fail "no sanitizer build"
for v in test pty functional valgrind coverage; do
    [ -f "build/result/$v.result" ] && pass "result file: $v" || fail "no result file: $v"
done
! has_call "do_package" && pass "no package step in QA 2" || fail "package in check-deep"

echo "--- check-deep runs everything even with no changed path"
"${G[@]}" stash -q -u 2>/dev/null || true
fresh
"$M" check-deep > "$FIX/o.log" 2>&1 || true
has_call "TUIKIT_TEST_WRAPPER=valgrind" && pass "check-deep never skips" || fail "check-deep skipped"

echo "$run checks, $failed failed"
[ "$failed" -eq 0 ]
