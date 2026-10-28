#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-03
# Maintenance front end of libtuikit: build, test, check and package.

# Every verb delegates to the real tool -- CMake, CTest, CPack, Valgrind,
# lcov -- and decides nothing their own configuration should decide.  The
# vocabulary is the workspace's: see policies/manage-sh-model.md.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Every build tree lives under build/, which "clean" removes as a whole and
# which is never committed.  build/ itself is the Release tree; the packages
# of one run, and nothing else, are in build/packages/ as the packaging
# policy asks.
BUILD_DIR="$SCRIPT_DIR/build"
PKG_DIR="$BUILD_DIR/packages"
BUILD_DIR_DEBUG="$BUILD_DIR/_debug"
BUILD_DIR_TEST="$BUILD_DIR/_test"
BUILD_DIR_VALGRIND="$BUILD_DIR/_valgrind"
BUILD_DIR_CONSUMER="$BUILD_DIR/_consumer"

# Line coverage of the library sources below this fails the run: every
# line of src/ is run by a test (failure paths through tests/common/faults.c).
COVERAGE_THRESHOLD=100
# Policy floors (policies/testing-model.md): unit tests alone, and the
# externally driven suites (functional + PTY) together.
COVERAGE_UNIT_THRESHOLD=80
COVERAGE_EXTERNAL_THRESHOLD=80

JOBS="$(nproc)"

# How libptytest, the test instrument, is found (policies/dependencies-model.md):
#   bundled  the submodule deps/libptytest, pinned to a release tag, built
#            with the tests and linked statically (the default: this is an
#            open-source project, and a stranger needs nothing of ours)
#   system   the installed libptytest-dev package, linked dynamically
#   auto     the package when it is installed, else the submodule
WORKSPACE_LIBS="${WORKSPACE_LIBS:-bundled}"
case "$WORKSPACE_LIBS" in
    bundled|system|auto) ;;
    *) echo "WORKSPACE_LIBS is '$WORKSPACE_LIBS'; it is bundled, system or auto" >&2; exit 1 ;;
esac

usage() {
    cat <<'USAGE'
Usage: ./manage.sh <command> [args]

Commands:
  deps                  Install build and test dependencies (apt or dnf; needs root)
  build                 Release build (build/)
  debug                 Debug build with ASAN + UBSan (build/_debug)
  test [ctest args]     Unit tests, ASAN + UBSan -- the command run most often
  pty [ctest args]      PTY tests: real programs through a real PTY, ASAN + UBSan
  functional [ctest args]  Functional tests: manage.sh itself, with mocks of our own
  coverage              Line coverage of the library (unit, external = functional + pty,
                        all together); fails below the thresholds; leaves the counters
                        in build/coverage/unit and build/coverage/external
  valgrind              Every unit suite under Valgrind on a clean Release build
  consumer              Build and run the example against the built .deb packages
  check                 Every gate, with a summary to compare against CI
  package [deb|rpm|all] Build the packages with CPack into build/packages/ (libtuikit0,
                        libtuikit-dev), lintian-checked; empty on failure; never tags or pushes
  tag-build VERSION     Make the annotated tag vVERSION on HEAD, locally; pushes nothing
  timing [KEY-N]        Print the gate line from build/timing.log; with a ticket key,
                        comment it on that ticket (issue-cli)
  clean                 Remove build/
  help                  This message

test, pty, functional, valgrind and coverage write build/result/<verb>.result
(policies/manage-sh-model.md); an unknown command exits 2.

libptytest, the test instrument, comes as the submodule deps/libptytest
(WORKSPACE_LIBS=bundled, the default; deps and the test builds initialise it
and check that its pin is a release tag) or as the installed libptytest-dev
(WORKSPACE_LIBS=system); WORKSPACE_LIBS=auto takes the package when present.

One suite, or one test of a suite:
  ./manage.sh test -R test-decoder
  build/tests/unit/bin/test-decoder test_paste    (substring filter; -l lists)
USAGE
}

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------

# libptytest, the PTY test instrument, is not in these lists: in the bundled
# way it is the submodule, and in the system way libptytest-dev comes from the
# workspace's own apt repository (added below).
APT_DEPS="build-essential cmake pkg-config git valgrind lcov lintian dpkg-dev
          rpm file locales groff"
DNF_DEPS="gcc make cmake pkgconf-pkg-config git valgrind lcov rpm-build
          dpkg glibc-langpack-en groff"

# libs_mode: the way libptytest is found, "auto" resolved: bundled or system.
libs_mode() {
    if [ "$WORKSPACE_LIBS" = auto ]; then
        if pkg-config --exists libptytest; then echo system; else echo bundled; fi
    else
        echo "$WORKSPACE_LIBS"
    fi
}

# ensure_libs: libptytest is there in the chosen way, or this says what to do.
# Bundled: the submodule is initialised when it is missing or not at the
# commit the superproject pins, and the pin must be a release tag.
ensure_libs() {
    local mode sub="$SCRIPT_DIR/deps/libptytest" status tag
    mode="$(libs_mode)"
    if [ "$mode" = system ]; then
        if ! pkg-config --exists libptytest; then
            echo "libptytest: WORKSPACE_LIBS=system, but libptytest-dev is not installed." >&2
            echo "  Install it (./manage.sh deps with WORKSPACE_LIBS=system)," >&2
            echo "  or use the bundled submodule: WORKSPACE_LIBS=bundled." >&2
            return 1
        fi
        return 0
    fi
    status="$(git -C "$SCRIPT_DIR" submodule status -- deps/libptytest 2>&1)" || {
        echo "libptytest: cannot read the submodule state: $status" >&2
        echo "  This is not a git checkout, or deps/libptytest is not in .gitmodules." >&2
        echo "  Put libptytest at deps/libptytest, or use WORKSPACE_LIBS=system." >&2
        return 1
    }
    case "$status" in
        ''|[-+U]*)
            echo "libptytest: initialising the submodule deps/libptytest"
            git -C "$SCRIPT_DIR" submodule update --init --recursive -- deps/libptytest || {
                echo "libptytest: git submodule update failed (no network? no .gitmodules entry?)." >&2
                echo "  Use WORKSPACE_LIBS=system with libptytest-dev installed instead." >&2
                return 1
            }
            ;;
    esac
    tag="$(git -C "$sub" describe --tags --exact-match HEAD)" || {
        echo "libptytest: the commit deps/libptytest is pinned to is not a release tag." >&2
        echo "  Pin it to a vX.Y.Z tag of libptytest (policies/dependencies-model.md)." >&2
        return 1
    }
    case "$tag" in
        v[0-9]*.[0-9]*.[0-9]*) ;;
        *) echo "libptytest: '$tag' is not a release tag (vX.Y.Z)." >&2; return 1 ;;
    esac
}

install_deps() {
    local apt_deps="$APT_DEPS" dnf_deps="$DNF_DEPS"
    if [ "$(libs_mode)" = system ]; then
        apt_deps="$apt_deps libptytest-dev"
        dnf_deps="$dnf_deps libptytest-devel"
    fi
    if command -v apt-get >/dev/null; then
        # shellcheck disable=SC2086
        apt-get update && apt-get install -y $apt_deps
    elif command -v dnf >/dev/null; then
        # shellcheck disable=SC2086
        dnf install -y $dnf_deps
    else
        echo "deps: no apt-get or dnf; install by hand:" >&2
        echo "  a C11 compiler, cmake >= 3.20, pkg-config, git, valgrind, lcov," >&2
        echo "  lintian, dpkg-deb, rpmbuild, groff" >&2
        return 1
    fi
    # The submodule needs git and network, not root: it is part of deps too.
    if [ "$(libs_mode)" = bundled ]; then
        ensure_libs
    fi
}

# ---------------------------------------------------------------------------
# Builds
#
# -DENABLE_COVERAGE is passed explicitly on every configure: coverage
# instrumentation is sticky in a CMake cache and would otherwise leak into
# release binaries and packages (policies/testing-model.md).
# ---------------------------------------------------------------------------

build_release() {
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release \
          -DCMAKE_INSTALL_PREFIX=/usr \
          -DENABLE_COVERAGE=OFF -DTUIKIT_ENABLE_ASAN=OFF \
          -DTUIKIT_BUILD_TESTS=OFF -DTUIKIT_TEST_WRAPPER=
    cmake --build "$BUILD_DIR" --parallel "$JOBS"
    echo "Release build done: $BUILD_DIR"
}

# build_debug_into <dir> [<test bin root>]
# Each step is chained: a failure is the function's status, also where a
# caller has switched "set -e" off (an "if" or "||" list).
build_debug_into() {
    local dir="$1" bin_root="${2:-$1/tests}"
    ensure_libs &&
    cmake -S "$SCRIPT_DIR" -B "$dir" -DCMAKE_BUILD_TYPE=Debug \
          -DENABLE_COVERAGE=OFF -DTUIKIT_ENABLE_ASAN=ON \
          -DTUIKIT_BUILD_TESTS=ON -DTUIKIT_TEST_WRAPPER= \
          "-DWORKSPACE_LIBS=$(libs_mode)" \
          "-DTUIKIT_TEST_BIN_ROOT=$bin_root" &&
    cmake --build "$dir" --parallel "$JOBS"
}

build_debug() {
    build_debug_into "$BUILD_DIR_DEBUG"
    echo "Debug build done: $BUILD_DIR_DEBUG"
}

# ---------------------------------------------------------------------------
# The result file (policies/manage-sh-model.md, "The integrator reads the
# output").  test, functional, pty, integration, valgrind and coverage write
# build/result/<verb>.result: deleted at the start, "running" while the
# suites run, final on every exit path (res_end, and the EXIT trap of
# "timed" for a path that did not get there).
# ---------------------------------------------------------------------------

RES_DIR="$BUILD_DIR/result"
RES_VERB=""
RES_START=0
RES_STARTED=""
RES_SCOPES=()
RES_SUITES=()

# short_commit: the short id of HEAD, with + after it for a dirty tree.
short_commit() {
    local commit dirty=""
    commit="$(git -C "$SCRIPT_DIR" rev-parse --short HEAD)" || commit=unknown
    if [ "$commit" != unknown ] &&
       [ -n "$(git -C "$SCRIPT_DIR" status --porcelain)" ]; then
        dirty="+"
    fi
    printf '%s%s' "$commit" "$dirty"
}

res_begin() {
    RES_VERB="$1"
    RES_START="$(date +%s)"
    RES_STARTED="$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    RES_SCOPES=()
    RES_SUITES=()
    mkdir -p "$RES_DIR"
    rm -f "$RES_DIR/$RES_VERB.result"
    res_write running 0
}

# res_write <status> <exit>: the file as it is now, replaced in one step.
res_write() {
    local status="$1" code="$2" line a=0 f=0 n=0 file="$RES_DIR/$RES_VERB.result"
    for line in "${RES_SUITES[@]}"; do
        set -- $line
        n=$((n + 1)); a=$((a + $3)); f=$((f + $4))
    done
    {
        echo "format: manage-result/1"
        echo "verb: $RES_VERB"
        echo "commit: $(short_commit)"
        echo "started: $RES_STARTED"
        echo "seconds: $(( $(date +%s) - RES_START ))"
        echo "exit: $code"
        echo "status: $status"
        echo "suites: $n"
        echo "assertions: $a"
        echo "failed: $f"
        for line in "${RES_SCOPES[@]}"; do echo "scope: $line"; done
        for line in "${RES_SUITES[@]}"; do echo "suite: $line"; done
    } > "$file.new"
    mv "$file.new" "$file"
}

# res_end <exit>: the final file.  pass = exit 0; fail = suites ran and the
# verb failed (a suite, or a threshold); error = the verb could not run them.
res_end() {
    local code="$1" status=pass line
    [ -n "$RES_VERB" ] || return 0
    if [ "$code" -ne 0 ]; then
        status=error
        [ "${#RES_SUITES[@]}" -eq 0 ] || status=fail
    fi
    res_write "$status" "$code"
    RES_VERB=""
}

# parse_counts <log> <exit>: P_A assertions and P_F failed, as the suite's
# own runner counted.  The runners differ (libptytest's "Assertions run:",
# the checking scripts' "checked: N, failed: M" or "N checks, M failed");
# a suite that prints none counts as one assertion, its own exit status.
parse_counts() {
    local log="$1" code="$2" s
    P_A=""; P_F=""
    if grep -q 'Assertions run: *[0-9]' "$log"; then
        P_A="$(sed -n 's/.*Assertions run: *\([0-9][0-9]*\).*/\1/p' "$log" | awk '{ s += $1 } END { print s }')"
        P_F="$(sed -n 's/.*Assertions failed: *\([0-9][0-9]*\).*/\1/p' "$log" | awk '{ s += $1 } END { print s + 0 }')"
    elif s="$(grep -Eo 'checked: [0-9]+, failed: [0-9]+' "$log" | tail -n 1)" && [ -n "$s" ]; then
        P_A="$(sed 's/checked: \([0-9]*\),.*/\1/' <<< "$s")"
        P_F="$(sed 's/.*failed: //' <<< "$s")"
    elif s="$(grep -Eo '[0-9]+ checks, [0-9]+ failed' "$log" | tail -n 1)" && [ -n "$s" ]; then
        P_A="$(sed 's/ checks.*//' <<< "$s")"
        P_F="$(sed 's/.*checks, \([0-9]*\) failed/\1/' <<< "$s")"
    elif s="$(grep -Eo '[0-9]+ checks over' "$log" | tail -n 1)" && [ -n "$s" ]; then
        P_A="${s%% *}"; P_F=0
    else
        P_A=1; P_F=0
        [ "$code" -eq 0 ] || P_F=1
    fi
}

# run_suites <prefix> <build dir> <label regex> [ctest args]
# Runs every suite of the label, one ctest call each, and records it in the
# result.  A failed suite does not stop the others; the status is non-zero
# when any failed.  The suite's own output is shown.
run_suites() {
    local prefix="$1" dir="$2" label="$3" names name log t0 st rc=0 crc
    shift 3
    names="$(ctest --test-dir "$dir" -N -L "$label" "$@" | sed -n 's/^ *Test *#[0-9]*: //p')" || return 1
    if [ -z "$names" ]; then
        echo "no tests found for label $label in $dir" >&2
        return 1
    fi
    mkdir -p "$BUILD_DIR/_logs"
    for name in $names; do
        log="$BUILD_DIR/_logs/suite-$prefix${name}.log"
        log="${log//\/\//\/}"
        mkdir -p "$(dirname "$log")"
        t0="$(date +%s)"
        crc=0
        ctest --test-dir "$dir" -R "^${name//./\\.}\$" -V --no-tests=error 2>&1 | tee "$log" || crc=$?
        parse_counts "$log" "$crc"
        if [ "$crc" -eq 0 ] && [ "$P_F" -eq 0 ]; then
            st=pass
        else
            rc=1
            if grep -q '\*\*\*Timeout' "$log"; then st=timeout; P_A=0; P_F=0
            elif grep -Eq 'SEGFAULT|Subprocess aborted|\*\*\*Exception' "$log"; then st=crash; P_A=0; P_F=0
            else
                st=fail
                [ "$P_F" -gt 0 ] || P_F=1
            fi
        fi
        RES_SUITES+=("$prefix${name#*.} $st $P_A $P_F $(( $(date +%s) - t0 ))")
    done
    return "$rc"
}

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

# run_label <verb> <label> [ctest args...]
run_label() {
    local verb="$1" label="$2" rc=0
    shift 2
    res_begin "$verb"
    # The test tree builds the tests where the integrator runs them from:
    # build/tests/<category>/bin/<name>.
    if ! build_debug_into "$BUILD_DIR_TEST" "$BUILD_DIR/tests"; then
        res_end 1
        return 1
    fi
    # Evidence of an earlier run would be taken for this run's.
    if [ -d "$SCRIPT_DIR/build/_test/evidence" ]; then
        ( cd "$SCRIPT_DIR/build/_test" && rm -rf evidence )
    fi
    # halt_on_error: a sanitiser finding fails the suite instead of being
    # printed and forgotten.  A failed screen assertion leaves its evidence
    # (report, screen, screen with colours) under the build tree.
    PTYTEST_EVIDENCE_DIR="$BUILD_DIR_TEST/evidence" \
    ASAN_OPTIONS="halt_on_error=1:detect_leaks=1" \
    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
        run_suites "" "$BUILD_DIR_TEST" "^${label}\$" "$@" || rc=1
    if [ -d "$BUILD_DIR_TEST/evidence" ]; then
        echo "Evidence of failed assertions: $BUILD_DIR_TEST/evidence"
    fi
    res_end "$rc"
    return "$rc"
}

run_unit() { run_label test unit "$@"; }
run_pty()  { run_label pty pty "$@"; }
run_functional() { run_label functional functional "$@"; }

# valgrind runs every unit suite (and only those: pty and functional have
# verbs of their own) on a clean Release build.
run_valgrind() {
    local rc=0
    res_begin valgrind
    if ! command -v valgrind >/dev/null; then
        echo "valgrind is not installed; install it with: ./manage.sh deps" >&2
        res_end 1
        return 1
    fi
    # A clean Release build: sanitiser and coverage instrumentation both
    # interfere with Valgrind's own.  Children of the tests (the fixture,
    # cat, sh) are not traced: the library is what is under examination.
    if ! { ensure_libs &&
           cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR_VALGRIND" -DCMAKE_BUILD_TYPE=Release \
              -DENABLE_COVERAGE=OFF -DTUIKIT_ENABLE_ASAN=OFF -DTUIKIT_BUILD_TESTS=ON \
              "-DWORKSPACE_LIBS=$(libs_mode)" \
              "-DTUIKIT_TEST_WRAPPER=valgrind;--error-exitcode=1;--leak-check=full;--errors-for-leak-kinds=definite;--track-fds=no;--quiet" &&
           cmake --build "$BUILD_DIR_VALGRIND" --parallel "$JOBS"; }; then
        res_end 1
        return 1
    fi
    # Valgrind does not emulate SIGTSTP stops (measured); the one test that
    # needs a real stop says so and is checked by the other runs.
    TUIKIT_TEST_UNDER_VALGRIND=1 run_suites "" "$BUILD_DIR_VALGRIND" '^unit$' || rc=1
    res_end "$rc"
    return "$rc"
}

# cov_build <dir>: an instrumented build tree of its own.
cov_build() {
    cmake -S "$SCRIPT_DIR" -B "$1" -DCMAKE_BUILD_TYPE=Debug \
          -DENABLE_COVERAGE=ON -DTUIKIT_ENABLE_ASAN=OFF -DTUIKIT_BUILD_TESTS=ON \
          "-DWORKSPACE_LIBS=$(libs_mode)" \
          -DTUIKIT_TEST_WRAPPER= &&
    cmake --build "$1" --parallel "$JOBS"
}

cov_pct() {
    lcov --summary "$1" | awk '/lines\.*:/ { sub("%", "", $2); print $2 }'
}

# cov_lines <tracefile>: "<covered> <total>" lines.
cov_lines() {
    lcov --summary "$1" | sed -n 's/.*(\([0-9][0-9]*\) of \([0-9][0-9]*\) lines).*/\1 \2/p'
}

# cov_scope <scope> <dir> <label regex>
# The counters of <dir> are zeroed, the suites of the scope are run (their
# verdict counts: a failing suite fails the run, the others still run), the
# counters are captured and cut to the product code (src/), and listed per
# file.  The tracefile is <dir>/<scope>.info, the figure is in $COV_PCT.
# The counters themselves stay in <dir>, for the integrator's own gcov.
cov_scope() {
    local scope="$1" dir="$2" label="$3" info rc=0 lines
    info="$dir/$scope.info"
    lcov --zerocounters --directory "$dir" || return 1
    run_suites "$scope/" "$dir" "$label" || rc=1
    lcov --capture --directory "$dir" --output-file "$info.all" &&
    lcov --extract "$info.all" "$SCRIPT_DIR/src/*" --output-file "$info" || return 1
    echo "== per-file line coverage: $scope =="
    lcov --list "$info" || return 1
    COV_PCT="$(cov_pct "$info")"
    lines="$(cov_lines "$info")"
    if [ -z "$COV_PCT" ] || [ -z "$lines" ]; then
        echo "coverage: could not read the line rate of $scope from lcov --summary" >&2
        return 1
    fi
    RES_SCOPES+=("$scope build/coverage/$scope $lines")
    return "$rc"
}

# cov_require <name> <pct> <threshold>
cov_require() {
    echo "Line coverage of src/, $1: $2% (threshold $3%)"
    if ! awk -v p="$2" -v t="$3" 'BEGIN { exit !(p >= t) }'; then
        echo "coverage: $1 is $2%, below the threshold of $3%" >&2
        return 1
    fi
}

# Three figures (policies/testing-model.md, Coverage): unit tests alone,
# the externally driven suites (functional, PTY and integration, through
# the library's public interface) together, and every suite together
# against the project's own, higher threshold.  The two scopes are two
# instrumented build trees, build/coverage/unit and build/coverage/external,
# each holding the counters of its own suites only.
run_coverage() {
    local unit_pct ext_pct all_pct rc=0 cov="$BUILD_DIR/coverage"
    res_begin coverage
    if ! command -v lcov >/dev/null; then
        echo "lcov is not installed; install it with: ./manage.sh deps" >&2
        res_end 1
        return 1
    fi
    if ! { ensure_libs && cov_build "$cov/unit" && cov_build "$cov/external"; }; then
        res_end 1
        return 1
    fi
    cov_scope unit "$cov/unit" '^unit$' || rc=1
    unit_pct="$COV_PCT"
    cov_scope external "$cov/external" '^(functional|pty)$' || rc=1
    ext_pct="$COV_PCT"
    if [ "$rc" -ne 0 ]; then
        res_end 1
        return 1
    fi
    lcov --add-tracefile "$cov/unit/unit.info" --add-tracefile "$cov/external/external.info" \
         --output-file "$cov/coverage.info" || { res_end 1; return 1; }
    echo "== per-file line coverage: all suites together =="
    lcov --list "$cov/coverage.info" || { res_end 1; return 1; }
    all_pct="$(cov_pct "$cov/coverage.info")"
    if [ -z "$all_pct" ]; then
        echo "coverage: could not read a line rate from lcov --summary" >&2
        res_end 1
        return 1
    fi
    if command -v genhtml >/dev/null; then
        genhtml --quiet "$cov/coverage.info" --output-directory "$cov/html" || rc=1
        echo "HTML report: $cov/html/index.html"
    fi
    cov_require "unit tests alone" "$unit_pct" "$COVERAGE_UNIT_THRESHOLD" || rc=1
    cov_require "externally driven (functional + pty)" "$ext_pct" "$COVERAGE_EXTERNAL_THRESHOLD" || rc=1
    cov_require "all suites together" "$all_pct" "$COVERAGE_THRESHOLD" || rc=1
    echo "Line coverage of src/: ${all_pct}% (threshold ${COVERAGE_THRESHOLD}%)"
    res_end "$rc"
    return "$rc"
}

# ---------------------------------------------------------------------------
# Packaging
#
# CPack builds the packages from the metadata in CMakeLists.txt.  Nothing
# about a package is decided here: no version, no dependency list, no file
# layout.  See policies/packaging-and-release-model.md.
# ---------------------------------------------------------------------------

do_package() {
    local kind="${1:-deb}" generators
    case "$kind" in
        deb) generators="DEB" ;;
        rpm) generators="RPM" ;;
        all) generators="DEB RPM" ;;
        *)
            echo "package: expected deb, rpm or all, got '$kind'" >&2
            return 1
            ;;
    esac

    # Emptied first: after a run the directory holds exactly that run's
    # packages, and after a failed run nothing.
    empty_pkg_dir
    build_release

    local g
    for g in $generators; do
        # umask 022 here, not left to the caller: CPack copies the umask of
        # whoever runs it into the directory modes of the package.  See
        # policies/packaging-and-release-model.md, "The umask leak".
        if ! ( umask 022; cd "$BUILD_DIR" && cpack -G "$g" --config CPackConfig.cmake ); then
            echo "package: cpack -G $g failed; $PKG_DIR left empty" >&2
            empty_pkg_dir
            return 1
        fi
    done

    # A package that is not lintian-clean is not a result.
    if [ "$generators" != "RPM" ] && ! run_lintian; then
        echo "package: lintian refused the packages; $PKG_DIR left empty" >&2
        empty_pkg_dir
        return 1
    fi

    echo
    echo "Packages in $PKG_DIR:"
    find "$PKG_DIR" -mindepth 1 | sort
}

# tag-build VERSION: makes the annotated tag vVERSION (VERSION is the exact
# Debian version of the package, e.g. 0.14.0.139) on HEAD, locally.  It never
# pushes: the agent pushes it from the host with gh-push after the package
# build (policies/manage-sh-model.md).  package never tags.  The tag follows
# the version and never decides it (policies/packaging-and-release-model.md).
# Recreating the same tag at the same commit is fine; the same tag on another
# commit is refused.
do_tag_build() {
    local version="${1:-}" tag prev msg
    version="${version#v}"
    if [ -z "$version" ]; then
        echo "tag-build: VERSION missing (./manage.sh tag-build VERSION)" >&2
        return 2
    fi
    [ -e "$SCRIPT_DIR/.git" ] || { echo "tag-build: not a git checkout" >&2; return 1; }
    tag="v$version"
    if git -C "$SCRIPT_DIR" rev-parse -q --verify "refs/tags/$tag" > /dev/null; then
        if [ "$(git -C "$SCRIPT_DIR" rev-parse "$tag^{commit}")" != "$(git -C "$SCRIPT_DIR" rev-parse HEAD)" ]; then
            echo "tag-build: $tag exists on another commit" >&2
            return 1
        fi
    else
        prev="$(git -C "$SCRIPT_DIR" tag --list 'v*' --sort=-v:refname --merged HEAD | head -n 1)"
        msg="libtuikit $version"$'\n'"Tickets: $(git -C "$SCRIPT_DIR" log ${prev:+"$prev..HEAD"} --format=%B |
            { grep -oE '\bTUI-[0-9]+\b' || [ $? -eq 1 ]; } | sort -u -t- -k2,2n | paste -sd' ')"
        # The tagger is the author of the built commit when no identity is
        # configured (a container, a CI worker).
        GIT_COMMITTER_NAME="$(git -C "$SCRIPT_DIR" log -1 --format=%cn)" \
        GIT_COMMITTER_EMAIL="$(git -C "$SCRIPT_DIR" log -1 --format=%ce)" \
            git -C "$SCRIPT_DIR" tag -a "$tag" -m "$msg" || return 1
    fi
    echo "tag-build: $tag (local, not pushed)"
}

# Removes build/packages/ with everything in it and creates it again.
empty_pkg_dir() {
    [ -n "$BUILD_DIR" ] && [ "$PKG_DIR" = "$BUILD_DIR/packages" ] || {
        echo "package: unexpected package directory: '$PKG_DIR'" >&2
        return 1
    }
    mkdir -p "$BUILD_DIR"
    ( cd "$BUILD_DIR" && rm -rf packages )
    mkdir "$PKG_DIR"
}

run_lintian() {
    local deb rc=0 found=0
    for deb in "$PKG_DIR"/*.deb; do
        [ -f "$deb" ] || continue
        found=1
        echo "--- $(basename "$deb")"
        # The control fields must parse before anything else is worth checking.
        dpkg-deb -f "$deb" Package Version Depends XB-Git-Commit XB-Build-Date
        lintian --fail-on error,warning --info "$deb" || rc=1
        # The runtime package's shlibs must ask for "at least" this version:
        # "=" pins every dependant, and apt removes them on an upgrade.
        case "$(dpkg-deb -f "$deb" Package)" in
        libtuikit[0-9]*)
            local shlibs
            shlibs="$(dpkg-deb -I "$deb" shlibs)"
            echo "shlibs: $shlibs"
            if ! grep -Eqx 'libtuikit [0-9]+ libtuikit[0-9]+ \(>= [0-9.]+\)' <<< "$shlibs"; then
                echo "lintian: $(basename "$deb"): shlibs must be 'libtuikit N libtuikitN (>= VERSION)'" >&2
                rc=1
            fi
            ;;
        esac
    done
    if [ "$found" -eq 0 ]; then
        echo "lintian: no .deb in $PKG_DIR" >&2
        return 1
    fi
    return "$rc"
}

# consumer -- the example, built and run against the content of the .deb
# packages: headers, pkg-config file, CMake package and shared library as a
# consuming project gets them.  The example's PTY test needs libptytest,
# which is a test dependency: the installed package (WORKSPACE_LIBS=system),
# or, in the bundled way, the submodule built and installed into a prefix of
# its own under the consumer tree, so that the example sees it as a package.
run_consumer() {
    local root="$BUILD_DIR_CONSUMER/root" deb found=0
    local ptyroot="$BUILD_DIR_CONSUMER/ptytest-root"
    # A fresh tree, so nothing from an earlier package can be picked up.
    if [ -d "$SCRIPT_DIR/build/_consumer" ]; then
        ( cd "$SCRIPT_DIR/build" && rm -rf _consumer )
    fi
    mkdir -p "$root"
    for deb in "$PKG_DIR"/*.deb; do
        [ -f "$deb" ] || continue
        found=1
        dpkg-deb -x "$deb" "$root"
    done
    if [ "$found" -eq 0 ]; then
        echo "consumer: no .deb in $PKG_DIR; run ./manage.sh package first" >&2
        return 1
    fi

    local libdir
    libdir="$(dirname "$(find "$root" -name 'libtuikit.so.*' -type f | head -n 1)")"
    local pc_dir="$libdir/pkgconfig"

    # libptytest: pt_cflags, pt_libs, pt_ld (a run-time path) and pt_prefix
    # (for find_package), from the system or from the submodule.
    local pt_cflags pt_libs pt_ld="" pt_prefix=""
    ensure_libs
    if [ "$(libs_mode)" = bundled ]; then
        echo "=== libptytest from the submodule ==="
        cmake -S "$SCRIPT_DIR/deps/libptytest" -B "$BUILD_DIR_CONSUMER/ptytest" \
              -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
              -DPTYTEST_BUILD_TESTS=OFF -DENABLE_COVERAGE=OFF
        cmake --build "$BUILD_DIR_CONSUMER/ptytest" --parallel "$JOBS"
        DESTDIR="$ptyroot" cmake --install "$BUILD_DIR_CONSUMER/ptytest"
        local pty_pc pty_lib
        pty_pc="$(dirname "$(find "$ptyroot" -name libptytest.pc | head -n 1)")"
        pty_lib="$(dirname "$(find "$ptyroot" -name 'libptytest.so.*' -type f | head -n 1)")"
        pt_cflags="$(PKG_CONFIG_SYSROOT_DIR="$ptyroot" PKG_CONFIG_LIBDIR="$pty_pc" \
                     pkg-config --cflags libptytest)"
        pt_libs="$(PKG_CONFIG_SYSROOT_DIR="$ptyroot" PKG_CONFIG_LIBDIR="$pty_pc" \
                   pkg-config --libs libptytest)"
        pt_ld=":$pty_lib"
        pt_prefix=";$ptyroot/usr"
    else
        pt_cflags="$(pkg-config --cflags libptytest)"
        pt_libs="$(pkg-config --libs libptytest)"
    fi

    echo "=== pkg-config ==="
    local cflags libs
    cflags="$(PKG_CONFIG_SYSROOT_DIR="$root" PKG_CONFIG_LIBDIR="$pc_dir" \
              pkg-config --cflags libtuikit)"
    libs="$(PKG_CONFIG_SYSROOT_DIR="$root" PKG_CONFIG_LIBDIR="$pc_dir" \
            pkg-config --libs libtuikit)"
    echo "cflags: $cflags"
    echo "libs:   $libs"
    # shellcheck disable=SC2086
    cc -std=c11 -Wall -Wextra -Werror $cflags -o "$BUILD_DIR_CONSUMER/tuikit-demo" \
       "$SCRIPT_DIR/examples/tuikit_demo.c" $libs
    # shellcheck disable=SC2086
    cc -std=c11 -Wall -Wextra -Werror $pt_cflags \
       -o "$BUILD_DIR_CONSUMER/test-tuikit-demo" \
       "$SCRIPT_DIR/examples/test_tuikit_demo.c" $pt_libs
    LD_LIBRARY_PATH="$libdir$pt_ld" timeout 60 \
        "$BUILD_DIR_CONSUMER/test-tuikit-demo" "$BUILD_DIR_CONSUMER/tuikit-demo"

    echo "=== CMake find_package ==="
    cmake -S "$SCRIPT_DIR/examples/consumer" -B "$BUILD_DIR_CONSUMER/cmake" \
          "-DCMAKE_PREFIX_PATH=$root/usr$pt_prefix"
    cmake --build "$BUILD_DIR_CONSUMER/cmake" --parallel "$JOBS"
    LD_LIBRARY_PATH="$libdir$pt_ld" timeout 60 \
        "$BUILD_DIR_CONSUMER/cmake/test-tuikit-demo" "$BUILD_DIR_CONSUMER/cmake/tuikit-demo"
}

# ---------------------------------------------------------------------------
# check -- every gate, each with its log, and a summary.
# ---------------------------------------------------------------------------

run_check() {
    mkdir -p "$BUILD_DIR"
    local logdir="$BUILD_DIR/_logs"
    mkdir -p "$logdir"

    local gates="unit pty functional valgrind coverage package lintian consumer"
    local -A result
    local g rc failed=0
    for g in $gates; do
        printf '%-10s ' "$g"
        rc=0
        case "$g" in
            unit)     run_unit       > "$logdir/$g.log" 2>&1 || rc=$? ;;
            pty)      run_pty        > "$logdir/$g.log" 2>&1 || rc=$? ;;
            functional) run_functional > "$logdir/$g.log" 2>&1 || rc=$? ;;
            valgrind) run_valgrind   > "$logdir/$g.log" 2>&1 || rc=$? ;;
            coverage) run_coverage   > "$logdir/$g.log" 2>&1 || rc=$? ;;
            package)  do_package all > "$logdir/$g.log" 2>&1 || rc=$? ;;
            lintian)  run_lintian    > "$logdir/$g.log" 2>&1 || rc=$? ;;
            consumer) run_consumer   > "$logdir/$g.log" 2>&1 || rc=$? ;;
        esac
        if [ "$rc" -eq 0 ]; then
            result[$g]=PASS
            echo "PASS"
        else
            result[$g]="FAIL (exit $rc, see $logdir/$g.log)"
            echo "FAIL -- last lines of $logdir/$g.log:"
            tail -n 25 "$logdir/$g.log" | sed 's/^/    /'
            failed=$((failed + 1))
        fi
    done

    echo
    echo "=== summary ==="
    for g in $gates; do printf '%-10s %s\n' "$g" "${result[$g]}"; done
    local rf
    for rf in "$RES_DIR"/*.result; do
        [ -f "$rf" ] || continue
        printf '%-12s %s assertions, %s failed\n' "$(sed -n 's/^verb: //p' "$rf")" \
               "$(sed -n 's/^assertions: //p' "$rf")" "$(sed -n 's/^failed: //p' "$rf")"
    done
    grep -h '^Line coverage' "$logdir/coverage.log" || echo "  (no coverage figure found in the log)"
    if [ "$failed" -ne 0 ]; then
        echo "$failed gate(s) failed."
        return 1
    fi
    echo "All gates passed."
}

# ---------------------------------------------------------------------------
# Timing -- every verb that does work appends one line to build/timing.log:
#   <UTC ISO time> <verb> <whole seconds>s <ok|fail> <commit>[+]
# (+ = uncommitted changes).  timing KEY keeps in build/timing.posted the
# record it last posted per key, and posts only a different record.
# The script measures, never the person (policies/manage-sh-model.md).
# ---------------------------------------------------------------------------

TIMING_LOG="$BUILD_DIR/timing.log"
TIMING_POSTED="$BUILD_DIR/timing.posted"
TIMING_VERB=""
TIMING_START=0

# The record is written by an EXIT trap, not by a wrapper around the verb:
# a function called in an "||" list runs without "set -e", and the verbs
# rely on it.  The trap sees the exit status of the verb, ok or not.
timing_record() {
    local rc=$? secs state=ok commit
    trap - EXIT
    # A result file still "running" is made final: a path that did not end
    # the verb itself (an interrupted run) leaves no stale answer.
    if [ -n "$RES_VERB" ]; then res_end "$rc"; fi
    secs=$(( $(date +%s) - TIMING_START ))
    [ "$rc" -eq 0 ] || state=fail
    commit="$(short_commit)"
    mkdir -p "$BUILD_DIR"
    printf '%s %s %ss %s %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" \
           "$TIMING_VERB" "$secs" "$state" "$commit" >> "$TIMING_LOG"
    exit "$rc"
}

# timed <verb>: from here to the end of the script, the run is timed.
timed() {
    TIMING_VERB="$1"
    TIMING_START="$(date +%s)"
    trap timing_record EXIT
    trap 'exit 130' INT
    trap 'exit 143' TERM
}

# timing_line: the gate line from the latest successful run of each verb of
# the gate (check, package, install); a part that did not run through this
# script is left out.  Prints "Gate: not measured" and fails without any.
timing_line() {
    local line=""
    if [ -s "$TIMING_LOG" ]; then
        line="$(awk '
            $4 == "ok" { v = $2; s = $3; sub("s$", "", s); last[v] = s }
            END {
                n = split("check package install", order, " ")
                total = 0; parts = ""; found = 0
                for (i = 1; i <= n; i++) {
                    v = order[i]
                    if (v in last) {
                        total += last[v]; found = 1
                        parts = parts (parts == "" ? "" : ", ") v " " last[v] "s"
                    }
                }
                if (found) printf "Gate: %ds (%s)\n", total, parts
            }' "$TIMING_LOG")"
    fi
    if [ -z "$line" ]; then
        echo "Gate: not measured"
        return 1
    fi
    echo "$line"
}

# timing_used: the log lines the gate line is made from (the latest ok line
# of each verb of the gate), joined with ";" -- the measured record.
timing_used() {
    awk '
        $4 == "ok" { last[$2] = $0 }
        END {
            n = split("check package install", order, " ")
            for (i = 1; i <= n; i++)
                if (order[i] in last) {
                    printf "%s%s", (out ? ";" : ""), last[order[i]]
                    out = 1
                }
        }' "$TIMING_LOG"
}

do_timing() {
    local key="${1:-}" line rc=0 record previous=""
    line="$(timing_line)" || rc=$?
    echo "$line"
    [ "$rc" -eq 0 ] || return "$rc"
    if [ -n "$key" ]; then
        record="$(timing_used)"
        if [ -f "$TIMING_POSTED" ]; then
            previous="$(awk -v k="$key" -F'\t' '$1 == k { r = $2 } END { print r }' "$TIMING_POSTED")"
        fi
        if [ "$record" = "$previous" ]; then
            echo "timing: this record is already posted on $key; nothing posted."
            return 0
        fi
        issue-cli issue comment "$key" "$line"
        : > "$TIMING_POSTED.new"
        if [ -f "$TIMING_POSTED" ]; then
            awk -v k="$key" -F'\t' '$1 != k' "$TIMING_POSTED" > "$TIMING_POSTED.new"
        fi
        printf '%s\t%s\n' "$key" "$record" >> "$TIMING_POSTED.new"
        mv "$TIMING_POSTED.new" "$TIMING_POSTED"
    fi
}

clean() {
    # Enter the directory, then delete a literal name: a recursive delete
    # never takes a bare variable.  The guard matters: "cd ''" succeeds and
    # changes nothing.
    [ -n "$SCRIPT_DIR" ] && [ -d "$SCRIPT_DIR" ] || {
        echo "clean: SCRIPT_DIR is not a directory: '$SCRIPT_DIR'" >&2
        return 1
    }
    ( cd "$SCRIPT_DIR" && rm -rf build )
    echo "Cleaned."
}

cmd="${1:-help}"
[ $# -gt 0 ] && shift
case "$cmd" in
    deps)      install_deps ;;
    build)     timed build; build_release ;;
    debug)     timed debug; build_debug ;;
    test)      timed test; run_unit "$@" ;;
    pty)       timed pty; run_pty "$@" ;;
    functional) timed functional; run_functional "$@" ;;
    coverage)  timed coverage; run_coverage ;;
    valgrind)  timed valgrind; run_valgrind ;;
    consumer)  timed consumer; run_consumer ;;
    check)     timed check; run_check ;;
    package)   timed package; do_package "${1:-deb}" ;;
    tag-build) do_tag_build "$@" ;;
    timing)    do_timing "$@" ;;
    clean)     clean ;;
    help|-h|--help) usage ;;
    *)
        echo "unknown command: $cmd" >&2
        usage >&2
        exit 2
        ;;
esac
