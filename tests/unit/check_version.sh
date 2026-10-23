#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Created: 2026-10-23)
# Checks the package version derivation: project version + total commit count.

#
# In a throwaway repository: N commits give V.N; the same tree after any tag
# gives V.N again; one more commit gives V.N+1; a raised project version
# sorts above; without git the bare V.  (TUI-66)
#
#   check_version.sh <source directory> <scratch directory>
set -euo pipefail

src="$1"
tmp="$2/version-$$"
case "$tmp" in /*/version-[0-9]*) ;; *) echo "FAIL: unsafe path '$tmp'" >&2; exit 1 ;; esac
trap 'rm -rf "$tmp"' EXIT
mkdir -p "$tmp/repo" "$tmp/nogit"

failed=0 run=0
derive() { cmake -DSRC="$1" -DPROJECT_VERSION="$2" -P "$src/cmake/TuikitVersion.cmake" 2>&1 | sed 's/^-- //'; }
expect() {
    run=$((run + 1))
    if [[ "$2" == "$3" ]]; then echo "  ok: $1 = $2"
    else failed=$((failed + 1)); echo "FAIL: $1: expected '$3', got '$2'" >&2; fi
}

cd "$tmp/repo"
git init -q .
git config user.email t@example.org
git config user.name t
for i in 1 2 3 4; do git commit -q --allow-empty -m "c$i"; done
expect "4 commits" "$(derive "$tmp/repo" 0.1.0)" "0.1.0.4"
git tag -a v0.1.0 -m release
expect "same tree after the tag" "$(derive "$tmp/repo" 0.1.0)" "0.1.0.4"
git commit -q --allow-empty -m c5
expect "one more commit after the tag" "$(derive "$tmp/repo" 0.1.0)" "0.1.0.5"
git tag -a v0.1.0.5 -m built
expect "after a build tag" "$(derive "$tmp/repo" 0.1.0)" "0.1.0.5"

run=$((run + 1))
if dpkg --compare-versions "$(derive "$tmp/repo" 0.2.0)" gt "0.1.0.5"; then echo "  ok: a raised version sorts above"
else failed=$((failed + 1)); echo "FAIL: 0.2.0.5 does not sort above 0.1.0.5" >&2; fi

expect "no git" "$(derive "$tmp/nogit" 0.1.0)" "0.1.0"

echo "check_version: $run checks, $failed failed"
[[ "$failed" -eq 0 ]]
