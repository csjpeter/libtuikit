#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-11
# Functional test: manage.sh times its verbs and prints the gate line.

#
# Every verb that does work appends "<UTC time> <verb> <n>s <ok|fail>" to
# build/timing.log, success or failure, and "./manage.sh timing [KEY]"
# prints (and posts) the gate line from it
# (policies/manage-sh-model.md, "Every verb times itself").
#
# Runs on a tree holding only manage.sh, with cmake and issue-cli of our own
# first in PATH: the mocks are the only outside world.
#
# Usage: timing.sh <source dir> <scratch dir>
set -euo pipefail

SRC="$1"
TMP="$2"
FIX="$TMP/timing-$$"
case "$FIX" in
    /*/timing-[0-9]*) ;;
    *) echo "FAIL: unsafe fixture path: '$FIX'" >&2; exit 1 ;;
esac

run=0 failed=0
pass() { run=$((run + 1)); echo "  ok: $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "  FAIL: $1" >&2; }

cleanup() {
    case "$FIX" in
        /*/timing-[0-9]*) rm -rf "$FIX" ;;
    esac
}
trap cleanup EXIT

mkdir -p "$FIX/tree" "$FIX/bin"
cp "$SRC/manage.sh" "$FIX/tree/manage.sh"
LOG="$FIX/tree/build/timing.log"

# cmake: succeeds, unless MOCK_CMAKE_FAIL is set.
cat > "$FIX/bin/cmake" <<'MOCK'
#!/usr/bin/env bash
[ -z "${MOCK_CMAKE_FAIL:-}" ] || { echo "mock cmake: refusing" >&2; exit 3; }
MOCK
# issue-cli: records its arguments, one per line.
cat > "$FIX/bin/issue-cli" <<MOCK
#!/usr/bin/env bash
printf '%s\n' "\$@" > "$FIX/issue-cli.args"
MOCK
chmod +x "$FIX/bin/cmake" "$FIX/bin/issue-cli"
export PATH="$FIX/bin:$PATH"
M="$FIX/tree/manage.sh"

echo "--- no log: not measured"
rc=0
out="$("$M" timing 2>&1)" || rc=$?
[ "$out" = "Gate: not measured" ] && pass "prints 'Gate: not measured'" || fail "printed '$out'"
[ "$rc" -ne 0 ] && pass "exits non-zero" || fail "exited 0"
rc=0
"$M" timing TUI-1 > /dev/null 2>&1 || rc=$?
if [ "$rc" -ne 0 ] && [ ! -e "$FIX/issue-cli.args" ]; then pass "nothing is posted"
else fail "a comment was posted without a measurement"; fi

echo "--- a successful verb is logged"
"$M" build > "$FIX/build.log" 2>&1 || { fail "build failed"; cat "$FIX/build.log" >&2; }
if [ "$(wc -l < "$LOG")" -eq 1 ] &&
   grep -Eq '^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9:]{8}Z build [0-9]+s ok$' "$LOG"; then
    pass "one 'build <n>s ok' line"
else fail "log: $(cat "$LOG")"; fi

echo "--- a failing verb is logged and fails"
rc=0
MOCK_CMAKE_FAIL=1 "$M" build > "$FIX/fail.log" 2>&1 || rc=$?
[ "$rc" -eq 3 ] && pass "exit status is the verb's" || fail "exit status $rc"
if [ "$(tail -n 1 "$LOG" | awk '{print $2, $4}')" = "build fail" ]; then pass "logged as fail"
else fail "log: $(cat "$LOG")"; fi

echo "--- the gate line is built from the log, not guessed"
mkdir -p "$FIX/tree/build"
cat > "$LOG" <<'LOG'
2026-10-11T10:00:00Z check 100s ok
2026-10-11T10:05:00Z check 90s ok
2026-10-11T10:06:00Z package 20s ok
2026-10-11T10:07:00Z check 500s fail
2026-10-11T10:08:00Z test 7s ok
LOG
out="$("$M" timing)"
[ "$out" = "Gate: 110s (check 90s, package 20s)" ] && pass "latest ok run of each part; install left out" \
    || fail "printed '$out'"

echo "--- with a key the line is posted"
"$M" timing TUI-7 > /dev/null
if [ "$(cat "$FIX/issue-cli.args")" = "issue
comment
TUI-7
Gate: 110s (check 90s, package 20s)" ]; then pass "issue-cli issue comment TUI-7 <line>"
else fail "issue-cli got: $(cat "$FIX/issue-cli.args")"; fi

echo "$run checks, $failed failed"
[ "$failed" -eq 0 ]
