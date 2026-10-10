#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-10
# Checks that every licence declaration of the project says the same.

#
# The licence is declared once and every place says the same (TUI-21,
# policies/studies/licensing-survey.md): LICENSE is the GPL version 3, each
# source, script, build and man page file carries the SPDX identifier
# "GPL-3.0-only OR LicenseRef-Proprietary" and no other one, and the places a package or a reader
# looks at (debian/copyright, the RPM field, README.md) agree.
#
#   check_license.sh <source directory>
set -euo pipefail

src="$1"
spdx="GPL-3.0-only OR LicenseRef-Proprietary"   # the tag of every file
pkg="GPL-3.0-only"                              # what a package field says
failed=0 run=0
fail() { failed=$((failed + 1)); echo "FAIL: $1" >&2; }
check() { run=$((run + 1)); }

cd "$src"

check
grep -q '^ *GNU GENERAL PUBLIC LICENSE$' LICENSE &&
grep -q '^ *Version 3, 29 June 2007$' LICENSE || fail "LICENSE is not the text of the GPL version 3"

files=0
while IFS= read -r f; do
    files=$((files + 1))
    check
    head -5 "$f" | grep -q "SPDX-License-Identifier: $spdx" ||
        fail "$f: no 'SPDX-License-Identifier: $spdx' in its first lines"
done < <(find . \( -path ./build -o -path ./.git \) -prune -o -type f \
              \( -name '*.c' -o -name '*.h' -o -name '*.sh' -o -name '*.cmake' \
                 -o -name '*.in' -o -name CMakeLists.txt -o -name '*.3' -o -name '*.7' \) \
              -print | sort)
check
[ "$files" -ge 60 ] || fail "only $files files found to check: the file search has gone wrong"

# No other identifier anywhere (the generator's template string counts too).
check
other="$(grep -rIn 'SPDX-License-Identifier' --exclude-dir=build --exclude-dir=.git . |
         grep -v "SPDX-License-Identifier: $spdx" | grep -v -e 'check_license.sh' -e 'check_headers.sh' || true)"
[ -z "$other" ] || fail "another licence identifier:
$other"

# "Or later" is not agreed to in advance (policies/licensing-model.md): the
# licence text itself offers it, and nothing else says it.
check
later="$(grep -rIn -i -E 'or[ -]later|GPL-3\+|any later version' --exclude-dir=build --exclude-dir=.git \
         --exclude=LICENSE --exclude=check_license.sh . || true)"
[ -z "$later" ] || fail "'or later' is said:
$later"

check
grep -q '^License: GPL-3$' CMakeLists.txt || fail "CMakeLists.txt: debian/copyright does not say 'License: GPL-3'"
# The dual licence is said in LICENSE and in debian/copyright
# (policies/licensing-model.md, "What the dual licence is written as").
check
head -4 LICENSE | tr '\n' ' ' | grep -q 'Licensed under the GNU GPL version 3 only. The copyright holder also licenses this work, separately, under proprietary terms.' ||
    fail "LICENSE does not start with the dual-licence sentence"
check
tr '\n' ' ' < CMakeLists.txt | tr -s ' ' | grep -q 'The copyright holder also licenses this work, separately, under proprietary terms' ||
    fail "CMakeLists.txt: debian/copyright does not say that the holder licenses the work separately"

check
grep -q "CPACK_RPM_PACKAGE_LICENSE \"$pkg\"" CMakeLists.txt || fail "CMakeLists.txt: the RPM licence is not $pkg"
check
! grep -q 'License: BSD' CMakeLists.txt || fail "CMakeLists.txt: a BSD licence paragraph is left"
check
grep -q "^$pkg, see \`LICENSE\`" README.md || fail "README.md: the licence section does not say $pkg"
check
grep -q "License: $pkg" docs/specification.md || fail "docs/specification.md: the licence is not $pkg"

if [ "$failed" -ne 0 ]; then
    echo "check_license: $failed of $run checks failed" >&2
    exit 1
fi
echo "check_license: $run checks over $files files, all $spdx"
