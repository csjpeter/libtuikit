#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary
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
BUILD_DIR_COVERAGE="$BUILD_DIR/_coverage"
BUILD_DIR_VALGRIND="$BUILD_DIR/_valgrind"
BUILD_DIR_CONSUMER="$BUILD_DIR/_consumer"

# Line coverage of the library sources below this fails the run: every
# line of src/ is run by a test (failure paths through tests/common/faults.c).
COVERAGE_THRESHOLD=100

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
  coverage              Line coverage of the library; fails below the threshold
  valgrind              Unit and PTY tests under Valgrind on a clean Release build
  consumer              Build and run the example against the built .deb packages
  check                 Every gate, with a summary to compare against CI
  package [deb|rpm|all] Build the packages with CPack into build/packages/ (libtuikit0,
                        libtuikit-dev), lintian-checked; empty on failure
  clean                 Remove build/
  help                  This message

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
build_debug_into() {
    local dir="$1" bin_root="${2:-$1/tests}"
    ensure_libs
    cmake -S "$SCRIPT_DIR" -B "$dir" -DCMAKE_BUILD_TYPE=Debug \
          -DENABLE_COVERAGE=OFF -DTUIKIT_ENABLE_ASAN=ON \
          -DTUIKIT_BUILD_TESTS=ON -DTUIKIT_TEST_WRAPPER= \
          "-DWORKSPACE_LIBS=$(libs_mode)" \
          "-DTUIKIT_TEST_BIN_ROOT=$bin_root"
    cmake --build "$dir" --parallel "$JOBS"
}

build_debug() {
    build_debug_into "$BUILD_DIR_DEBUG"
    echo "Debug build done: $BUILD_DIR_DEBUG"
}

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------

# run_label <label> [ctest args...]
run_label() {
    local label="$1"
    shift
    # The test tree builds the tests where the integrator runs them from:
    # build/tests/<category>/bin/<name>.
    build_debug_into "$BUILD_DIR_TEST" "$BUILD_DIR/tests"
    # Evidence of an earlier run would be taken for this run's.
    if [ -d "$SCRIPT_DIR/build/_test/evidence" ]; then
        ( cd "$SCRIPT_DIR/build/_test" && rm -rf evidence )
    fi
    # halt_on_error: a sanitiser finding fails the suite instead of being
    # printed and forgotten.  A failed screen assertion leaves its evidence
    # (report, screen, screen with colours) under the build tree.  The
    # status is kept, not discarded, and returned after the hint.
    local rc=0
    PTYTEST_EVIDENCE_DIR="$BUILD_DIR_TEST/evidence" \
    ASAN_OPTIONS="halt_on_error=1:detect_leaks=1" \
    UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1" \
        ctest --test-dir "$BUILD_DIR_TEST" -L "^${label}\$" \
              --output-on-failure --no-tests=error "$@" || rc=$?
    if [ -d "$BUILD_DIR_TEST/evidence" ]; then
        echo "Evidence of failed assertions: $BUILD_DIR_TEST/evidence"
    fi
    return "$rc"
}

run_unit() { run_label unit "$@"; }
run_pty()  { run_label pty "$@"; }
run_functional() { run_label functional "$@"; }

run_valgrind() {
    if ! command -v valgrind >/dev/null; then
        echo "valgrind is not installed; install it with: ./manage.sh deps" >&2
        return 1
    fi
    # A clean Release build: sanitiser and coverage instrumentation both
    # interfere with Valgrind's own.  Children of the tests (the fixture,
    # cat, sh) are not traced: the library is what is under examination.
    ensure_libs
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR_VALGRIND" -DCMAKE_BUILD_TYPE=Release \
          -DENABLE_COVERAGE=OFF -DTUIKIT_ENABLE_ASAN=OFF -DTUIKIT_BUILD_TESTS=ON \
          "-DWORKSPACE_LIBS=$(libs_mode)" \
          "-DTUIKIT_TEST_WRAPPER=valgrind;--error-exitcode=1;--leak-check=full;--errors-for-leak-kinds=definite;--track-fds=no;--quiet"
    cmake --build "$BUILD_DIR_VALGRIND" --parallel "$JOBS"
    # Valgrind does not emulate SIGTSTP stops (measured); the one test that
    # needs a real stop says so and is checked by the other runs.
    TUIKIT_TEST_UNDER_VALGRIND=1 \
        ctest --test-dir "$BUILD_DIR_VALGRIND" -L "^(unit|pty)$" --output-on-failure --no-tests=error
}

run_coverage() {
    if ! command -v lcov >/dev/null; then
        echo "lcov is not installed; install it with: ./manage.sh deps" >&2
        return 1
    fi
    ensure_libs
    cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR_COVERAGE" -DCMAKE_BUILD_TYPE=Debug \
          -DENABLE_COVERAGE=ON -DTUIKIT_ENABLE_ASAN=OFF -DTUIKIT_BUILD_TESTS=ON \
          "-DWORKSPACE_LIBS=$(libs_mode)" \
          -DTUIKIT_TEST_WRAPPER=
    cmake --build "$BUILD_DIR_COVERAGE" --parallel "$JOBS"
    lcov --zerocounters --directory "$BUILD_DIR_COVERAGE"
    # The suites' verdict counts: a failing suite fails the coverage run.
    ctest --test-dir "$BUILD_DIR_COVERAGE" -L "^(unit|pty)$" --output-on-failure --no-tests=error

    local info="$BUILD_DIR_COVERAGE/coverage.info"
    lcov --capture --directory "$BUILD_DIR_COVERAGE" --output-file "$info.all"
    # Product code only, one list for every report: the library sources.
    lcov --extract "$info.all" "$SCRIPT_DIR/src/*" --output-file "$info"
    lcov --list "$info"
    if command -v genhtml >/dev/null; then
        genhtml --quiet "$info" --output-directory "$BUILD_DIR_COVERAGE/html"
        echo "HTML report: $BUILD_DIR_COVERAGE/html/index.html"
    fi

    local pct
    pct="$(lcov --summary "$info" | awk '/lines\.*:/ { sub("%", "", $2); print $2 }')"
    if [ -z "$pct" ]; then
        echo "coverage: could not read the line rate from lcov --summary" >&2
        return 1
    fi
    echo "Line coverage of src/: ${pct}% (threshold ${COVERAGE_THRESHOLD}%)"
    if ! awk -v p="$pct" -v t="$COVERAGE_THRESHOLD" 'BEGIN { exit !(p >= t) }'; then
        echo "coverage: ${pct}% is below the threshold of ${COVERAGE_THRESHOLD}%" >&2
        return 1
    fi
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
    if ! grep -h -E '^(Assertions run|Line coverage)' "$logdir/unit.log" \
             "$logdir/pty.log" "$logdir/coverage.log"; then
        echo "  (no assertion counts or coverage figure found in the logs)"
    fi
    if [ "$failed" -ne 0 ]; then
        echo "$failed gate(s) failed."
        return 1
    fi
    echo "All gates passed."
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
    build)     build_release ;;
    debug)     build_debug ;;
    test)      run_unit "$@" ;;
    pty)       run_pty "$@" ;;
    functional) run_functional "$@" ;;
    coverage)  run_coverage ;;
    valgrind)  run_valgrind ;;
    consumer)  run_consumer ;;
    check)     run_check ;;
    package)   do_package "${1:-deb}" ;;
    clean)     clean ;;
    help|-h|--help) usage ;;
    *)
        echo "unknown command: $cmd" >&2
        usage >&2
        exit 1
        ;;
esac
