#ifndef STAR_TRANSCRIPTOME_PRIMARY_H
#define STAR_TRANSCRIPTOME_PRIMARY_H
#include <cstdint>

// --quantTranscriptomeBAMprimary ReadIndex: picks which of a read's nAlign
// transcriptomic alignments is primary from the run seed and the read's number
// in the input only, so the choice does not depend on the mapping thread.
inline uint64_t transcriptomePrimaryByReadIndex(int runRNGseed, uint64_t iReadAll, uint64_t nAlign) {
    if (nAlign <= 1)
        return 0;
    // SplitMix64 finalizer over (seed, read number)
    uint64_t z = iReadAll + 0x9E3779B97F4A7C15ULL * ((uint64_t)(uint32_t)runRNGseed + 1);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    z ^= z >> 31;
    // Scale the top 32 bits to [0, nAlign); nAlign is far below 2^32
    return (uint64_t)(((z >> 32) * (nAlign & 0xFFFFFFFFULL)) >> 32);
}
#endif
