#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-10
# Functional test: libptytest is found bundled or as the system package, as WORKSPACE_LIBS says.
#
# policies/dependencies-model.md, "The workspace's own libraries": one switch,
# bundled|system|auto; auto takes the package when installed and else the
# submodule, and fails naming both ways when neither is there; the build
# initialises the submodule itself and checks that its pin is a release tag.
#
# Runs on copies of the source tree.  The system way needs libptytest-dev on
# this machine; where it is not installed that is said, not skipped silently.
#
# Usage: workspace_libs.sh <source dir> <scratch dir>
set -euo pipefail

SRC="$1"
TMP="$2"
FIX="$TMP/wslibs-$$"
case "$FIX" in
    /*/wslibs-[0-9]*) ;;
    *) echo "FAIL: unsafe fixture path: '$FIX'" >&2; exit 1 ;;
esac

run=0 failed=0
pass() { run=$((run + 1)); echo "  ok: $1"; }
fail() { run=$((run + 1)); failed=$((failed + 1)); echo "  FAIL: $1" >&2; }
cleanup() {
    case "$FIX" in
        /*/wslibs-[0-9]*) rm -rf "$FIX" ;;
    esac
}
trap cleanup EXIT

# expect <label> <log> <pattern>: the log has the pattern.
expect() { if grep -q -e "$3" "$2"; then pass "$1"; else fail "$1: no '$3' in $2"; cat "$2" >&2; fi; }
# expect_not <label> <log> <pattern>
expect_not() { if grep -q -e "$3" "$2"; then fail "$1: '$3' is in $2"; cat "$2" >&2; else pass "$1"; fi; }

# The working tree as git sees it, uncommitted changes included.
copy_tree() {
    mkdir -p "$1"
    ( cd "$SRC" && git ls-files -co --exclude-standard -z | tar --null -T - -cf - ) |
        ( cd "$1" && tar -xf - )
}
configure() {   # <tree> <build dir> <log> <cmake args...>; returns cmake's status
    local tree="$1" bdir="$2" log="$3"
    shift 3
    cmake -S "$tree" -B "$bdir" -DCMAKE_BUILD_TYPE=Debug -DTUIKIT_BUILD_TESTS=ON "$@" > "$log" 2>&1
}

copy_tree "$FIX/with"
copy_tree "$FIX/without"
[ -f "$FIX/with/deps/libptytest/CMakeLists.txt" ] || { echo "FAIL: the submodule is not checked out in $SRC" >&2; exit 1; }
( cd "$FIX/without/deps" && rm -rf libptytest )

echo "--- bundled (the default), the submodule present"
rc=0; configure "$FIX/with" "$FIX/b1" "$FIX/b1.log" || rc=$?
[ "$rc" -eq 0 ] && pass "configures" || fail "configure exits $rc"
expect "bundled and static" "$FIX/b1.log" "libptytest: bundled, deps/libptytest, static"

echo "--- bundled, the submodule missing"
rc=0; configure "$FIX/without" "$FIX/b2" "$FIX/b2.log" || rc=$?
[ "$rc" -ne 0 ] && pass "refuses" || fail "configure exits 0 without the submodule"
expect "names the missing submodule" "$FIX/b2.log" "deps/libptytest is missing"
expect "and the other way" "$FIX/b2.log" "WORKSPACE_LIBS=system"

echo "--- auto, neither there"
rc=0; configure "$FIX/without" "$FIX/a1" "$FIX/a1.log" -DWORKSPACE_LIBS=auto \
    -DCMAKE_DISABLE_FIND_PACKAGE_ptytest=ON || rc=$?
[ "$rc" -ne 0 ] && pass "refuses" || fail "configure exits 0 with neither way"
expect "names the bundled way" "$FIX/a1.log" "bundled: the submodule deps/libptytest is missing"
expect "names the system way" "$FIX/a1.log" "system:  the libptytest-dev package is not installed"

echo "--- auto, the submodule only"
rc=0; configure "$FIX/with" "$FIX/a2" "$FIX/a2.log" -DWORKSPACE_LIBS=auto \
    -DCMAKE_DISABLE_FIND_PACKAGE_ptytest=ON || rc=$?
[ "$rc" -eq 0 ] && pass "configures" || fail "configure exits $rc"
expect "falls back to the submodule" "$FIX/a2.log" "libptytest: bundled"

echo "--- a value that is none of the three"
rc=0; configure "$FIX/with" "$FIX/v1" "$FIX/v1.log" -DWORKSPACE_LIBS=sideways || rc=$?
[ "$rc" -ne 0 ] && pass "refuses" || fail "configure exits 0"
expect "says what the values are" "$FIX/v1.log" "bundled, system or auto"

echo "--- the system way"
if pkg-config --exists libptytest; then
    rc=0; configure "$FIX/without" "$FIX/s1" "$FIX/s1.log" -DWORKSPACE_LIBS=system || rc=$?
    [ "$rc" -eq 0 ] && pass "configures with the package, no submodule" || fail "configure exits $rc"
    expect "the package, shared" "$FIX/s1.log" "libptytest: system package"
    rc=0; cmake --build "$FIX/s1" --parallel "$(nproc)" --target test-buf > "$FIX/s1.build.log" 2>&1 || rc=$?
    [ "$rc" -eq 0 ] && pass "a test builds against it" || { fail "build exits $rc"; cat "$FIX/s1.build.log" >&2; }
    rc=0; ctest --test-dir "$FIX/s1" -R test-buf > "$FIX/s1.ctest.log" 2>&1 || rc=$?
    [ "$rc" -eq 0 ] && pass "and passes" || { fail "ctest exits $rc"; cat "$FIX/s1.ctest.log" >&2; }

    rc=0; configure "$FIX/without" "$FIX/a3" "$FIX/a3.log" -DWORKSPACE_LIBS=auto || rc=$?
    [ "$rc" -eq 0 ] && pass "auto configures with the package alone" || fail "configure exits $rc"
    expect "auto takes the package" "$FIX/a3.log" "libptytest: system package"
else
    echo "  NOTICE: the system way is not exercised here: libptytest-dev is not installed"
fi

echo "--- manage.sh: the value, and the submodule pin"
rc=0; WORKSPACE_LIBS=sideways "$FIX/with/manage.sh" help > "$FIX/m1.log" 2>&1 || rc=$?
[ "$rc" -ne 0 ] && pass "manage.sh refuses a wrong value" || fail "manage.sh exits 0"
expect "and says which" "$FIX/m1.log" "bundled, system or auto"

# Not a git checkout: the submodule cannot be initialised, and it says so.
rc=0; "$FIX/without/manage.sh" debug > "$FIX/m2.log" 2>&1 || rc=$?
[ "$rc" -ne 0 ] && pass "no git checkout, no submodule: debug refuses" || fail "debug exits 0"
expect "names the other way" "$FIX/m2.log" "WORKSPACE_LIBS=system"

# A real superproject whose submodule is pinned to a commit with no tag.
export GIT_CONFIG_COUNT=1 GIT_CONFIG_KEY_0=protocol.file.allow GIT_CONFIG_VALUE_0=always
export GIT_AUTHOR_NAME=t GIT_AUTHOR_EMAIL=t@example.org GIT_COMMITTER_NAME=t GIT_COMMITTER_EMAIL=t@example.org
mkdir -p "$FIX/lib"
( cd "$FIX/lib" && git init -q -b main && printf 'cmake_minimum_required(VERSION 3.20)\n' > CMakeLists.txt \
    && git add CMakeLists.txt && git commit -q -m "a commit with no release tag" )
mkdir -p "$FIX/super"
copy_tree "$FIX/super"
( cd "$FIX/super/deps" && rm -rf libptytest )
( cd "$FIX/super" && git init -q -b main && git add -A && git commit -q -m base \
    && git submodule add -q "$FIX/lib" deps/libptytest && git commit -q -m "pin" )
rc=0; "$FIX/super/manage.sh" debug > "$FIX/m3.log" 2>&1 || rc=$?
[ "$rc" -ne 0 ] && pass "a pin that is no release tag is refused" || fail "debug exits 0"
expect "and the message says so" "$FIX/m3.log" "not a release tag"

# Uninitialised: the build initialises it itself.
( cd "$FIX/super/deps" && rm -rf libptytest && mkdir libptytest )
rc=0; "$FIX/super/manage.sh" debug > "$FIX/m4.log" 2>&1 || rc=$?
expect "an empty submodule is initialised by the build" "$FIX/m4.log" "initialising the submodule"
[ -f "$FIX/super/deps/libptytest/CMakeLists.txt" ] && pass "and is checked out" || fail "the submodule is still empty"

echo "functional workspace_libs: $run checks, $failed failed"
[ "$failed" -eq 0 ]
