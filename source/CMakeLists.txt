cmake_minimum_required(VERSION 3.15)
project(STAR VERSION 2.7.11 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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
# Override STAR_VERSION at compile time: "2.7.11c_<hash>"
add_compile_definitions(STAR_VERSION="2.7.11c_${GIT_COMMIT_HASH}")

# ---------- Options ----------
option(STAR_LONG_READS "Build STARlong variant for long reads" OFF)
# AVX2 is x86-only; auto-disable on ARM/Apple Silicon
if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
    option(STAR_USE_AVX2 "Enable AVX2 SIMD instructions" OFF)
else()
    option(STAR_USE_AVX2 "Enable AVX2 SIMD instructions" ON)
endif()
option(STAR_ASAN "Enable AddressSanitizer" OFF)

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
        GIT_REPOSITORY https://github.com/madler/zlib.git
        GIT_TAG v1.3.2
        GIT_SHALLOW TRUE
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
    GIT_REPOSITORY https://github.com/jeffdaily/parasail.git
    GIT_TAG v2.6.2
    GIT_SHALLOW TRUE
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

# Parasail's test_verify registers a CTest entry using ${CMAKE_SOURCE_DIR}/data/... which,
# under FetchContent, resolves to the STAR source dir rather than the parasail source dir.
# Copy the required test fixture to the location the test expects.
if(EXISTS "${parasail_SOURCE_DIR}/data/test_small_2.fasta")
    file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/data")
    file(COPY "${parasail_SOURCE_DIR}/data/test_small_2.fasta"
         DESTINATION "${CMAKE_SOURCE_DIR}/data")
endif()

# ---------- cpp-httplib (header-only HTTP server) ----------
FetchContent_Declare(httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.20.0
    GIT_SHALLOW    TRUE
)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)   # keep header-only, no separate lib
set(HTTPLIB_REQUIRE_OPENSSL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# ---------- nlohmann/json (header-only JSON) ----------
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
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
    # Core htslib 1.21
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
    ${httplib_SOURCE_DIR}        # httplib.h
)

target_include_directories(STAR SYSTEM PRIVATE
    ${nlohmann_json_SOURCE_DIR}/include  # nlohmann/json.hpp
)

target_link_libraries(STAR PRIVATE hts ZLIB::ZLIB parasail)

# ---------- Compile-time defines ----------
# Build date/place
string(TIMESTAMP BUILD_DATE "%Y-%m-%dT%H:%M:%S" UTC)
if(DEFINED ENV{HOSTNAME})
    set(BUILD_PLACE "$ENV{HOSTNAME}")
else()
    cmake_host_system_information(RESULT BUILD_PLACE QUERY HOSTNAME)
endif()

target_compile_definitions(STAR PRIVATE
    "COMPILATION_TIME_PLACE=\"${BUILD_DATE} ${BUILD_PLACE}\""
    "GIT_BRANCH_COMMIT_DIFF=\"Windows build\""
)

if(STAR_LONG_READS)
    target_compile_definitions(STAR PRIVATE COMPILE_FOR_LONG_READS)
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
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(STAR PRIVATE /O2 /Ob2 /Oi)
    endif()

    # Flags specific to cl.exe (not supported by clang-cl)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
        target_compile_options(STAR PRIVATE /MP)      # Parallel compilation (Ninja already parallelizes)
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(STAR PRIVATE /GL)   # Whole-program optimization
            target_link_options(STAR PRIVATE /LTCG)    # Link-time code generation
        endif()
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
            target_compile_options(STAR PRIVATE -flto=thin)  # Clang LTO equivalent
        endif()
    endif()

    # Always generate PDB for debugging crash dumps
    target_compile_options(STAR PRIVATE /Zi)
    target_link_options(STAR PRIVATE /DEBUG)

    if(STAR_ASAN)
        target_compile_options(STAR PRIVATE /fsanitize=address)
        target_compile_options(hts PRIVATE /fsanitize=address)
    endif()

    # Link Windows libraries needed by wincompat.h
    target_link_libraries(STAR PRIVATE psapi ws2_32)

    # Increase stack size to 64MB (STAR uses large stack arrays for read processing)
    # Linux default is 8MB; STAR with STARsolo needs more
    target_link_options(STAR PRIVATE /STACK:16777216)

else()
    # GCC / Clang
    target_compile_options(STAR PRIVATE -pipe -Wall -Wextra -O3 -Wno-register)

    find_package(OpenMP REQUIRED)
    target_link_libraries(STAR PRIVATE OpenMP::OpenMP_CXX)

    if(STAR_USE_AVX2)
        target_compile_options(STAR PRIVATE -mavx2)
    endif()

    target_link_libraries(STAR PRIVATE pthread)

    # Sanitizer support (GCC/Clang)
    if(STAR_ASAN)
        target_compile_options(STAR PRIVATE -fsanitize=address -fno-omit-frame-pointer)
        target_link_options(STAR PRIVATE -fsanitize=address)
    endif()
endif()

# ---------- Testing ----------
enable_testing()
add_test(NAME version_check COMMAND STAR --version)
set_tests_properties(version_check PROPERTIES PASS_REGULAR_EXPRESSION "2\\.7\\.11")

option(STAR_BUILD_TESTS "Build unit tests (requires network access for doctest)" ON)
if(STAR_BUILD_TESTS)
    set(STAR_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})
    add_subdirectory(${CMAKE_SOURCE_DIR}/../test ${CMAKE_BINARY_DIR}/test)
endif()

# ---------- Install & Packaging ----------
install(TARGETS STAR RUNTIME DESTINATION bin)

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
                install(FILES "${_dll}" DESTINATION bin)
            endif()
        endforeach()
    endif()

    # MSVC runtime — use CMake's built-in InstallRequiredSystemLibraries
    include(InstallRequiredSystemLibraries)
endif()

message(STATUS "STAR ${PROJECT_VERSION} build configuration:")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Long reads: ${STAR_LONG_READS}")
message(STATUS "  AVX2 SIMD: ${STAR_USE_AVX2}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
