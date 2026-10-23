#ifndef STAR_SEED_INDEX_VIEW_H
#define STAR_SEED_INDEX_VIEW_H
#include "PackedArray.h"

// Borrowed, read-only view of one effective index. Construct again after index
// insertion/reload; the owner and its arrays must outlive every search.
struct SeedIndexView {
    const char* G;
    const PackedArray& SA;
    uint64 nGenome;
    unsigned char GstrandBit;
    uint64 GstrandMask;
    uint64 nSA;
};
#endif
