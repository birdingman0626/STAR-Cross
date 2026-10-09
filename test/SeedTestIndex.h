#ifndef STAR_SEED_TEST_INDEX_H
#define STAR_SEED_TEST_INDEX_H
#include "SeedIndexView.h"
// Test/replay storage, independent of Parameters, streams and Genome ownership.
struct SeedTestIndex {
    const char* G=nullptr;
    PackedArray SA;
    uint nGenome=0, GstrandMask=0, nSA=0;
    unsigned char GstrandBit=0;
    operator SeedIndexView() const { return {G,SA,nGenome,GstrandBit,GstrandMask,nSA}; }
};
#endif
