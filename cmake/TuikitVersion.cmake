# SPDX-License-Identifier: GPL-3.0-only
# Copyright (c) 2026 Peter Csaszar (Created: 2026-10-23)
# Derives the package version: project version + total commit count.

#
# tuikit_derive_version(<source dir> <project version> <out variable>)
#
# The version is <project version>.<git rev-list --count HEAD>.  The tag
# takes no part in it and restarts nothing: the tag vX.Y.Z.N follows the
# version.  Without git, or outside a repository, the bare project version.
# Needs the full history (a shallow clone gives a wrong count).
# See policies/packaging-and-release-model.md, "Deriving the version".
function(tuikit_derive_version src project_version out)
    set(version "${project_version}")
    find_package(Git QUIET)
    if(Git_FOUND AND EXISTS "${src}/.git")
        execute_process(
            COMMAND "${GIT_EXECUTABLE}" rev-list --count HEAD
            WORKING_DIRECTORY "${src}"
            OUTPUT_VARIABLE count
            OUTPUT_STRIP_TRAILING_WHITESPACE
            RESULT_VARIABLE rc)
        if(rc EQUAL 0 AND count)
            set(version "${project_version}.${count}")
        endif()
    endif()
    set("${out}" "${version}" PARENT_SCOPE)
endfunction()

# cmake -P mode: -DSRC=<dir> -DPROJECT_VERSION=<v> prints the version.
if(CMAKE_SCRIPT_MODE_FILE STREQUAL CMAKE_CURRENT_LIST_FILE)
    tuikit_derive_version("${SRC}" "${PROJECT_VERSION}" _v)
    message(STATUS "${_v}")
endif()
