#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-06
# Functional test: manage.sh package leaves exactly one run's packages in build/packages.

#
# "./manage.sh package deb" leaves exactly one run's packages in
# build/packages/, and nothing after a failed run
# (policies/packaging-and-release-model.md, "Where the output goes").
#
# Runs on a copy of the source tree, so the project's own build/ is not
# touched.  The failing run uses a lintian of our own that refuses
# everything: the mock is the only outside world here.
#
# Usage: package_output.sh <source dir> <scratch dir>
set -euo pipefail

SRC="$1"
TMP="$2"
FIX="$TMP/package-$$"
case "$FIX" in
    /*/package-[0-9]*) ;;
    *) echo "FAIL: unsafe fixture path: '$FIX'" >&2; exit 1 ;;
esac

run=0 failed=0
pass() { run=$((run + 1)); echo "  ok: $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "  FAIL: $1" >&2; }

cleanup() {
    case "$FIX" in
        /*/package-[0-9]*) rm -rf "$FIX" ;;
    esac
}
trap cleanup EXIT

# The working tree as git sees it, uncommitted changes included.
mkdir -p "$FIX/src"
( cd "$SRC" && git ls-files -co --exclude-standard -z | tar --null -T - -cf - ) |
    ( cd "$FIX/src" && tar -xf - )

# A package left over from an earlier version.
mkdir -p "$FIX/src/build/packages"
: > "$FIX/src/build/packages/libtuikit0_0.0.1_amd64.deb"

echo "--- a successful run"
rc=0
"$FIX/src/manage.sh" package deb > "$FIX/ok.log" 2>&1 || rc=$?
if [ "$rc" -eq 0 ]; then pass "package deb exits 0"
else fail "package deb exits $rc; log follows"; cat "$FIX/ok.log" >&2; fi

mapfile -t files < <(cd "$FIX/src/build/packages" && find . -mindepth 1 -printf '%P\n' | sort)
echo "  build/packages: ${files[*]:-(empty)}"
if [ "${#files[@]}" -eq 2 ]; then pass "two entries"
else fail "expected two entries, found ${#files[@]}"; fi
if [ ! -e "$FIX/src/build/packages/libtuikit0_0.0.1_amd64.deb" ]; then pass "the stale package is gone"
else fail "the stale package is still there"; fi

names="" dates=""
for f in "${files[@]}"; do
    p="$FIX/src/build/packages/$f"
    if [ ! -f "$p" ] || [[ "$f" != *.deb ]]; then
        fail "not a .deb file: $f"
        continue
    fi
    name="$(dpkg-deb -f "$p" Package)"
    date="$(dpkg-deb -f "$p" XB-Build-Date)"
    case "$f" in
        "${name}"_*) pass "$f holds $name" ;;
        *) fail "$f holds $name" ;;
    esac
    names="$names $name"
    dates="$dates
$date"
done
if [ "$(echo $names | tr ' ' '\n' | LC_ALL=C sort | tr '\n' ' ')" = "libtuikit-dev libtuikit0 " ]; then
    pass "libtuikit0 and libtuikit-dev"
else
    fail "packages: '$names'"
fi
ndates="$(printf '%s\n' "$dates" | sed '/^$/d' | sort -u | wc -l)"
if [ "$ndates" -eq 1 ]; then pass "one build date: one run"
else fail "$ndates build dates: '$dates'"; fi

echo "--- a run that lintian refuses"
mkdir -p "$FIX/mock"
cat > "$FIX/mock/lintian" <<'MOCK'
#!/bin/sh
echo "E: lintian mock: refused $*" >&2
exit 1
MOCK
chmod 755 "$FIX/mock/lintian"
rc=0
PATH="$FIX/mock:$PATH" "$FIX/src/manage.sh" package deb > "$FIX/refused.log" 2>&1 || rc=$?
if [ "$rc" -ne 0 ]; then pass "package deb exits $rc"
else fail "package deb exits 0 although lintian refused"; fi
if grep -q 'lintian mock: refused' "$FIX/refused.log"; then pass "the mock was the lintian that ran"
else fail "the mock did not run; log follows"; cat "$FIX/refused.log" >&2; fi
left="$(cd "$FIX/src/build/packages" && find . -mindepth 1 | wc -l)"
if [ "$left" -eq 0 ]; then pass "build/packages is empty"
else fail "build/packages holds $left entries after a failed run"; fi

echo "Assertions run: $run, failed: $failed"
if [ "$run" -lt 10 ]; then
    echo "FAIL: $run assertions, fewer than the floor of 10" >&2
    exit 1
fi
[ "$failed" -eq 0 ]
