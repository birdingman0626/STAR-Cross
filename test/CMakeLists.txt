cmake_minimum_required(VERSION 3.15)

# ---------- doctest (single-header, fetched at configure time) ----------
include(FetchContent)
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.4.11
    GIT_SHALLOW    TRUE
)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
# doctest 2.4.11 uses cmake_minimum_required(VERSION 2.8.12); CMake >= 4.0 rejects < 3.5.
# Setting CMAKE_POLICY_VERSION_MINIMUM allows its CMakeLists to be processed.
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM "3.5")
endif()
FetchContent_MakeAvailable(doctest)

# ---------- Source files from the STAR codebase compiled into the test binary ----------
# Only the files whose logic we want to test; avoid pulling in the full STAR object graph.
set(STAR_TEST_SOURCES
    ${STAR_SOURCE_DIR}/PackedArray.cpp
    ${STAR_SOURCE_DIR}/binarySearch2.cpp
)

add_executable(star_tests
    main.cpp
    unit/test_FastResetVector.cpp
    unit/test_binarySearch2.cpp
    unit/test_PackedArray.cpp
    unit/test_UMIgraph.cpp
    ${STAR_TEST_SOURCES}
)

target_include_directories(star_tests PRIVATE
    ${STAR_SOURCE_DIR}          # IncludeDefine.h, PackedArray.h, UMIgraph.h, etc.
    ${CMAKE_CURRENT_SOURCE_DIR} # test-local headers
    ${doctest_SOURCE_DIR}       # provides doctest/doctest.h
)

# IncludeDefine.h includes <omp.h>; the test binary must link OpenMP
find_package(OpenMP REQUIRED)
target_link_libraries(star_tests PRIVATE OpenMP::OpenMP_CXX)

# ---------- Platform-specific settings (mirrors source/CMakeLists.txt) ----------
if(MSVC)
    target_compile_definitions(star_tests PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
    )
    target_compile_options(star_tests PRIVATE
        /W3 /EHsc /bigobj
        /wd4244 /wd4267 /wd4100
    )
    target_link_libraries(star_tests PRIVATE psapi ws2_32)
else()
    target_compile_options(star_tests PRIVATE -Wall -Wextra -O1 -Wno-register)
    target_link_libraries(star_tests PRIVATE pthread)
endif()

# STAR_VERSION is injected by the parent CMakeLists via add_compile_definitions (global scope).
# No additional definition needed here.

# ---------- Register with CTest ----------
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(star_tests)
