#ifndef STAR_SEED_HIT_BATCH_H
#define STAR_SEED_HIT_BATCH_H
#include "AdaptiveSeedTypes.h"
#include <vector>
#include <cstddef>

struct SeedHitBatch {
    std::vector<std::size_t> offsets;          // first hit of each piece
    std::vector<std::size_t> counts;           // hits retained per piece (all, unless window-filtered)
    std::vector<SeedProjectedHit> hits;
    std::size_t expanded=0;                    // hits expanded on the device before filtering
};
#endif
