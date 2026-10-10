#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-03
# API conformance: a man page for every exported function and no groff warning.

# API conformance checks that a C test cannot make:
#
#   1. every function the shared library exports has a man page in
#      section 3 (its own page or an alias), as the specification requires;
#   2. every man page formats without a groff warning;
#   3. no public header defines a name the library promised never to define
#      (docs/specification.md, section 3) -- names that tg-cli and
#      issue-manager already use for their own symbols.
#
#   check_api.sh <libtuikit.so> <built man dir> <include dir>
#
# Exits non-zero with the reason on any finding.  Prints the number of
# checks made, so a check that silently checked nothing is visible.
set -eu

lib="$1"
mandir="$2"
incdir="$3"
failed=0
checks=0

[ -f "$lib" ] || { echo "FAIL: no library at $lib" >&2; exit 1; }
[ -d "$mandir/man3" ] || { echo "FAIL: no man3 directory in $mandir" >&2; exit 1; }

# 1. exported functions vs. man pages, and nothing exported outside tui_
symbols=$(nm -D --defined-only "$lib" | awk '$2 == "T" && $3 ~ /^tui_/ { print $3 }' | sort)
[ -n "$symbols" ] || { echo "FAIL: nm found no exported tui_ function in $lib" >&2; exit 1; }
others=$(nm -D --defined-only "$lib" | awk '$2 ~ /^[TDBR]$/ && $3 !~ /^tui_/ { print $3 }' | sort)
if nm -D --defined-only "$lib" | grep -q '__gcov_'; then
    # A coverage build: libgcov exports names of its own (mangle_path, ...).
    others=""
fi
checks=$((checks + 1))
if [ -n "$others" ]; then
    echo "FAIL: exported outside the tui_ namespace: $(echo "$others" | tr '\n' ' ')"
    failed=$((failed + 1))
fi
for sym in $symbols; do
    checks=$((checks + 1))
    if [ ! -e "$mandir/man3/$sym.3.gz" ]; then
        echo "FAIL: exported function $sym has no man page (man/man3/$sym.3 or an alias)"
        failed=$((failed + 1))
    fi
done

# 2. every page formats cleanly
for page in "$mandir"/man3/*.3.gz "$mandir"/man7/*.7.gz; do
    [ -f "$page" ] || continue
    [ -L "$page" ] && continue
    checks=$((checks + 1))
    warnings=$(gzip -dc "$page" | groff -t -man -ww -z -Tutf8 2>&1)
    if [ -n "$warnings" ]; then
        echo "FAIL: $(basename "$page") does not format cleanly:"
        echo "$warnings" | sed 's/^/    /'
        failed=$((failed + 1))
    fi
done

# 3. names the library never defines
reserved='tui_textarea[A-Za-z0-9_]*|tui_text_summary|tui_app_[A-Za-z0-9_]*|tui_model_[A-Za-z0-9_]*|tui_input_[A-Za-z0-9_]*|tui_form_[A-Za-z0-9_]*|tui_api_[A-Za-z0-9_]*|tui_run|tui_view_t|tui_modal_t|TUI_EVENT_[A-Z0-9_]*|TUI_FOCUS_[A-Z0-9_]*|TUI_VIEW_[A-Z0-9_]*|TUI_MODAL_[A-Z0-9_]*|TUI_FORM_[A-Z0-9_]*|TUI_SUB_[A-Z0-9_]*|TUI_MIN_[A-Z0-9_]*|TUI_MAX_[A-Z0-9_]*|TUI_KEY_MAX|TUI_INPUT_MAX|TUI_SCREEN_H|TUI_PANE_H|TUI_LIST_VIEW_H|TUI_APP_H'
headers=$(ls "$incdir"/tuikit/*.h)
[ -n "$headers" ] || { echo "FAIL: no headers in $incdir/tuikit" >&2; exit 1; }
for h in $headers; do
    checks=$((checks + 1))
    found=$(grep -n -o -w -E "$reserved" "$h" | sort -u | tr '\n' ' ')
    if [ -n "$found" ]; then
        echo "FAIL: $(basename "$h") uses reserved names: $found"
        failed=$((failed + 1))
    fi
done

echo "Assertions run:   $checks"
echo "Assertions failed:$failed"
# Floor: a check that found nothing to check must not pass.
if [ "$checks" -lt 140 ]; then
    echo "FAIL: $checks checks, fewer than the floor of 140"
    exit 1
fi
[ "$failed" -eq 0 ]
