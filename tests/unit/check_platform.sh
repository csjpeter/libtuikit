#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-10
# Checks that no code branch exists for a platform the project is not built for.
#
# policies/platform-scope-model.md: Linux is the only target, and no #ifdef
# branch is written for macOS, Windows or Android; what binds the code to
# Linux is written down in docs/portability.md instead.
#
#   check_platform.sh <source directory>
set -euo pipefail

cd "$1"
failed=0
fail() { failed=$((failed + 1)); echo "FAIL: $1" >&2; }

[ -s docs/portability.md ] || fail "docs/portability.md is missing or empty"

hits="$(grep -rIn -E '_WIN32|_WIN64|__MINGW(32|64)__|__CYGWIN__|__APPLE__|__MACH__|__ANDROID__|TARGET_OS_' \
        src include tests examples tools cmake CMakeLists.txt manage.sh \
        --exclude=check_platform.sh || true)"
[ -z "$hits" ] || fail "a branch for another platform:
$hits"

if [ "$failed" -ne 0 ]; then
    echo "check_platform: $failed problems" >&2
    exit 1
fi
echo "check_platform: no branch for another platform; docs/portability.md present"
