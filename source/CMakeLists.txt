cmake_minimum_required(VERSION 3.15)
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
project(STAR LANGUAGES CXX C)

set(STAR_CXX_STANDARD "17" CACHE STRING "C++ language level for STAR targets (17 or 20)")
set_property(CACHE STAR_CXX_STANDARD PROPERTY STRINGS 17 20)
if(NOT STAR_CXX_STANDARD MATCHES "^(17|20)$")
    message(FATAL_ERROR "STAR_CXX_STANDARD must be 17 or 20")
endif()
set(CMAKE_CXX_STANDARD ${STAR_CXX_STANDARD})
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile_commands.json for LSP/IDE support (clangd, ccls)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------- Git commit hash for version string ----------
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
        OUTPUT_VARIABLE GIT_COMMIT_HASH
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    )
endif()
if(NOT GIT_COMMIT_HASH)
    set(GIT_COMMIT_HASH "unknown")
endif()
# Identify the exact source revision: "STAR-Cross <hash>".
# STAR-Cross is derived from the preserved upstream boundary b1edc12.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/StarBuildOptions.cmake)

# ---------- Options ----------
option(STAR_LONG_READS "Build STARlong variant for long reads" OFF)
option(STAR_POSIX_SHARED_MEM "Use POSIX rather than SysV shared memory (Unix only)" OFF)
if(STAR_POSIX_SHARED_MEM AND WIN32)
    message(FATAL_ERROR "STAR_POSIX_SHARED_MEM requires a Unix platform")
endif()
# AVX2 is x86-only; enable it only on x86_64/AMD64 and auto-disable on every
# other architecture (ARM64/Apple Silicon, s390x, ppc64, ...) so -mavx2 is never
# passed to a non-x86 compiler.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64|amd64|x64")
    option(STAR_USE_AVX2 "Enable AVX2 SIMD instructions (requires AVX2 CPU)" OFF)
else()
    option(STAR_USE_AVX2 "Enable AVX2 SIMD instructions" OFF)
endif()
option(STAR_ASAN "Enable AddressSanitizer" OFF)
option(STAR_UBSAN "Enable UndefinedBehaviorSanitizer (GCC/Clang)" OFF)
if(STAR_UBSAN AND MSVC)
    message(FATAL_ERROR "STAR_UBSAN requires GCC or Unix Clang")
endif()

option(STAR_ENABLE_CUDA "Build experimental CUDA junction SA remap" OFF)
set(STAR_CUDA_STANDARD "17" CACHE STRING "CUDA device language level (17 or 20), independent of host C++")
set_property(CACHE STAR_CUDA_STANDARD PROPERTY STRINGS 17 20)
if(NOT STAR_CUDA_STANDARD MATCHES "^(17|20)$")
    message(FATAL_ERROR "STAR_CUDA_STANDARD must be 17 or 20")
endif()
option(STAR_CAPTURE_SEEDS "Diagnostic bounded post-clipping seed capture (not for benchmarks)" OFF)
if(STAR_CAPTURE_SEEDS)
    set_property(SOURCE ReadAlign_maxMappableLength2strands.cpp ReadAlign_mapOneRead.cpp
                 SuffixArrayFuns.cpp ReadAlign_stitchPieces.cpp ReadAlign_stitchWindowSeeds.cpp
                 stitchWindowAligns.cpp ReadAlignChunk_processChunks.cpp APPEND PROPERTY
                 COMPILE_DEFINITIONS STAR_CAPTURE_SEEDS=1)
endif()
if(STAR_ENABLE_CUDA)
    if(CMAKE_VERSION VERSION_LESS "3.24")
        message(FATAL_ERROR "Experimental CUDA build requires CMake >= 3.24")
    endif()
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(STAR_CUDA_ARCHITECTURES "native" CACHE STRING "CUDA architectures for the experimental adapter")
    add_library(star_sjdb_remap STATIC gpuSjdbRemap.cu)
    set_target_properties(star_sjdb_remap PROPERTIES CUDA_STANDARD ${STAR_CUDA_STANDARD} CUDA_STANDARD_REQUIRED ON
                          CUDA_ARCHITECTURES "${STAR_CUDA_ARCHITECTURES}")
    target_link_libraries(star_sjdb_remap PUBLIC CUDA::cudart)
else()
    add_library(star_sjdb_remap STATIC gpuSjdbRemap.cpp)
endif()
target_include_directories(star_sjdb_remap PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# ---------- Find or build zlib ----------
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    message(STATUS "System zlib not found, building from source via FetchContent")
    include(FetchContent)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_TESTING OFF CACHE BOOL "" FORCE)
    set(ZLIB_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        zlib
        URL https://codeload.github.com/madler/zlib/tar.gz/refs/tags/v1.3.2
        URL_HASH SHA256=b99a0b86c0ba9360ec7e78c4f1e43b1cbdf1e6936c8fa0f6835c0cd694a495a1
    )
    FetchContent_MakeAvailable(zlib)

    # Make zlib headers available
    set(ZLIB_INCLUDE_DIRS "${zlib_SOURCE_DIR}" "${zlib_BINARY_DIR}")
    set(ZLIB_LIBRARIES zlibstatic)

    # Create an alias so ZLIB::ZLIB works
    if(NOT TARGET ZLIB::ZLIB)
        add_library(ZLIB::ZLIB ALIAS zlibstatic)
    endif()
endif()

# ---------- Parasail (sequence alignment for adapter clipping) ----------
if(NOT FETCHCONTENT_DECLARED)
    include(FetchContent)
endif()
set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# Parasail's cmake_minimum_required predates CMake 4.x policy enforcement
if(CMAKE_VERSION VERSION_GREATER_EQUAL "4.0")
    set(CMAKE_POLICY_VERSION_MINIMUM "3.5")
endif()
FetchContent_Declare(parasail
    URL https://codeload.github.com/jeffdaily/parasail/tar.gz/refs/tags/v2.6.2
    URL_HASH SHA256=9057041db8e1cde76678f649420b85054650414e5de9ea84ee268756c7ea4b4b
    PATCH_COMMAND ${CMAKE_COMMAND} -DPARASAIL_SOURCE=<SOURCE_DIR>
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/PatchParasail.cmake
)
FetchContent_MakeAvailable(parasail)
set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)

# Parasail builds its own test/app executables unconditionally. Exclude them from
# the default ALL target so they don't cause build failures on newer compilers
# (e.g. test_verify_traces fails with GCC 15 on macOS due to int8_t* / int* mismatch).
# test_verify is kept as an explicit build target for CTest (see below).
foreach(_pt IN ITEMS
        test_verify_tables test_verify_rowcols test_verify_traces test_verify_cigars
        parasail_aligner parasail_stats)
    if(TARGET ${_pt})
        set_target_properties(${_pt} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    endif()
endforeach()

# The patch step fixes the upstream fixture path without writing source files.

# ---------- cpp-httplib (header-only HTTP server) ----------
FetchContent_Declare(httplib
    URL https://codeload.github.com/yhirose/cpp-httplib/tar.gz/refs/tags/v0.60.0
    URL_HASH SHA256=f8d6bc6e2a958f9f21bde11ca408bd987cdf91b99ed9fc63c3e4ea3439376e71
)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)   # keep header-only, no separate lib
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# ---------- nlohmann/json (header-only JSON) ----------
FetchContent_Declare(nlohmann_json
    URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# ---------- Generate parametersDefault.xxd ----------
# Replicate the `xxd -i parametersDefault > parametersDefault.xxd` step
set(PARAM_DEFAULT_FILE "${CMAKE_CURRENT_SOURCE_DIR}/parametersDefault")
set(PARAM_DEFAULT_XXD "${CMAKE_CURRENT_BINARY_DIR}/parametersDefault.xxd")

add_custom_command(
    OUTPUT "${PARAM_DEFAULT_XXD}"
    COMMAND ${CMAKE_COMMAND}
        -DINPUT_FILE=${PARAM_DEFAULT_FILE}
        -DOUTPUT_FILE=${PARAM_DEFAULT_XXD}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxd.cmake
    DEPENDS "${PARAM_DEFAULT_FILE}" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/xxd.cmake"
    COMMENT "Generating parametersDefault.xxd"
)
add_custom_target(generate_xxd DEPENDS "${PARAM_DEFAULT_XXD}")

# ---------- HTSlib ----------
option(USE_SYSTEM_HTSLIB "Use system-installed HTSlib instead of bundled" OFF)
option(STAR_USE_LIBDEFLATE "Enable libdeflate in bundled HTSlib (requires installed development library)" OFF)

if(STAR_USE_LIBDEFLATE AND USE_SYSTEM_HTSLIB)
    message(FATAL_ERROR "STAR_USE_LIBDEFLATE applies only to bundled HTSlib; configure the system HTSlib separately")
endif()

if(USE_SYSTEM_HTSLIB)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(HTS REQUIRED IMPORTED_TARGET htslib)
    # Create an alias target so the rest of the build uses 'hts' uniformly
    add_library(hts ALIAS PkgConfig::HTS)
    set(HTSLIB_DIR "" CACHE INTERNAL "")
    message(STATUS "Using system HTSlib: ${HTS_VERSION}")
else()

set(HTSLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/htslib")
set(HTSLIB_SOURCES
    # Core htslib 1.24
    ${HTSLIB_DIR}/kfunc.c
    ${HTSLIB_DIR}/kstring.c
    ${HTSLIB_DIR}/bcf_sr_sort.c
    ${HTSLIB_DIR}/bgzf.c
    ${HTSLIB_DIR}/errmod.c
    ${HTSLIB_DIR}/faidx.c
    ${HTSLIB_DIR}/header.c
    ${HTSLIB_DIR}/hfile.c
    ${HTSLIB_DIR}/hts.c
    ${HTSLIB_DIR}/hts_expr.c
    ${HTSLIB_DIR}/hts_os.c
    ${HTSLIB_DIR}/md5.c
    ${HTSLIB_DIR}/multipart.c
    ${HTSLIB_DIR}/probaln.c
    ${HTSLIB_DIR}/realn.c
    ${HTSLIB_DIR}/regidx.c
    ${HTSLIB_DIR}/region.c
    ${HTSLIB_DIR}/sam.c
    ${HTSLIB_DIR}/sam_mods.c
    ${HTSLIB_DIR}/simd.c
    ${HTSLIB_DIR}/synced_bcf_reader.c
    ${HTSLIB_DIR}/vcf_sweep.c
    ${HTSLIB_DIR}/tbx.c
    ${HTSLIB_DIR}/textutils.c
    ${HTSLIB_DIR}/thread_pool.c
    ${HTSLIB_DIR}/vcf.c
    ${HTSLIB_DIR}/vcfutils.c
    # CRAM
    ${HTSLIB_DIR}/cram/cram_codecs.c
    ${HTSLIB_DIR}/cram/cram_decode.c
    ${HTSLIB_DIR}/cram/cram_encode.c
    ${HTSLIB_DIR}/cram/cram_external.c
    ${HTSLIB_DIR}/cram/cram_index.c
    ${HTSLIB_DIR}/cram/cram_io.c
    ${HTSLIB_DIR}/cram/cram_stats.c
    ${HTSLIB_DIR}/cram/mFILE.c
    ${HTSLIB_DIR}/cram/open_trace_file.c
    ${HTSLIB_DIR}/cram/pooled_alloc.c
    ${HTSLIB_DIR}/cram/string_alloc.c
    # htscodecs (bundled)
    ${HTSLIB_DIR}/htscodecs/htscodecs/arith_dynamic.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/fqzcomp_qual.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/htscodecs.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/pack.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/rANS_static4x16pr.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/rANS_static32x16pr.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/rANS_static32x16pr_avx2.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/rANS_static32x16pr_avx512.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/rANS_static32x16pr_sse4.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/rANS_static32x16pr_neon.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/rANS_static.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/rle.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/tokenise_name3.c
    ${HTSLIB_DIR}/htscodecs/htscodecs/utils.c
)

add_library(hts STATIC ${HTSLIB_SOURCES})
target_include_directories(hts PUBLIC ${HTSLIB_DIR} ${HTSLIB_DIR}/htslib ${HTSLIB_DIR}/htscodecs)
target_compile_definitions(hts PRIVATE SAMTOOLS=1)
target_link_libraries(hts PRIVATE ZLIB::ZLIB)
if(STAR_USE_LIBDEFLATE)
    find_path(STAR_LIBDEFLATE_INCLUDE_DIR libdeflate.h)
    find_library(STAR_LIBDEFLATE_LIBRARY NAMES deflate libdeflate)
    if(NOT EXISTS "${STAR_LIBDEFLATE_INCLUDE_DIR}/libdeflate.h" OR NOT EXISTS "${STAR_LIBDEFLATE_LIBRARY}")
        message(FATAL_ERROR "STAR_USE_LIBDEFLATE requires libdeflate headers and library")
    endif()
    target_include_directories(hts PRIVATE ${STAR_LIBDEFLATE_INCLUDE_DIR})
    target_compile_definitions(hts PRIVATE HAVE_LIBDEFLATE=1)
    target_link_libraries(hts PRIVATE ${STAR_LIBDEFLATE_LIBRARY})
    message(STATUS "Bundled HTSlib libdeflate backend: ${STAR_LIBDEFLATE_LIBRARY}")
endif()
if(DEFINED ZLIB_INCLUDE_DIRS)
    target_include_directories(hts PRIVATE ${ZLIB_INCLUDE_DIRS})
endif()

if(MSVC)
    # Force-include win32_compat.h before every htslib C source
    target_compile_options(hts PRIVATE /FI${HTSLIB_DIR}/win32_compat.h)
    target_compile_options(hts PRIVATE /W0) # suppress warnings in third-party code
    target_compile_definitions(hts PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE _USE_MATH_DEFINES)
    # Stub headers for POSIX headers that don't exist on Windows
    target_include_directories(hts PRIVATE ${HTSLIB_DIR}/win32_stubs)
else()
    target_compile_options(hts PRIVATE -w)
endif()

endif() # USE_SYSTEM_HTSLIB

# ---------- STAR sources ----------
set(STAR_SOURCES
    systemFunctions.cpp
    funPrimaryAlignMark.cpp
    SoloFeature_collapseUMI_Graph.cpp
    SoloFeature_collapseUMIall.cpp
    ParametersClip_initialize.cpp
    ClipMate_clip.cpp
    ClipCR4.cpp
    ClipMate_clipChunk.cpp
    ClipMate_initialize.cpp
    SoloFeature_loadRawMatrix.cpp
    SoloFeature_emptyDrops_CR.cpp
    soloInputFeatureUMI.cpp
    SoloFeature_countSmartSeq.cpp
    SoloFeature_redistributeReadsByCB.cpp
    SoloFeature_quantTranscript.cpp
    SoloFeature_sumThreads.cpp
    SoloFeature_countVelocyto.cpp
    SoloFeature_countCBgeneUMI.cpp
    Transcriptome_classifyAlign.cpp
    Transcriptome_geneFullAlignOverlap_ExonOverIntron.cpp
    Transcriptome_alignExonOverlap.cpp
    SoloFeature_cellFiltering.cpp
    SoloFeature_statsOutput.cpp
    bamSortByCoordinate.cpp
    SoloBarcode.cpp
    ParametersSolo.cpp
    SoloRead.cpp
    SoloRead_record.cpp
    SoloReadBarcode.cpp
    SoloReadBarcode_getCBandUMI.cpp
    SoloBarcode_extractBarcode.cpp
    SoloReadFeature.cpp
    SoloReadFeature_record.cpp
    SoloReadFeature_inputRecords.cpp
    Solo.cpp
    SoloFeature.cpp
    SoloFeature_outputResults.cpp
    SoloFeature_processRecords.cpp
    SoloFeature_addBAMtags.cpp
    ReadAlign_transformGenome.cpp
    Genome_transformGenome.cpp
    Transcript_convertGenomeCigar.cpp
    twoPassRunPass1.cpp
    samHeaders.cpp
    Genome_genomeLoad.cpp
    Genome_genomeOutLoad.cpp
    Transcript_transformGenome.cpp
    ReadAlign_outputSpliceGraphSAM.cpp
    ReadAlign_mapOneReadSpliceGraph.cpp
    SpliceGraph.cpp
    SpliceGraph_swScoreSpliced.cpp
    SpliceGraph_swTraceBack.cpp
    SpliceGraph_findSuperTr.cpp
    sjAlignSplit.cpp
    GTF.cpp
    GTF_transcriptGeneSJ.cpp
    GTF_superTranscript.cpp
    SuperTranscriptome.cpp
    ReadAlign_outputAlignments.cpp
    ReadAlign.cpp
    STAR.cpp
    SharedMemory.cpp
    PackedArray.cpp
    SuffixArrayFuns.cpp
    Parameters.cpp
    Parameters_register.cpp
    Parameters_inputSources.cpp
    Parameters_runtimeSetup.cpp
    Parameters_validateInput.cpp
    Parameters_samAttributes.cpp
    InOutStreams.cpp
    SequenceFuns.cpp
    Genome.cpp
    ParametersGenome.cpp
    Stats.cpp
    Transcript.cpp
    Transcript_alignScore.cpp
    Transcript_generateCigarP.cpp
    Chain.cpp
    Transcript_variationAdjust.cpp
    Variation.cpp
    ReadAlign_waspMap.cpp
    ReadAlign_storeAligns.cpp
    ReadAlign_stitchPieces.cpp
    ReadAlign_multMapSelect.cpp
    ReadAlign_mapOneRead.cpp
    readLoad.cpp
    ReadAlignChunk.cpp
    ReadAlignChunk_processChunks.cpp
    ReadAlignChunk_mapChunk.cpp
    OutSJ.cpp
    outputSJ.cpp
    blocksOverlap.cpp
    ThreadControl.cpp
    sysRemoveDir.cpp
    ReadAlign_maxMappableLength2strands.cpp
    binarySearch2.cpp
    ReadAlign_outputTranscriptSAM.cpp
    ReadAlign_outputTranscriptSJ.cpp
    ReadAlign_outputTranscriptCIGARp.cpp
    ReadAlign_calcCIGAR.cpp
    ReadAlign_createExtendWindowsWithAlign.cpp
    ReadAlign_assignAlignToWindow.cpp
    ReadAlign_oneRead.cpp
    ReadAlign_stitchWindowSeeds.cpp
    ReadAlign_peOverlapMergeMap.cpp
    ReadAlign_mappedFilter.cpp
    ParametersChimeric_initialize.cpp
    ReadAlign_chimericDetection.cpp
    ReadAlign_chimericDetectionOld.cpp
    ReadAlign_chimericDetectionOldOutput.cpp
    ChimericDetection.cpp
    ChimericDetection_chimericDetectionMult.cpp
    ReadAlign_chimericDetectionPEmerged.cpp
    stitchWindowAligns.cpp
    extendAlign.cpp
    stitchAlignToTranscript.cpp
    ChimericSegment.cpp
    ChimericAlign.cpp
    ChimericAlign_chimericJunctionOutput.cpp
    ChimericAlign_chimericBAMoutput.cpp
    ChimericAlign_chimericStitching.cpp
    Genome_genomeGenerate.cpp
    genomeParametersWrite.cpp
    genomeScanFastaFiles.cpp
    genomeSAindex.cpp
    Genome_insertSequences.cpp
    insertSeqSA.cpp
    funCompareUintAndSuffixes.cpp
    funCompareUintAndSuffixesMemcmp.cpp
    TimeFunctions.cpp
    ErrorWarning.cpp
    streamFuns.cpp
    stringSubstituteAll.cpp
    Transcriptome.cpp
    Transcriptome_quantAlign.cpp
    Transcriptome_geneFullAlignOverlap.cpp
    ReadAlign_quantTranscriptome.cpp
    Quantifications.cpp
    Transcriptome_geneCountsAddAlign.cpp
    sjdbLoadFromFiles.cpp
    sjdbLoadFromStream.cpp
    sjdbPrepare.cpp
    sjdbBuildIndex.cpp
    sjdbInsertJunctions.cpp
    mapThreadsSpawn.cpp
    Parameters_readFilesInit.cpp
    Parameters_openReadsFiles.cpp
    Parameters_closeReadsFiles.cpp
    Parameters_readSAMheader.cpp
    bam_cat.c  # compiled as C++ despite .c extension
    serviceFuns.cpp
    GlobalVariables.cpp
    BAMoutput.cpp
    cramOutput.cpp
    BAMfunctions.cpp
    ReadAlign_alignBAM.cpp
    BAMbinSortByCoordinate.cpp
    signalFromBAM.cpp
    bamRemoveDuplicates.cpp
    BAMbinSortUnmapped.cpp
    webui/WebUI.cpp
    webui/WebUI_report.cpp
)

# Prepend source directory
list(TRANSFORM STAR_SOURCES PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

# bam_cat.c uses C++ headers despite .c extension - compile as C++
set_source_files_properties(${CMAKE_CURRENT_SOURCE_DIR}/bam_cat.c PROPERTIES LANGUAGE CXX)

add_executable(STAR ${STAR_SOURCES})
add_dependencies(STAR generate_xxd)

target_include_directories(STAR PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}  # for parametersDefault.xxd
    ${HTSLIB_DIR}
)

# SYSTEM suppresses third-party compiler warnings. It is not a substitute for
# auditing dependency vulnerabilities or documenting individual CodeQL alerts.
target_include_directories(STAR SYSTEM PRIVATE
    ${httplib_SOURCE_DIR}                # httplib.h (cpp-httplib)
    ${nlohmann_json_SOURCE_DIR}/include  # nlohmann/json.hpp
)

target_link_libraries(STAR PRIVATE hts ZLIB::ZLIB parasail star_sjdb_remap)

# ---------- Compile-time defines ----------
# Build date/place
if(DEFINED ENV{SOURCE_DATE_EPOCH})
    string(TIMESTAMP BUILD_DATE "%Y-%m-%dT%H:%M:%S" UTC)
else()
    set(BUILD_DATE "unspecified")
endif()

target_compile_definitions(STAR PRIVATE
    "COMPILATION_TIME_PLACE=\"${BUILD_DATE}\""
    "GIT_BRANCH_COMMIT_DIFF=\"${GIT_COMMIT_HASH}\""
)
star_target_build_options(STAR)
star_target_sanitizers(STAR)
if(NOT USE_SYSTEM_HTSLIB)
    star_target_sanitizers(hts)
endif()
star_target_sanitizers(star_sjdb_remap)

if(STAR_LONG_READS)
    target_compile_definitions(STAR PRIVATE COMPILE_FOR_LONG_READS)
endif()
if(STAR_POSIX_SHARED_MEM)
    target_compile_definitions(STAR PRIVATE POSIX_SHARED_MEM)
endif()

# ---------- Platform-specific settings ----------
if(MSVC)
    target_compile_definitions(STAR PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _CRT_NONSTDC_NO_DEPRECATE
    )

    # OpenMP
    find_package(OpenMP REQUIRED)
    target_link_libraries(STAR PRIVATE OpenMP::OpenMP_CXX)

    # For clang-cl: track OpenMP runtime DLL for packaging
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        get_filename_component(_CLANG_BIN_DIR ${CMAKE_CXX_COMPILER} DIRECTORY)
        set(STAR_RUNTIME_DLLS "${_CLANG_BIN_DIR}/libomp.dll" CACHE INTERNAL "")
    endif()

    # AVX2 SIMD
    if(STAR_USE_AVX2)
        target_compile_options(STAR PRIVATE /arch:AVX2)
    endif()

    # General MSVC-compatible flags (work on both cl.exe and clang-cl)
    target_compile_options(STAR PRIVATE
        /W4 /EHsc /bigobj
        /wd4244 /wd4267 /wd4100 /wd4389 /wd4018 /wd4456 /wd4457  # suppress noisy warnings in upstream code
    )

    # Flags specific to cl.exe (not supported by clang-cl)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(STAR PRIVATE /MP)      # Parallel compilation (Ninja already parallelizes)
        target_compile_options(STAR PRIVATE $<$<NOT:$<CONFIG:Debug>>:/GL>)
        target_link_options(STAR PRIVATE $<$<NOT:$<CONFIG:Debug>>:/LTCG>)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_compile_options(STAR PRIVATE $<$<NOT:$<CONFIG:Debug>>:-flto=thin>)
        target_link_options(STAR PRIVATE $<$<NOT:$<CONFIG:Debug>>:-flto=thin>)
    endif()

    # Always generate PDB for debugging crash dumps
    target_compile_options(STAR PRIVATE /Zi)
    target_link_options(STAR PRIVATE /DEBUG)


    # Link Windows libraries needed by wincompat.h
    target_link_libraries(STAR PRIVATE psapi ws2_32)

    # Increase stack size to 16MB (STAR uses large stack arrays for read processing)
    # Linux default is 8MB; STAR with STARsolo needs more
    target_link_options(STAR PRIVATE /STACK:16777216)

else()
    # GCC / Clang
    target_compile_options(STAR PRIVATE -pipe -Wall -Wextra -Wno-register)

    find_package(OpenMP REQUIRED)
    target_link_libraries(STAR PRIVATE OpenMP::OpenMP_CXX)

    if(STAR_USE_AVX2)
        target_compile_options(STAR PRIVATE -mavx2)
    endif()

    target_link_libraries(STAR PRIVATE pthread)

endif()

# ---------- Testing ----------
enable_testing()
add_test(NAME version_check COMMAND STAR --version)
set_tests_properties(version_check PROPERTIES
    PASS_REGULAR_EXPRESSION "^STAR-Cross ${GIT_COMMIT_HASH}[\r\n]*$")

option(STAR_BUILD_TESTS "Build unit tests (requires network access for doctest)" ON)
if(STAR_BUILD_TESTS)
    set(STAR_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    add_subdirectory(${CMAKE_SOURCE_DIR}/../test ${CMAKE_BINARY_DIR}/test)
endif()

# ---------- Install & Packaging ----------
install(TARGETS STAR RUNTIME DESTINATION bin COMPONENT STAR)
if(USE_SYSTEM_HTSLIB)
    set(STAR_HTSLIB_VERSION "${HTS_VERSION}")
else()
    set(STAR_HTSLIB_VERSION "1.24")
endif()
if(NOT ZLIB_VERSION_STRING)
    set(ZLIB_VERSION_STRING "1.3.2")
endif()
set(STAR_SOURCE_TREE_CLEAN "null")
if(GIT_FOUND)
    execute_process(COMMAND ${GIT_EXECUTABLE} status --porcelain --untracked-files=normal
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR} OUTPUT_VARIABLE STAR_TREE_STATUS
        RESULT_VARIABLE STAR_TREE_STATUS_RESULT ERROR_QUIET OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(STAR_TREE_STATUS_RESULT EQUAL 0)
        if(STAR_TREE_STATUS STREQUAL "")
            set(STAR_SOURCE_TREE_CLEAN "true")
        else()
            set(STAR_SOURCE_TREE_CLEAN "false")
        endif()
    endif()
endif()
file(SHA256 "${CMAKE_CURRENT_SOURCE_DIR}/../dependencies.lock.json" STAR_DEPENDENCY_LOCK_SHA256)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/STAR-build-info-$<CONFIG>.json"
    CONTENT "{\n  \"commit\": \"${GIT_COMMIT_HASH}\",\n  \"source_tree_clean\": ${STAR_SOURCE_TREE_CLEAN},\n  \"dependency_lock_sha256\": \"${STAR_DEPENDENCY_LOCK_SHA256}\",\n  \"configuration\": \"$<CONFIG>\",\n  \"compiler\": \"${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}\",\n  \"cxx_standard\": ${STAR_CXX_STANDARD},\n  \"avx2\": \"${STAR_USE_AVX2}\",\n  \"asan\": \"${STAR_ASAN}\",\n  \"ubsan\": \"${STAR_UBSAN}\",\n  \"htslib\": \"${STAR_HTSLIB_VERSION}\",\n  \"zlib\": \"${ZLIB_VERSION_STRING}\",\n  \"parasail\": \"2.6.2\",\n  \"httplib\": \"0.60.0\",\n  \"json\": \"3.12.0\"\n}\n")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/STAR-build-info-$<CONFIG>.json" DESTINATION share/STAR-Cross COMPONENT STAR)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../LICENSE" DESTINATION share/STAR-Cross/licenses RENAME STAR.txt COMPONENT STAR)
if(NOT USE_SYSTEM_HTSLIB)
    install(FILES "${HTSLIB_DIR}/LICENSE" DESTINATION share/STAR-Cross/licenses RENAME HTSlib.txt COMPONENT STAR)
    install(FILES "${HTSLIB_DIR}/htscodecs/LICENSE.md" DESTINATION share/STAR-Cross/licenses RENAME htscodecs.txt COMPONENT STAR)
    install(FILES "${HTSLIB_DIR}/os/rand.c" DESTINATION share/STAR-Cross/licenses RENAME HTSlib-rand-notice.c COMPONENT STAR)
endif()
install(FILES "${parasail_SOURCE_DIR}/LICENSE.md" DESTINATION share/STAR-Cross/licenses RENAME Parasail.txt COMPONENT STAR)
install(FILES "${parasail_SOURCE_DIR}/contrib/simde/COPYING" DESTINATION share/STAR-Cross/licenses RENAME SIMDe.txt COMPONENT STAR)
install(FILES "${parasail_SOURCE_DIR}/contrib/sais-lite-lcp/COPYING" DESTINATION share/STAR-Cross/licenses RENAME sais.txt COMPONENT STAR)
install(FILES "${httplib_SOURCE_DIR}/LICENSE" DESTINATION share/STAR-Cross/licenses RENAME cpp-httplib.txt COMPONENT STAR)
install(FILES "${nlohmann_json_SOURCE_DIR}/LICENSE.MIT" DESTINATION share/STAR-Cross/licenses RENAME nlohmann-json.txt COMPONENT STAR)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/../dependencies.lock.json" DESTINATION share/STAR-Cross COMPONENT STAR)
find_file(STAR_ZLIB_NOTICE zlib.h HINTS ${ZLIB_INCLUDE_DIRS} ${ZLIB_INCLUDE_DIR})
if(STAR_ZLIB_NOTICE)
    # zlib.h contains the actual selected distribution's complete license notice.
    install(FILES "${STAR_ZLIB_NOTICE}" DESTINATION share/STAR-Cross/licenses RENAME zlib-notice.h COMPONENT STAR)
endif()

# Bundle runtime DLLs alongside the binary for Windows release packaging
if(WIN32)
    # Copy runtime DLLs next to STAR.exe after build (for local testing)
    add_custom_command(TARGET STAR POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Bundling runtime DLLs..."
    )

    # clang-cl OpenMP runtime
    if(DEFINED STAR_RUNTIME_DLLS)
        foreach(_dll ${STAR_RUNTIME_DLLS})
            if(EXISTS "${_dll}")
                add_custom_command(TARGET STAR POST_BUILD
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${_dll}" "$<TARGET_FILE_DIR:STAR>"
                    COMMENT "Copying ${_dll}"
                )
                install(FILES "${_dll}" DESTINATION bin COMPONENT STAR)
            endif()
        endforeach()
    endif()

    # MSVC runtime — use CMake's built-in InstallRequiredSystemLibraries
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_COMPONENT STAR)
    include(InstallRequiredSystemLibraries)
endif()

message(STATUS "STAR-Cross ${GIT_COMMIT_HASH} build configuration:")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Long reads: ${STAR_LONG_READS}")
message(STATUS "  AVX2 SIMD: ${STAR_USE_AVX2}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
