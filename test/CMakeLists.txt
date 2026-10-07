cmake_minimum_required(VERSION 3.15)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# ---------- doctest (single-header, fetched at configure time) ----------
include(FetchContent)
FetchContent_Declare(
    doctest
    URL https://codeload.github.com/doctest/doctest/tar.gz/refs/tags/v2.5.3
    URL_HASH SHA256=174ebc4e769928959614789c5b4e9c3d0a0f81a62bb608756b127bfebfb21331
)
set(DOCTEST_NO_INSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(doctest)

# ---------- Source files from the STAR codebase compiled into the test binary ----------
# Only the files whose logic we want to test; avoid pulling in the full STAR object graph.
set(STAR_TEST_SOURCES
    ${STAR_SOURCE_DIR}/PackedArray.cpp
    ${STAR_SOURCE_DIR}/binarySearch2.cpp
    ${STAR_SOURCE_DIR}/ClipCR4.cpp
)

add_executable(star_tests
    main.cpp
    unit/test_FastResetVector.cpp
    unit/test_binarySearch2.cpp
    unit/test_PackedArray.cpp
    unit/test_byteOrder.cpp
    unit/test_unaligned.cpp
    unit/test_UMIgraph.cpp
    unit/test_blocksOverlap.cpp
    unit/test_EmptyDrops.cpp
    unit/test_UMIgraph_directedCollapse.cpp
    unit/test_samAux.cpp
    unit/test_BGZF.cpp
    unit/test_HTSFormats.cpp
    unit/test_ClipCR4.cpp
    unit/test_gpuSjdbRemap.cpp
    unit/test_ParametersGenome.cpp
    ${STAR_TEST_SOURCES}
)
star_target_build_options(star_tests)
star_target_sanitizers(star_tests)

target_include_directories(star_tests PRIVATE
    ${STAR_SOURCE_DIR}          # IncludeDefine.h, PackedArray.h, UMIgraph.h, etc.
    ${CMAKE_CURRENT_SOURCE_DIR} # test-local headers
    ${doctest_SOURCE_DIR}       # provides doctest/doctest.h
)

# IncludeDefine.h includes <omp.h>; the test binary must link OpenMP
find_package(OpenMP REQUIRED)
target_link_libraries(star_tests PRIVATE OpenMP::OpenMP_CXX hts parasail star_sjdb_remap)
if(STAR_ENABLE_CUDA)
    target_compile_definitions(star_tests PRIVATE STAR_TEST_CUDA=1)
    add_library(star_seed_search STATIC ${STAR_SOURCE_DIR}/gpuSeedSearch.cu)
    set_target_properties(star_seed_search PROPERTIES CUDA_STANDARD ${STAR_CUDA_STANDARD} CUDA_STANDARD_REQUIRED ON
                          CUDA_ARCHITECTURES "${STAR_CUDA_ARCHITECTURES}")
    target_include_directories(star_seed_search PUBLIC ${STAR_SOURCE_DIR})
    target_link_libraries(star_seed_search PUBLIC CUDA::cudart)
    add_executable(star_seed_benchmark benchmark_seed_search.cpp
        ${STAR_SOURCE_DIR}/SuffixArrayFuns.cpp ${STAR_SOURCE_DIR}/PackedArray.cpp)
    star_target_build_options(star_seed_benchmark)
    target_include_directories(star_seed_benchmark PRIVATE ${STAR_SOURCE_DIR})
    target_link_libraries(star_seed_benchmark PRIVATE star_seed_search OpenMP::OpenMP_CXX parasail)
    if(MSVC)
        target_compile_options(star_seed_benchmark PRIVATE /EHsc)
        target_compile_definitions(star_seed_benchmark PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
        target_link_libraries(star_seed_benchmark PRIVATE psapi ws2_32)
    endif()
endif()

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
    target_compile_options(star_tests PRIVATE -Wall -Wextra -Wno-register)
    target_link_libraries(star_tests PRIVATE pthread)
endif()

# STAR_VERSION and sanitizer flags share the production target's helpers.

# ---------- Register with CTest ----------
include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(star_tests)

# Exercise both Unix mapping implementations in separate processes. Windows IPC
# is deliberately unsupported; do not substitute a stub test for this contract.
if(UNIX)
    foreach(backend IN ITEMS sysv posix)
        add_executable(star_ipc_${backend} shared_memory_lifecycle.cpp ${STAR_SOURCE_DIR}/SharedMemory.cpp)
        target_include_directories(star_ipc_${backend} PRIVATE ${STAR_SOURCE_DIR})
        star_target_build_options(star_ipc_${backend})
        star_target_sanitizers(star_ipc_${backend})
        if(backend STREQUAL "posix")
            target_compile_definitions(star_ipc_${backend} PRIVATE POSIX_SHARED_MEM)
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                target_link_libraries(star_ipc_${backend} PRIVATE rt)
            endif()
        endif()
        add_test(NAME shared_memory_${backend}_lifecycle COMMAND star_ipc_${backend})
        set_tests_properties(shared_memory_${backend}_lifecycle PROPERTIES TIMEOUT 20)
    endforeach()
endif()
