#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright 2026 Péter Császár
#
# Within one SOVERSION the ABI only grows (TUI-15): no public struct a
# program allocates changes its size or alignment, and no exported
# function disappears.  The record is abi/libtuikit<SOVERSION>.abi; a
# library whose SONAME has no record fails, so a SOVERSION change comes
# with a new record.
#
#   check_abi.sh <libtuikit.so> <abi-sizes program> <abi directory>
#
# A struct or a function that is new must be recorded too (the lines to
# add are printed), so that the next change to it is caught.
set -euo pipefail

lib="$1" sizes_prog="$2" abi_dir="$3"
failed=0 run=0
fail() { failed=$((failed + 1)); echo "FAIL: $1" >&2; }

soname="$(readelf -d "$lib" | sed -n 's/.*Library soname: \[\(.*\)\].*/\1/p')"
sover="${soname##*.so.}"
case "$sover" in
    ''|*[!0-9]*) echo "FAIL: cannot read the SOVERSION of $lib (soname '$soname')" >&2; exit 1 ;;
esac
record="$abi_dir/libtuikit$sover.abi"
[ -f "$record" ] || { echo "FAIL: no ABI record for SOVERSION $sover: $record" >&2; exit 1; }
arch="$(uname -m)"
rec_arch="$(sed -n 's/^arch //p' "$record")"
if [ "$arch" != "$rec_arch" ]; then
    echo "FAIL: the record is for $rec_arch, this machine is $arch: record this architecture" >&2
    exit 1
fi
echo "SOVERSION $sover, $arch, record $record"

# Sizes and alignments, both ways.
current_sizes="$("$sizes_prog")"
while read -r kind name size align; do
    [ "$kind" = size ] || continue
    run=$((run + 1))
    now="$(printf '%s\n' "$current_sizes" | awk -v n="$name" '$2 == n { print $3 " " $4 }')"
    if [ -z "$now" ]; then
        fail "$name is recorded but no longer measured (removed from the headers or abi_sizes.c?)"
    elif [ "$now" != "$size $align" ]; then
        fail "$name: size and alignment $now, recorded $size $align -- a change needs a new SOVERSION"
    fi
done < "$record"
while read -r kind name size align; do
    run=$((run + 1))
    if ! grep -qx "size $name [0-9]* [0-9]*" "$record"; then
        fail "$name is not recorded; add: size $name $size $align"
    fi
done <<< "$current_sizes"

# Exported functions, both ways.
current_syms="$(nm -D --defined-only "$lib" | awk '$2 == "T" && $3 ~ /^tui_/ { print $3 }' | LC_ALL=C sort)"
while read -r kind name; do
    [ "$kind" = symbol ] || continue
    run=$((run + 1))
    if ! printf '%s\n' "$current_syms" | grep -qx "$name"; then
        fail "$name is recorded but no longer exported -- a removal needs a new SOVERSION"
    fi
done < "$record"
while read -r name; do
    run=$((run + 1))
    if ! grep -qx "symbol $name" "$record"; then
        fail "$name is exported but not recorded; add: symbol $name"
    fi
done <<< "$current_syms"

echo "checked: $run, failed: $failed"
if [ "$run" -lt 50 ]; then
    echo "FAIL: $run checks, fewer than the floor of 50" >&2
    exit 1
fi
[ "$failed" -eq 0 ]
