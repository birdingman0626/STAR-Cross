# Reads the checked-out commit when it runs (cmake -P), not when the build was
# configured, so incremental builds report the revision they were built from.
#
# Generate mode (STAR_VERSION_OUTPUT set): writes the version source file and,
# when STAR_BUILD_INFO_TEMPLATE is set, the build-info JSON. configure_file only
# rewrites a file whose content changed, so an unchanged commit recompiles nothing.
#
# Check mode (STAR_CHECK_EXE set): runs "STAR --version" and fails unless it
# prints "STAR-Cross <commit>" for the commit checked out now.
#
# Inputs: STAR_SOURCE_DIR, STAR_GIT_EXECUTABLE (empty when git is missing).

set(STAR_COMMIT "unknown")
set(STAR_SOURCE_TREE_CLEAN "null")
if(STAR_GIT_EXECUTABLE)
    execute_process(COMMAND "${STAR_GIT_EXECUTABLE}" rev-parse --short HEAD
        WORKING_DIRECTORY "${STAR_SOURCE_DIR}"
        OUTPUT_VARIABLE star_hash RESULT_VARIABLE star_hash_result
        OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(star_hash_result EQUAL 0 AND star_hash)
        set(STAR_COMMIT "${star_hash}")
        execute_process(COMMAND "${STAR_GIT_EXECUTABLE}" status --porcelain --untracked-files=normal
            WORKING_DIRECTORY "${STAR_SOURCE_DIR}"
            OUTPUT_VARIABLE star_status RESULT_VARIABLE star_status_result
            OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
        if(star_status_result EQUAL 0)
            if(star_status STREQUAL "")
                set(STAR_SOURCE_TREE_CLEAN "true")
            else()
                set(STAR_SOURCE_TREE_CLEAN "false")
            endif()
        endif()
    endif()
endif()

if(STAR_CHECK_EXE)
    execute_process(COMMAND "${STAR_CHECK_EXE}" --version
        OUTPUT_VARIABLE star_version_out RESULT_VARIABLE star_version_result)
    string(REGEX REPLACE "[\r\n]+$" "" star_version_out "${star_version_out}")
    if(NOT star_version_result EQUAL 0 OR NOT star_version_out STREQUAL "STAR-Cross ${STAR_COMMIT}")
        message(FATAL_ERROR "STAR --version printed '${star_version_out}' (exit ${star_version_result}); "
            "expected 'STAR-Cross ${STAR_COMMIT}'")
    endif()
    return()
endif()

configure_file("${STAR_SOURCE_DIR}/STARVersionInfo.cpp.in" "${STAR_VERSION_OUTPUT}" @ONLY)
if(STAR_BUILD_INFO_TEMPLATE AND EXISTS "${STAR_BUILD_INFO_TEMPLATE}")
    configure_file("${STAR_BUILD_INFO_TEMPLATE}" "${STAR_BUILD_INFO_OUTPUT}" @ONLY)
endif()
