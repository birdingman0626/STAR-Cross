#ifndef STAR_ADAPTIVE_SEED_TYPES_H
#define STAR_ADAPTIVE_SEED_TYPES_H
#include "SeedSearchTypes.h"

// Plain host/device interchange types; no coroutine, STL container or owner.
struct SeedEmission {
    unsigned long long direction, start, fragment;
    SeedMatch match;
};
struct SeedRoundConfig {
    unsigned long long maxSplits, minSplit, minMap, startMax, fixedLength;
    double startFraction;
    unsigned long long maxMultiplicity=10000, maxSeeds=1000;
};
// Same counters as resetN/storeAligns, including counts before duplicate removal.
struct SeedSummary {
    unsigned long long splits=0, longestSplit=0, pieces=0, alignments=0, unique=0, multiple=0;
    unsigned long long uniqLmax=0, uniqLmaxInd=0, multLmax=0, multLmaxN=0;
    unsigned long long multNmin=0, multNminL=0, multNmax=0, multNmaxL=0;
};
struct SeedReadRange { uint64_t offset, length; };
// Positive-genome coordinates, final strand before revertStrand, and oriented
// read start. One record per SA hit, in the seed's original SA order.
// Read coordinates fit the bounded read pool; genome coordinates remain 64-bit.
struct SeedProjectedHit { uint64_t genome; uint32_t strand, readStart; };
static_assert(sizeof(SeedProjectedHit)==16,"unexpected projected-hit padding");
#endif
