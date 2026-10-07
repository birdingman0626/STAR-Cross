# Bounded compatibility/test changes to the hash-verified Parasail 2.6.2 source.
file(READ "${PARASAIL_SOURCE}/CMakeLists.txt" contents)
string(REPLACE "\${CMAKE_SOURCE_DIR}/data/test_small_2.fasta"
    "\${CMAKE_CURRENT_SOURCE_DIR}/data/test_small_2.fasta" contents "${contents}")
if(NOT contents MATCHES "STAR_CROSS_TEST_LIMIT")
    string(APPEND contents "\n# STAR_CROSS_TEST_LIMIT: avoid tiny-fixture OpenMP oversubscription\nset_tests_properties(test_verify PROPERTIES ENVIRONMENT \"OMP_NUM_THREADS=2\" TIMEOUT 120)\n")
endif()
file(WRITE "${PARASAIL_SOURCE}/CMakeLists.txt" "${contents}")
file(READ "${PARASAIL_SOURCE}/src/cpuid.c" contents)
if(NOT contents MATCHES "__s390x__")
    if(NOT contents MATCHES "defined\\(__PPC64__\\)")
        message(FATAL_ERROR "Parasail CPUID patch context changed")
    endif()
    string(REPLACE "defined(__PPC64__)"
        "defined(__PPC64__) || defined(__s390__) || defined(__s390x__)" contents "${contents}")
    file(WRITE "${PARASAIL_SOURCE}/src/cpuid.c" "${contents}")
endif()
