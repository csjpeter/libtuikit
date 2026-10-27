#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-27
# Functional test: manage.sh writes build/result/<verb>.result in the one specified format.
#
# policies/manage-sh-model.md, "The integrator reads the output": test, pty,
# functional, valgrind and coverage write the file, deleted at the start,
# "running" while the suites run, final on every exit path; an unknown verb
# exits 2.
#
# Runs on a tree holding only manage.sh, with cmake, ctest, lcov, valgrind and
# pkg-config of our own first in PATH: the mocks are the only outside world.
#
# Usage: result.sh <source dir> <scratch dir>
set -euo pipefail

SRC="$1"
TMP="$2"
FIX="$TMP/result-$$"
case "$FIX" in
    /*/result-[0-9]*) ;;
    *) echo "FAIL: unsafe fixture path: '$FIX'" >&2; exit 1 ;;
esac

run=0 failed=0
pass() { run=$((run + 1)); echo "  ok: $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "  FAIL: $1" >&2; }
check() {   # <label> <command...>
    local label="$1"
    shift
    if "$@"; then pass "$label"; else fail "$label"; fi
}

cleanup() {
    case "$FIX" in
        /*/result-[0-9]*) rm -rf "$FIX" ;;
    esac
}
trap cleanup EXIT

mkdir -p "$FIX/tree" "$FIX/bin"
cp "$SRC/manage.sh" "$FIX/tree/manage.sh"
git -C "$FIX/tree" init -q
echo "build/" >> "$FIX/tree/.git/info/exclude"
git -C "$FIX/tree" add manage.sh
git -C "$FIX/tree" -c user.name=t -c user.email=t@example.org commit -q -m t

# cmake: succeeds, unless MOCK_CMAKE_FAIL is set.
cat > "$FIX/bin/cmake" <<'MOCK'
#!/usr/bin/env bash
[ -z "${MOCK_CMAKE_FAIL:-}" ] || { echo "mock cmake: refusing" >&2; exit 3; }
MOCK
# pkg-config: libptytest is installed (WORKSPACE_LIBS=system).
cat > "$FIX/bin/pkg-config" <<'MOCK'
#!/usr/bin/env bash
exit 0
MOCK
cat > "$FIX/bin/valgrind" <<'MOCK'
#!/usr/bin/env bash
exit 0
MOCK
# ctest: the suites come from MOCK_unit, MOCK_pty, MOCK_functional (names
# separated by spaces); what a suite does is told by its name: "bad" fails
# with a count, "crash" and "slow" die, "script" prints a checker's summary,
# "silent" prints nothing.  While a suite runs, the result file's status is
# recorded in $FIX/seen.
cat > "$FIX/bin/ctest" <<MOCK
#!/usr/bin/env bash
FIX="$FIX"
MOCK
cat >> "$FIX/bin/ctest" <<'MOCK'
mode=run label="" re=""
while [ $# -gt 0 ]; do
    case "$1" in
        -N) mode=list ;;
        -L) label="$2"; shift ;;
        -R) re="$2"; shift ;;
        --test-dir) shift ;;
    esac
    shift
done
if [ "$mode" = list ]; then
    echo "Test project /mock"
    n=0
    for l in unit pty functional; do
        case "$label" in *"$l"*) ;; *) continue ;; esac
        var="MOCK_$l"
        for s in ${!var:-}; do
            n=$((n + 1)); echo "  Test #$n: $l.$s"
        done
    done
    exit 0
fi
name="${re#^}"; name="${name%\$}"; name="${name//\\/}"
res="$(ls -t "$FIX"/tree/build/result/*.result 2>/dev/null | head -n 1)"
[ -z "$res" ] || sed -n 's/^status: //p' "$res" >> "$FIX/seen"
case "$name" in
    *bad*)    echo "1: Assertions run:   9"; echo "1: Assertions failed:2"; exit 8 ;;
    *crash*)  echo "1: ***Exception: SegFault"; exit 8 ;;
    *slow*)   echo "1: ***Timeout"; exit 8 ;;
    *script*) echo "1: checked: 7, failed: 0"; exit 0 ;;
    *silent*) exit 0 ;;
    *)        echo "1: Assertions run:   5"; echo "1: Assertions failed:0"; exit 0 ;;
esac
MOCK
# lcov: writes the file named by --output-file; a summary of MOCK_COV lines.
cat > "$FIX/bin/lcov" <<'MOCK'
#!/usr/bin/env bash
out=""
prev=""
for a in "$@"; do
    [ "$prev" = --output-file ] && out="$a"
    prev="$a"
done
[ -z "$out" ] || { mkdir -p "$(dirname "$out")"; : > "$out"; }
case "$1" in
    --summary) c="${MOCK_COV:-10}"; echo "  lines.......: $((c * 10))% ($c of 10 lines)" ;;
    --list) echo "mock listing" ;;
esac
MOCK
cat > "$FIX/bin/genhtml" <<'MOCK'
#!/usr/bin/env bash
exit 0
MOCK
chmod +x "$FIX/bin/"*
export PATH="$FIX/bin:$PATH"
export WORKSPACE_LIBS=system
M="$FIX/tree/manage.sh"
R="$FIX/tree/build/result"

# keys <file>: the keys of the header, in order.
header_keys() { sed -n '1,/^failed:/p' "$1" | sed 's/:.*//' | paste -sd' '; }
has() { grep -qx -e "$2" "$1"; }

echo "--- test: all suites pass"
export MOCK_unit="alpha script silent" MOCK_pty="pty1" MOCK_functional="fn1"
rc=0; "$M" test > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] && pass "exit 0" || fail "exit $rc"
f="$R/test.result"
check "the file is there" test -f "$f"
check "the header keys, in order" test "$(header_keys "$f")" = "format verb commit started seconds exit status suites assertions failed"
has "$f" "format: manage-result/1" && pass "format line" || fail "format line"
has "$f" "verb: test" && pass "verb line" || fail "verb line"
has "$f" "exit: 0" && pass "exit line" || fail "exit line"
has "$f" "status: pass" && pass "status pass" || fail "status pass"
has "$f" "suites: 3" && pass "three suites" || fail "suites: $(grep '^suites' "$f")"
has "$f" "assertions: 13" && pass "assertions are summed (5 + 7 + 1)" || fail "assertions: $(grep '^assertions' "$f")"
has "$f" "failed: 0" && pass "failed 0" || fail "failed line"
grep -Eq '^commit: [0-9a-f]{7,}\+?$' "$f" && pass "commit line" || fail "commit: $(grep '^commit' "$f")"
grep -Eq '^started: [0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8}Z$' "$f" && pass "started is UTC ISO" || fail "started line"
grep -Eq '^suite: alpha pass 5 0 [0-9]+$' "$f" && pass "a suite with libptytest counts" || fail "alpha line"
grep -Eq '^suite: script pass 7 0 [0-9]+$' "$f" && pass "a checking script's counts" || fail "script line"
grep -Eq '^suite: silent pass 1 0 [0-9]+$' "$f" && pass "no counts: one assertion" || fail "silent line"
grep -qx running "$FIX/seen" && pass "the file said running while a suite ran" || fail "never running: $(cat "$FIX/seen")"
! grep -qx pass "$FIX/seen" && pass "and never an old pass" || fail "seen pass while running"

echo "--- test: a failed suite fails the verb, the others still run"
export MOCK_unit="alpha bad1 crash1 slow1 omega"
rc=0; "$M" test > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -ne 0 ] && pass "exit non-zero ($rc)" || fail "exit 0"
f="$R/test.result"
has "$f" "status: fail" && pass "status fail" || fail "status: $(grep '^status' "$f")"
has "$f" "exit: $rc" && pass "exit line agrees with the exit code" || fail "exit line: $(grep '^exit' "$f")"
grep -Eq '^suite: bad1 fail 9 2 ' "$f" && pass "a failing suite: counts kept" || fail "bad1 line"
grep -Eq '^suite: crash1 crash 0 0 ' "$f" && pass "a crash" || fail "crash line"
grep -Eq '^suite: slow1 timeout 0 0 ' "$f" && pass "a timeout" || fail "timeout line"
grep -Eq '^suite: omega pass 5 0 ' "$f" && pass "the suite after the failures ran" || fail "omega line"
has "$f" "suites: 5" && pass "five suites" || fail "suites line"
has "$f" "failed: 2" && pass "failed is the sum" || fail "failed: $(grep '^failed' "$f")"

echo "--- test: a build failure is an error, an old result is gone"
export MOCK_unit="alpha"
"$M" test > "$FIX/o.log" 2>&1
has "$R/test.result" "status: pass" && pass "a pass to begin with" || fail "no pass"
rc=0; MOCK_CMAKE_FAIL=1 "$M" test > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -ne 0 ] && pass "exit non-zero" || fail "exit 0"
f="$R/test.result"
has "$f" "status: error" && pass "status error, not the old pass" || fail "status: $(grep '^status' "$f")"
has "$f" "suites: 0" && pass "no suite" || fail "suites line"

echo "--- no suite found: an error"
export MOCK_unit=""
rc=0; "$M" test > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -ne 0 ] && has "$R/test.result" "status: error" && pass "no tests: exit non-zero, status error" || fail "no tests accepted"

echo "--- pty and functional"
export MOCK_unit="alpha" MOCK_pty="p1 p2" MOCK_functional="f1"
"$M" pty > "$FIX/o.log" 2>&1 && has "$R/pty.result" "verb: pty" && has "$R/pty.result" "suites: 2" \
    && pass "pty: its suites only" || fail "pty result"
"$M" functional > "$FIX/o.log" 2>&1 && has "$R/functional.result" "verb: functional" && has "$R/functional.result" "suites: 1" \
    && pass "functional: its suites only" || fail "functional result"

echo "--- valgrind runs every unit suite and no other kind"
"$M" valgrind > "$FIX/o.log" 2>&1 && has "$R/valgrind.result" "verb: valgrind" && has "$R/valgrind.result" "suites: 1" \
    && grep -q '^suite: alpha ' "$R/valgrind.result" && pass "unit suites only" || fail "valgrind result"
# A PATH of its own, with the mocks and the tools manage.sh uses, but no valgrind.
mkdir -p "$FIX/min"
for t in bash git date mkdir rm mv sed awk grep cat dirname tee sort paste head nproc ls tail; do
    ln -s "$(command -v "$t")" "$FIX/min/$t"
done
for t in cmake ctest lcov pkg-config; do ln -s "$FIX/bin/$t" "$FIX/min/$t"; done
rc=0; PATH="$FIX/min" "$M" valgrind > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -ne 0 ] && has "$R/valgrind.result" "status: error" && pass "a missing tool fails, no skip" || fail "missing valgrind did not fail"

echo "--- coverage: two scopes, each with its suites and its figure"
export MOCK_unit="alpha beta" MOCK_pty="p1" MOCK_functional="f1" MOCK_COV=10
rc=0; "$M" coverage > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] && pass "100 % passes" || { fail "exit $rc"; cat "$FIX/o.log" >&2; }
f="$R/coverage.result"
has "$f" "status: pass" && pass "status pass" || fail "status: $(grep '^status' "$f")"
has "$f" "scope: unit build/coverage/unit 10 10" && pass "unit scope line" || fail "unit scope: $(grep '^scope' "$f")"
has "$f" "scope: external build/coverage/external 10 10" && pass "external scope line" || fail "external scope"
grep -Eq '^suite: unit/alpha pass ' "$f" && grep -Eq '^suite: external/p1 pass ' "$f" \
    && grep -Eq '^suite: external/f1 pass ' "$f" && pass "suites are named by scope" || fail "suite names"
has "$f" "suites: 4" && pass "unit 2 + external 2" || fail "suites: $(grep '^suites' "$f")"
MOCK_COV=5 "$M" coverage > "$FIX/o.log" 2>&1 && rc=0 || rc=$?
[ "$rc" -ne 0 ] && has "$f" "exit: $rc" && has "$f" "status: fail" && pass "below the threshold: exit non-zero, status fail" \
    || fail "low coverage accepted"
MOCK_unit="alpha bad1" "$M" coverage > "$FIX/o.log" 2>&1 && rc=0 || rc=$?
[ "$rc" -ne 0 ] && grep -Eq '^suite: unit/bad1 fail ' "$f" && pass "a failing suite fails coverage and is listed" \
    || fail "failing suite hidden"

echo "--- an unknown verb exits 2, help exits 0"
rc=0; "$M" nonesuch > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -eq 2 ] && pass "unknown verb: 2" || fail "unknown verb: $rc"
grep -q '^Usage:' "$FIX/o.log" && pass "usage is printed" || fail "no usage"
rc=0; "$M" help > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -eq 0 ] && pass "help: 0" || fail "help: $rc"
rc=0; "$M" unit > "$FIX/o.log" 2>&1 || rc=$?
[ "$rc" -eq 2 ] && pass "no second name for test" || fail "'unit' is accepted"

echo "$run checks, $failed failed"
[ "$failed" -eq 0 ]
