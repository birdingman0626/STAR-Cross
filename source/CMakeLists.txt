cmake_minimum_required(VERSION 3.15)
project(STAR VERSION 2.7.11 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Export compile_commands.json for LSP/IDE support (clangd, ccls)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ---------- Options ----------
option(STAR_LONG_READS "Build STARlong variant for long reads" OFF)
option(STAR_USE_AVX2 "Enable AVX2 SIMD instructions" ON)
option(STAR_FAST_MODE "Aggressive optimizations (may change alignment output)" OFF)
option(STAR_ASAN "Enable AddressSanitizer" OFF)

# ---------- Find or build zlib ----------
find_package(ZLIB QUIET)
if(NOT ZLIB_FOUND)
    message(STATUS "System zlib not found, building from source via FetchContent")
    include(FetchContent)
    set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
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

# ---------- htslib sources ----------
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

# ---------- STAR sources ----------
set(STAR_SOURCES
    systemFunctions.cpp
    funPrimaryAlignMark.cpp
    SoloFeature_collapseUMI_Graph.cpp
    SoloFeature_collapseUMIall.cpp
    ParametersClip_initialize.cpp
    ClipMate_clip.cpp
    ClipCR4.cpp
    opal/opal.cpp
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
    BAMfunctions.cpp
    ReadAlign_alignBAM.cpp
    BAMbinSortByCoordinate.cpp
    signalFromBAM.cpp
    bamRemoveDuplicates.cpp
    BAMbinSortUnmapped.cpp
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

target_link_libraries(STAR PRIVATE hts ZLIB::ZLIB)

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

if(STAR_FAST_MODE)
    target_compile_definitions(STAR PRIVATE STAR_FAST_MODE)
    set_target_properties(STAR PROPERTIES OUTPUT_NAME "STARfast")
    message(STATUS "  Fast mode: ON -> binary: STARfast")
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

    # AVX2 SIMD
    if(STAR_USE_AVX2)
        target_compile_options(STAR PRIVATE /arch:AVX2)
    endif()

    # General MSVC flags
    target_compile_options(STAR PRIVATE
        /W4 /MP /EHsc /bigobj
        /wd4244 /wd4267 /wd4100 /wd4389 /wd4018 /wd4456 /wd4457  # suppress noisy warnings in upstream code
    )
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        target_compile_options(STAR PRIVATE /O2 /Ob2 /Oi)
        target_compile_options(STAR PRIVATE /GL)      # Whole-program optimization
        target_link_options(STAR PRIVATE /LTCG)       # Link-time code generation
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

    # opal uses SIMDe which needs AVX2 and local includes
    set_source_files_properties(
        ${CMAKE_CURRENT_SOURCE_DIR}/opal/opal.cpp
        PROPERTIES COMPILE_FLAGS "/arch:AVX2 /I${CMAKE_CURRENT_SOURCE_DIR}/opal"
    )

else()
    # GCC / Clang
    target_compile_options(STAR PRIVATE -pipe -Wall -Wextra -O3 -Wno-register)

    find_package(OpenMP REQUIRED)
    target_link_libraries(STAR PRIVATE OpenMP::OpenMP_CXX)

    if(STAR_USE_AVX2)
        target_compile_options(STAR PRIVATE -mavx2)
        set_source_files_properties(
            ${CMAKE_CURRENT_SOURCE_DIR}/opal/opal.cpp
            PROPERTIES COMPILE_FLAGS "-mavx2"
        )
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
set_tests_properties(version_check PROPERTIES PASS_REGULAR_EXPRESSION "2\\.7\\.11b")

# ---------- Install ----------
install(TARGETS STAR RUNTIME DESTINATION bin)

message(STATUS "STAR ${PROJECT_VERSION} build configuration:")
message(STATUS "  Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "  Long reads: ${STAR_LONG_READS}")
message(STATUS "  AVX2 SIMD: ${STAR_USE_AVX2}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
