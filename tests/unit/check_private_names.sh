#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-10
# Checks that the owner's private domain is in no file of the tree.
#
# policies/private-names-model.md: the domain is referenced nowhere, and a
# contact in packaging metadata is the commit identity.  The name is not
# written here, or this script would fail its own check: the pattern is
# joined from two parts.  The tree is checked; the history is looked at
# once, with "git log -S", before a repository is published.
#
#   check_private_names.sh <source directory>
set -euo pipefail

cd "$1"
a="csasz"; b="ar.em"; c="ail"
pattern="${a}${b}${c}"

hits="$(grep -rIn -i -F -e "$pattern" --exclude-dir=build --exclude-dir=.git --exclude-dir=deps . || true)"
if [ -n "$hits" ]; then
    echo "FAIL: the private domain is in the tree (use example.org, localhost or a configuration value):" >&2
    printf '%s\n' "$hits" | cut -d: -f1,2 >&2
    exit 1
fi
files="$(find . \( -path ./build -o -path ./.git -o -path ./deps \) -prune -o -type f -print | wc -l)"
echo "check_private_names: $files files, the private domain is in none"
