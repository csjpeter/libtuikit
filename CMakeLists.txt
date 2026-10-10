# SPDX-License-Identifier: GPL-3.0-only OR LicenseRef-Proprietary
# Copyright (c) 2026 Peter Csaszar (Császár Péter)
# Created: 2026-10-03
# The build, test, install and package definition of libtuikit.

cmake_minimum_required(VERSION 3.20)
project(libtuikit VERSION 0.4.1 LANGUAGES C)

# ---------------------------------------------------------------------------
# Version
#
# project(... VERSION ...) above is the single source: there is no VERSION
# file and no version written into any packaging metadata.  A build between
# releases is refined with the number of commits since the last tag, so
# successive builds sort correctly for apt.  No "-" in the result: in a
# Debian version the last "-" separates the upstream version from the
# Debian revision.  See policies/packaging-and-release-model.md.
# ---------------------------------------------------------------------------
set(TUIKIT_VERSION "${PROJECT_VERSION}")
set(TUIKIT_GIT_COMMIT "unknown")

find_package(Git QUIET)
if(Git_FOUND AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _tuikit_sha
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _tuikit_sha_rc)
    if(_tuikit_sha_rc EQUAL 0 AND _tuikit_sha)
        set(TUIKIT_GIT_COMMIT "${_tuikit_sha}")
    endif()

    # A repository without a tag yet is a normal state, not an error, so
    # the tag list is asked for rather than "git describe" failing on it.
    execute_process(
        COMMAND "${GIT_EXECUTABLE}" tag --list "v*" --sort=-v:refname --merged HEAD
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        OUTPUT_VARIABLE _tuikit_tags
        OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE _tuikit_tags_rc)
    if(_tuikit_tags_rc EQUAL 0 AND _tuikit_tags)
        string(REPLACE "\n" ";" _tuikit_tags "${_tuikit_tags}")
        list(GET _tuikit_tags 0 _tuikit_tag)
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-list --count "${_tuikit_tag}..HEAD"
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
            OUTPUT_VARIABLE _tuikit_commits
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE _tuikit_commits_rc)
        if(_tuikit_commits_rc EQUAL 0 AND _tuikit_commits
           AND NOT _tuikit_commits STREQUAL "0")
            set(TUIKIT_VERSION "${PROJECT_VERSION}.${_tuikit_commits}")
        endif()
    endif()
endif()

string(TIMESTAMP TUIKIT_BUILD_DATE "%Y-%m-%dT%H:%M:%SZ" UTC)
string(TIMESTAMP TUIKIT_BUILD_STAMP "%Y%m%dT%H%M%S" UTC)
message(STATUS "libtuikit version ${TUIKIT_VERSION} (commit ${TUIKIT_GIT_COMMIT})")

# ---------------------------------------------------------------------------
# Build options
#
# Used by another project through add_subdirectory(), only the library is
# built; tests, documentation and packaging belong to this project's own
# build.
# ---------------------------------------------------------------------------
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(TUIKIT_TOP_LEVEL ON)
else()
    set(TUIKIT_TOP_LEVEL OFF)
endif()

option(TUIKIT_BUILD_TESTS "Build the unit and PTY tests"        ${TUIKIT_TOP_LEVEL})
option(TUIKIT_ENABLE_ASAN "Build with AddressSanitizer + UBSan" OFF)
option(ENABLE_COVERAGE    "Build with gcov instrumentation"     OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND TUIKIT_TOP_LEVEL)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(GNUInstallDirs)

set(TUIKIT_WARNINGS -Wall -Wextra -Wshadow -Wstrict-prototypes -Wformat=2
                    -Wconversion -Wno-sign-conversion)

# Hardening for what is shipped; lintian checks for it.
set(TUIKIT_HARDENING
    $<$<CONFIG:Release>:-D_FORTIFY_SOURCE=2>
    -fstack-protector-strong)
set(TUIKIT_HARDENING_LINK "LINKER:-z,relro" "LINKER:-z,now")

if(TUIKIT_ENABLE_ASAN AND ENABLE_COVERAGE)
    message(FATAL_ERROR "ASAN and coverage are separate builds: gcov's "
                        "bookkeeping and the sanitiser's do not mix "
                        "(policies/testing-model.md)")
endif()

if(TUIKIT_ENABLE_ASAN)
    set(TUIKIT_SAN_FLAGS -fsanitize=address,undefined
                         -fno-sanitize-recover=undefined
                         -fno-omit-frame-pointer)
    add_compile_options(${TUIKIT_SAN_FLAGS})
    add_link_options(${TUIKIT_SAN_FLAGS})
endif()

if(ENABLE_COVERAGE)
    add_compile_options(--coverage -O0 -g)
    add_link_options(--coverage)
endif()

# ---------------------------------------------------------------------------
# The library: one set of sources, built shared (what the package ships and
# what consumers link) and static (what the unit tests link, so that they
# can reach the internal helpers the shared library does not export).
# ---------------------------------------------------------------------------
set(TUIKIT_SOURCES
    src/buf.c
    src/canvas.c
    src/decoder.c
    src/dialog.c
    src/fieldset.c
    src/lineedit.c
    src/listview.c
    src/screen.c
    src/term.c
    src/text.c
    src/textedit.c
    src/textview.c
    src/tty.c
    src/version.c
    src/width_table.c)

file(GLOB TUIKIT_PUBLIC_HEADERS CONFIGURE_DEPENDS
     "${CMAKE_CURRENT_SOURCE_DIR}/include/tuikit/*.h")

function(tuikit_configure_target _tgt)
    target_include_directories(${_tgt}
        PUBLIC  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
                $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
        PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(${_tgt} PRIVATE
        TUIKIT_VERSION_STRING="${TUIKIT_VERSION}")
    target_compile_options(${_tgt} PRIVATE ${TUIKIT_WARNINGS} ${TUIKIT_HARDENING})
endfunction()

add_library(tuikit SHARED ${TUIKIT_SOURCES})
tuikit_configure_target(tuikit)
set_target_properties(tuikit PROPERTIES
    OUTPUT_NAME tuikit
    VERSION     ${PROJECT_VERSION}
    SOVERSION   ${PROJECT_VERSION_MAJOR}
    C_VISIBILITY_PRESET hidden)
target_link_options(tuikit PRIVATE ${TUIKIT_HARDENING_LINK} "LINKER:--as-needed")

add_library(tuikit_static STATIC ${TUIKIT_SOURCES})
tuikit_configure_target(tuikit_static)
set_target_properties(tuikit_static PROPERTIES
    OUTPUT_NAME tuikit
    POSITION_INDEPENDENT_CODE ON)

add_library(tuikit::tuikit ALIAS tuikit)
add_library(tuikit::tuikit_static ALIAS tuikit_static)

if(NOT TUIKIT_TOP_LEVEL)
    return()
endif()

# ---------------------------------------------------------------------------
# The width table is generated at development time and committed; see
# tools/gen-width.c.  "cmake --build <dir> --target regen-width" rewrites it.
# ---------------------------------------------------------------------------
add_executable(gen-width EXCLUDE_FROM_ALL tools/gen-width.c)
target_compile_options(gen-width PRIVATE ${TUIKIT_WARNINGS})
add_custom_target(regen-width
    COMMAND gen-width "${CMAKE_CURRENT_SOURCE_DIR}/src/width_table.c"
    DEPENDS gen-width
    COMMENT "Regenerating src/width_table.c from glibc's C.UTF-8 wcwidth")

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(TUIKIT_BUILD_TESTS)
    # The test instrument: a dependency of the tests only, never of the
    # library (docs/specification.md, section 2).
    find_package(ptytest 0.1 REQUIRED)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Manual pages, compressed: lintian rejects uncompressed ones.  Every page
# in man/ is installed; a function documented on another function's page
# gets an alias, a symlink to that page.  The unit test "man-pages" checks
# that every exported function has one or the other.
# ---------------------------------------------------------------------------
file(GLOB TUIKIT_MAN3_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/man/man3/*.3")
file(GLOB TUIKIT_MAN7_SRC CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/man/man7/*.7")

# alias page -> page it points to
set(TUIKIT_MAN3_ALIASES
    tui_buf_free:tui_buf_init tui_buf_reset:tui_buf_init
    tui_buf_append:tui_buf_init tui_buf_puts:tui_buf_init
    tui_buf_printf:tui_buf_init tui_buf_write_fd:tui_buf_init
    tui_term_opts_init:tui_term_open tui_term_close:tui_term_open
    tui_term_size:tui_term_open tui_term_write:tui_term_open
    tui_term_flush_buf:tui_term_open tui_term_suspend:tui_term_open
    tui_term_resume:tui_term_open tui_term_fd:tui_term_wait
    tui_tty_cols:tui_tty_is tui_tty_rows:tui_tty_is
    tui_tty_raw_enter:tui_tty_is tui_tty_raw_exit:tui_tty_is
    tui_tty_read_key:tui_tty_is tui_tty_read_password:tui_tty_is
    tui_decoder_free:tui_decoder_init tui_decoder_feed:tui_decoder_init
    tui_decoder_next:tui_decoder_init tui_decoder_pending:tui_decoder_init
    tui_decoder_timeout:tui_decoder_init tui_event_name:tui_decoder_init
    tui_utf8_decode:tui_cp_width tui_utf8_encode:tui_cp_width
    tui_utf8_seq_len:tui_cp_width tui_cp_sanitize:tui_cp_width
    tui_str_width:tui_cp_width tui_strn_width:tui_cp_width
    tui_str_fit:tui_cp_width tui_str_fit_ellipsis:tui_cp_width
    tui_version:libtuikit
    tui_screen_free:tui_screen_new tui_screen_resize:tui_screen_new
    tui_screen_rows:tui_screen_new tui_screen_cols:tui_screen_new
    tui_screen_clear:tui_screen_new tui_screen_put:tui_screen_new
    tui_screen_fill:tui_screen_new tui_screen_set_cursor:tui_screen_new
    tui_screen_invalidate:tui_screen_new tui_screen_set_sync:tui_screen_new
    tui_screen_render:tui_screen_new
    tui_canvas_sub:tui_canvas_of tui_canvas_put:tui_canvas_of
    tui_canvas_put_aligned:tui_canvas_of tui_canvas_fill:tui_canvas_of
    tui_canvas_clear:tui_canvas_of tui_canvas_hline:tui_canvas_of
    tui_canvas_vline:tui_canvas_of tui_canvas_box:tui_canvas_of
    tui_canvas_set_cursor:tui_canvas_of tui_rect_split_h:tui_canvas_of
    tui_rect_split_v:tui_canvas_of tui_rect_inset:tui_canvas_of
    tui_rect_center:tui_canvas_of tui_rect_intersect:tui_canvas_of
    tui_listview_set_count:tui_listview_init tui_listview_set_viewport:tui_listview_init
    tui_listview_select:tui_listview_init tui_listview_move_up:tui_listview_init
    tui_listview_move_down:tui_listview_init tui_listview_page_up:tui_listview_init
    tui_listview_page_down:tui_listview_init tui_listview_home:tui_listview_init
    tui_listview_end:tui_listview_init tui_listview_reveal_selected:tui_listview_init
    tui_listview_is_visible:tui_listview_init tui_listview_handle:tui_listview_init
    tui_listview_render:tui_listview_init
    tui_textview_free:tui_textview_init tui_textview_set_text:tui_textview_init
    tui_textview_append:tui_textview_init tui_textview_wrap:tui_textview_init
    tui_textview_lines:tui_textview_init tui_textview_scroll:tui_textview_init
    tui_textview_scroll_to:tui_textview_init tui_textview_scroll_end:tui_textview_init
    tui_textview_at_end:tui_textview_init tui_textview_top:tui_textview_init
    tui_textview_handle:tui_textview_init tui_textview_render:tui_textview_init
    tui_lineedit_free:tui_lineedit_init tui_lineedit_set_text:tui_lineedit_init
    tui_lineedit_insert:tui_lineedit_init tui_lineedit_handle:tui_lineedit_init
    tui_lineedit_render:tui_lineedit_init tui_lineedit_cursor_col:tui_lineedit_init
    tui_lineedit_run:tui_lineedit_init tui_history_init:tui_lineedit_init
    tui_history_free:tui_lineedit_init tui_history_add:tui_lineedit_init
    tui_history_count:tui_lineedit_init tui_history_get:tui_lineedit_init
    tui_readline_opts_init:tui_readline
    tui_dialog_confirm:tui_dialog_message tui_dialog_prompt:tui_dialog_message
    tui_dialog_menu:tui_dialog_message tui_dialog_free:tui_dialog_message
    tui_dialog_handle:tui_dialog_message tui_dialog_render:tui_dialog_message
    tui_dialog_checklist:tui_dialog_message tui_dialog_is_checked:tui_dialog_message
    tui_dialog_help:tui_dialog_message
    tui_fieldset_free:tui_fieldset_init tui_field_text:tui_fieldset_init
    tui_field_choice:tui_fieldset_init tui_fieldset_focus:tui_fieldset_init
    tui_fieldset_set_error:tui_fieldset_init tui_fieldset_update_completion:tui_fieldset_init
    tui_fieldset_handle:tui_fieldset_init tui_fieldset_render:tui_fieldset_init
    tui_tty_wait:tui_tty_is tui_lineedit_replace:tui_lineedit_init
    tui_textedit_set_text:tui_textedit_init tui_textedit_insert:tui_textedit_init
    tui_textedit_handle:tui_textedit_init tui_textedit_lines:tui_textedit_init
    tui_textedit_cursor:tui_textedit_init tui_textedit_render:tui_textedit_init
    tui_textedit_summary:tui_textedit_init)
set(TUIKIT_MAN3_ALIASES ${TUIKIT_MAN3_ALIASES} CACHE INTERNAL "")

set(TUIKIT_MAN_OUT "${CMAKE_BINARY_DIR}/man")
set(TUIKIT_MAN_GZ "")
foreach(_src IN LISTS TUIKIT_MAN3_SRC TUIKIT_MAN7_SRC)
    get_filename_component(_name "${_src}" NAME)
    string(REGEX REPLACE ".*\\." "" _sec "${_name}")
    set(_dst "${TUIKIT_MAN_OUT}/man${_sec}/${_name}")
    # Copy, then compress in place; -n keeps the build reproducible.
    add_custom_command(
        OUTPUT  "${_dst}.gz"
        COMMAND "${CMAKE_COMMAND}" -E make_directory "${TUIKIT_MAN_OUT}/man${_sec}"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_src}" "${_dst}"
        COMMAND gzip -9nf "${_dst}"
        DEPENDS "${_src}"
        COMMENT "Compressing man page ${_name}")
    list(APPEND TUIKIT_MAN_GZ "${_dst}.gz")
endforeach()

set(TUIKIT_MAN_LINKS "")
foreach(_alias IN LISTS TUIKIT_MAN3_ALIASES)
    string(REPLACE ":" ";" _pair "${_alias}")
    list(GET _pair 0 _from)
    list(GET _pair 1 _to)
    if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/man/man3/${_to}.3")
        message(FATAL_ERROR "man alias ${_from} points to a missing page ${_to}.3")
    endif()
    add_custom_command(
        OUTPUT  "${TUIKIT_MAN_OUT}/man3/${_from}.3.gz"
        COMMAND "${CMAKE_COMMAND}" -E create_symlink
                "${_to}.3.gz" "${TUIKIT_MAN_OUT}/man3/${_from}.3.gz"
        DEPENDS "${TUIKIT_MAN_OUT}/man3/${_to}.3.gz")
    list(APPEND TUIKIT_MAN_LINKS "${TUIKIT_MAN_OUT}/man3/${_from}.3.gz")
endforeach()
add_custom_target(tuikit_man ALL DEPENDS ${TUIKIT_MAN_GZ} ${TUIKIT_MAN_LINKS})

# ---------------------------------------------------------------------------
# Debian documentation: copyright and changelog, which lintian requires in
# every binary package.
# ---------------------------------------------------------------------------
set(TUIKIT_MAINTAINER "Péter Császár <csjpeter@gmail.com>")
set(TUIKIT_PKG_RUNTIME "libtuikit${PROJECT_VERSION_MAJOR}")
set(TUIKIT_PKG_DEV     "libtuikit-dev")

file(WRITE "${CMAKE_BINARY_DIR}/debian-doc/copyright"
"Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/
Upstream-Name: libtuikit
Source: https://github.com/csjpeter/libtuikit

Files: *
Copyright: 2026 Peter Csaszar (Császár Péter)
License: GPL-3

License: GPL-3
 This program is free software: you can redistribute it and/or modify it
 under the terms of the GNU General Public License as published by the Free
 Software Foundation, version 3 of the License.
 .
 The copyright holder also licenses this work, separately, under
 proprietary terms; this licence is the one that applies to everyone else.
 .
 This program is distributed in the hope that it will be useful, but WITHOUT
 ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 more details.
 .
 On Debian systems, the complete text of the GNU General Public License
 version 3 can be found in /usr/share/common-licenses/GPL-3.
")

string(TIMESTAMP TUIKIT_CHANGELOG_DATE "%a, %d %b %Y %H:%M:%S +0000" UTC)

function(tuikit_write_changelog _pkg)
    set(_dir "${CMAKE_BINARY_DIR}/debian-doc/${_pkg}")
    file(WRITE "${_dir}/changelog"
"${_pkg} (${TUIKIT_VERSION}) stable; urgency=medium

  * Build from git commit ${TUIKIT_GIT_COMMIT}.
  * The per-change history is the git history; this file exists because
    Debian policy requires a changelog in every binary package.

 -- ${TUIKIT_MAINTAINER}  ${TUIKIT_CHANGELOG_DATE}
")
    add_custom_command(
        OUTPUT  "${_dir}/changelog.gz"
        COMMAND "${CMAKE_COMMAND}" -E copy "${_dir}/changelog" "${_dir}/changelog.tmp"
        COMMAND gzip -9nf "${_dir}/changelog.tmp"
        COMMAND "${CMAKE_COMMAND}" -E rename "${_dir}/changelog.tmp.gz" "${_dir}/changelog.gz"
        DEPENDS "${_dir}/changelog"
        COMMENT "Compressing changelog for ${_pkg}")
    add_custom_target("tuikit_changelog_${_pkg}" ALL DEPENDS "${_dir}/changelog.gz")
endfunction()

tuikit_write_changelog("${TUIKIT_PKG_RUNTIME}")
tuikit_write_changelog("${TUIKIT_PKG_DEV}")

# ---------------------------------------------------------------------------
# Install rules: the list of what the software consists of.
#
#   runtime  libtuikit.so.N                     -> libtuikitN
#   dev      headers, libtuikit.so, .a, pkg-config, CMake package,
#            man pages (3 and 7)                -> libtuikit-dev
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

install(TARGETS tuikit tuikit_static
        EXPORT tuikitTargets
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                COMPONENT runtime
                NAMELINK_COMPONENT dev
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
                COMPONENT dev)
install(FILES ${TUIKIT_PUBLIC_HEADERS}
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/tuikit"
        COMPONENT dev)

install(EXPORT tuikitTargets
        NAMESPACE tuikit::
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tuikit"
        COMPONENT dev)

configure_package_config_file(
    "${CMAKE_SOURCE_DIR}/cmake/tuikitConfig.cmake.in"
    "${CMAKE_BINARY_DIR}/tuikitConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tuikit")
write_basic_package_version_file(
    "${CMAKE_BINARY_DIR}/tuikitConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_BINARY_DIR}/tuikitConfig.cmake"
              "${CMAKE_BINARY_DIR}/tuikitConfigVersion.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/tuikit"
        COMPONENT dev)

configure_file("${CMAKE_SOURCE_DIR}/cmake/libtuikit.pc.in"
               "${CMAKE_BINARY_DIR}/libtuikit.pc" @ONLY)
install(FILES "${CMAKE_BINARY_DIR}/libtuikit.pc"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig"
        COMPONENT dev)

install(DIRECTORY "${TUIKIT_MAN_OUT}/man3" "${TUIKIT_MAN_OUT}/man7"
        DESTINATION "${CMAKE_INSTALL_MANDIR}"
        COMPONENT dev)

install(FILES "${CMAKE_BINARY_DIR}/debian-doc/copyright"
              "${CMAKE_BINARY_DIR}/debian-doc/${TUIKIT_PKG_RUNTIME}/changelog.gz"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${TUIKIT_PKG_RUNTIME}"
        COMPONENT runtime)
install(FILES "${CMAKE_BINARY_DIR}/debian-doc/copyright"
              "${CMAKE_BINARY_DIR}/debian-doc/${TUIKIT_PKG_DEV}/changelog.gz"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/${TUIKIT_PKG_DEV}"
        COMPONENT dev)

# lintian overrides: the XB- build metadata fields (see the file).
foreach(_comp_pkg IN ITEMS "runtime:${TUIKIT_PKG_RUNTIME}" "dev:${TUIKIT_PKG_DEV}")
    string(REPLACE ":" ";" _cp "${_comp_pkg}")
    list(GET _cp 0 _comp)
    list(GET _cp 1 PACKAGE)
    configure_file("${CMAKE_SOURCE_DIR}/debian/lintian-overrides"
                   "${CMAKE_BINARY_DIR}/debian-doc/${PACKAGE}/lintian-overrides" @ONLY)
    install(FILES "${CMAKE_BINARY_DIR}/debian-doc/${PACKAGE}/lintian-overrides"
            DESTINATION "${CMAKE_INSTALL_DATADIR}/lintian/overrides"
            RENAME "${PACKAGE}"
            COMPONENT ${_comp})
endforeach()

# ===========================================================================
# Packaging (CPack)
#
# All package metadata lives here and nowhere else: no debian/control, no
# .spec file.  manage.sh only configures a Release build and calls CPack.
# See policies/packaging-and-release-model.md.
# ===========================================================================
set(CPACK_PACKAGE_NAME          "libtuikit")
set(CPACK_PACKAGE_VERSION       "${TUIKIT_VERSION}")
set(CPACK_PACKAGE_CONTACT       "${TUIKIT_MAINTAINER}")
set(CPACK_PACKAGE_VENDOR        "csjpeter")
set(CPACK_PACKAGE_HOMEPAGE_URL  "https://github.com/csjpeter/libtuikit")
# No CPACK_PACKAGE_DESCRIPTION_SUMMARY: CPackDeb would prepend it to each
# component's description, and the first line of each is its own synopsis.
set(CPACK_STRIP_FILES ON)
# Without this the directory modes come from the building user's umask.
set(CPACK_INSTALL_DEFAULT_DIRECTORY_PERMISSIONS
    OWNER_READ OWNER_WRITE OWNER_EXECUTE
    GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)

set(CPACK_GENERATOR "DEB;RPM")
set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_COMPONENTS_ALL runtime dev)
set(CPACK_COMPONENTS_GROUPING IGNORE)

# --- Debian ----------------------------------------------------------------
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${TUIKIT_MAINTAINER}")
set(CPACK_DEBIAN_PACKAGE_PRIORITY   "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE   "${CPACK_PACKAGE_HOMEPAGE_URL}")
# Discovered from the binaries rather than guessed by hand.
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS  ON)

execute_process(COMMAND dpkg --print-architecture
                OUTPUT_VARIABLE TUIKIT_DEB_ARCH
                OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE _tuikit_arch_rc)
if(NOT _tuikit_arch_rc EQUAL 0 OR NOT TUIKIT_DEB_ARCH)
    set(TUIKIT_DEB_ARCH "amd64")
endif()

# The finished packages, and nothing else, go to build/packages/: CPack's
# own staging tree (_CPack_Packages) stays in the build directory.
# manage.sh empties the directory before each run, so it holds exactly one
# run's packages (policies/packaging-and-release-model.md, "Where the
# output goes").
set(CPACK_OUTPUT_FILE_PREFIX "${CMAKE_BINARY_DIR}/packages")

set(CPACK_DEBIAN_RUNTIME_PACKAGE_NAME    "${TUIKIT_PKG_RUNTIME}")
set(CPACK_DEBIAN_RUNTIME_PACKAGE_SECTION "libs")
# shlibs file and the ldconfig trigger, so packages linking the library get
# a correct dependency on it.  (CPackDeb has no per-component switch for
# this; the dev package has no shared object, so the global one affects
# only the runtime package.)
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS ON)
# ">=", not CPack's default "=": within one SOVERSION the ABI only grows, so
# a program built against this version runs on every later one.  With "="
# every dependant pins the exact version, and apt removes it to install an
# upgrade of the library (TUI-6).
set(CPACK_DEBIAN_PACKAGE_GENERATE_SHLIBS_POLICY ">=")
set(CPACK_DEBIAN_RUNTIME_FILE_NAME
    "${TUIKIT_PKG_RUNTIME}_${TUIKIT_VERSION}_${TUIKIT_BUILD_STAMP}_${TUIKIT_GIT_COMMIT}_${TUIKIT_DEB_ARCH}.deb")
set(CPACK_COMPONENT_RUNTIME_DESCRIPTION
"Terminal user interface toolkit for full-screen C programs
libtuikit gives a full-screen terminal program raw mode and its reliable
restoration, size and resize events, decoded keys, a diffed cell screen,
clipped drawing, and list, text, line-editing and dialog widgets, with no
dependency beyond the C library.

This package contains the shared library.")

set(CPACK_DEBIAN_DEV_PACKAGE_NAME    "${TUIKIT_PKG_DEV}")
set(CPACK_DEBIAN_DEV_PACKAGE_SECTION "libdevel")
set(CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "${TUIKIT_PKG_RUNTIME} (= ${TUIKIT_VERSION})")
set(CPACK_DEBIAN_DEV_FILE_NAME
    "${TUIKIT_PKG_DEV}_${TUIKIT_VERSION}_${TUIKIT_BUILD_STAMP}_${TUIKIT_GIT_COMMIT}_${TUIKIT_DEB_ARCH}.deb")
set(CPACK_COMPONENT_DEV_DESCRIPTION
"Terminal user interface toolkit for full-screen C programs - development
libtuikit gives a full-screen terminal program raw mode and its reliable
restoration, size and resize events, decoded keys, a diffed cell screen,
clipped drawing, and list, text, line-editing and dialog widgets, with no
dependency beyond the C library.

This package contains the headers, the static library, the pkg-config
and CMake package files, and the manual pages.")

# Build date and commit as XB- control fields, and an ldconfig trigger in
# place of CPack's ldconfig calls.  See cmake/deb-post-build.cmake.
set(CPACK_POST_BUILD_SCRIPTS "${CMAKE_SOURCE_DIR}/cmake/deb-post-build.cmake")
# CPACK_-prefixed, because only those reach a post-build script.
set(CPACK_TUIKIT_GIT_COMMIT "${TUIKIT_GIT_COMMIT}")
set(CPACK_TUIKIT_BUILD_DATE "${TUIKIT_BUILD_DATE}")

# --- RPM -------------------------------------------------------------------
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-only")
set(CPACK_RPM_PACKAGE_GROUP   "Development/Libraries")
set(CPACK_RPM_RUNTIME_PACKAGE_SUMMARY
    "Terminal user interface toolkit for full-screen C programs")
set(CPACK_RPM_DEV_PACKAGE_SUMMARY
    "Terminal user interface toolkit for full-screen C programs - development")
set(CPACK_RPM_RUNTIME_PACKAGE_NAME "libtuikit")
set(CPACK_RPM_DEV_PACKAGE_NAME     "libtuikit-devel")
set(CPACK_RPM_DEV_PACKAGE_REQUIRES "libtuikit = ${TUIKIT_VERSION}")
set(CPACK_RPM_PACKAGE_AUTOREQPROV ON)
set(CPACK_RPM_RUNTIME_FILE_NAME RPM-DEFAULT)
set(CPACK_RPM_DEV_FILE_NAME RPM-DEFAULT)
# Directories the system already owns must not be claimed by this package.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
    /usr/share/man /usr/share/man/man3 /usr/share/man/man7
    /usr/share/doc /usr/lib64/pkgconfig /usr/lib64/cmake
    /usr/lib/pkgconfig /usr/lib/cmake /usr/share/lintian
    /usr/share/lintian/overrides)

include(CPack)
