# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Centlake Software AB
#
# Writes the version header, stamping the version the build is and the commit
# it actually came from.
#
# Run as a script (cmake -P) from a build-time target rather than at configure
# time, because a configure-time stamp records whichever commit happened to be
# checked out when cmake last ran. On this project that is not a nicety: the
# artifact of a run is a trace, and a trace that names the wrong build sends
# you looking for a bug in source that never went into the binary.
#
# The version: project() holds the next release, raised right after one is
# tagged, and the tag says what this build is (SemVer, so each sorts before
# the release it leads to):
#
#   on the tag v1.3.0          1.3.0                   the release
#   on the tag v1.3.0-rc.1     1.3.0-rc.1              a release candidate
#   anything else              1.3.0-dev.48+gd5e45db   48 commits past the
#                                                      last tag, at d5e45db
#
# A release or rc tag whose version is not project()'s fails the build: the
# binary would name a version its source does not. A tree with uncommitted
# changes is never the tagged source, so it is a dev build (".dirty").
#
# The header is only rewritten when its contents change, so a rebuild on the
# same commit does not recompile everything that includes it.

execute_process(
    COMMAND git describe --always --dirty --tags
    WORKING_DIRECTORY "${SRC_DIR}"
    OUTPUT_VARIABLE GIT_DESCRIBE
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE git_result)

if(NOT git_result EQUAL 0 OR GIT_DESCRIBE STREQUAL "")
    # A source archive has no repository, and a machine may have no git.
    # Say so rather than invent a hash that would be believed.
    set(GIT_DESCRIBE "no-git")
    set(TAPTO_VERSION_FULL "${PROJECT_VERSION}-dev+no-git")
else()
    execute_process(
        COMMAND git describe --tags --exact-match --match "v[0-9]*"
        WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE exact_tag
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        RESULT_VARIABLE exact_result)
    execute_process(
        COMMAND git describe --tags --long --always --dirty --match "v[0-9]*"
        WORKING_DIRECTORY "${SRC_DIR}"
        OUTPUT_VARIABLE long_describe
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET)
    string(REGEX MATCH "-dirty$" dirty "${long_describe}")

    if(exact_result EQUAL 0 AND NOT dirty
       AND exact_tag MATCHES "^v([0-9]+\\.[0-9]+\\.[0-9]+)(-rc\\.[0-9]+)?$")
        if(NOT CMAKE_MATCH_1 STREQUAL PROJECT_VERSION)
            message(FATAL_ERROR
                "the commit is tagged ${exact_tag}, but CMakeLists.txt's project() says "
                "${PROJECT_VERSION}: set project() to ${CMAKE_MATCH_1} before tagging")
        endif()
        string(SUBSTRING "${exact_tag}" 1 -1 TAPTO_VERSION_FULL)
    elseif(long_describe MATCHES "-([0-9]+)-g([0-9a-f]+)(-dirty)?$")
        set(TAPTO_VERSION_FULL "${PROJECT_VERSION}-dev.${CMAKE_MATCH_1}+g${CMAKE_MATCH_2}")
        if(CMAKE_MATCH_3)
            string(APPEND TAPTO_VERSION_FULL ".dirty")
        endif()
    else()
        # No tag to count from: the bare hash.
        string(REGEX REPLACE "-dirty$" "" hash "${long_describe}")
        set(TAPTO_VERSION_FULL "${PROJECT_VERSION}-dev+g${hash}")
        if(dirty)
            string(APPEND TAPTO_VERSION_FULL ".dirty")
        endif()
    endif()
endif()

configure_file("${IN_FILE}" "${OUT_FILE}.tmp" @ONLY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                        "${OUT_FILE}.tmp" "${OUT_FILE}")
file(REMOVE "${OUT_FILE}.tmp")
