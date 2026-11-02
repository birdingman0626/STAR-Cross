#ifndef STAR_GPU_SEED_SEARCH_H
#define STAR_GPU_SEED_SEARCH_H
#include <cstdint>
#include <cstddef>
#include <vector>
#include "SeedSearchTypes.h"
#include "AdaptiveSeedTypes.h"
#include "SeedHitBatch.h"

// Isolated experimental search adapter. No alignment/candidate selection changes.
using GpuSeedQuery = SeedQuery;
using GpuSeedMatch = SeedMatch;
struct GpuSeedTiming {double upload=0, kernel=0, download=0;};
struct GpuAdaptiveSeeds {
    std::vector<std::vector<SeedEmission>> reads;
    std::vector<SeedSummary> summaries;
    std::vector<SeedHitBatch> projected;
    uint64_t searches=0;
    uint64_t generation=0;
};
class GpuSeedIndex {
public:
    GpuSeedIndex(const char* genome, uint64_t genomeBytes, const char* sa,
                 uint64_t saBytes, uint64_t records, unsigned strandBit,
                 size_t batchCapacity=65536, size_t readCapacity=16*1024*1024, size_t slots=1);
    ~GpuSeedIndex();
    GpuSeedIndex(const GpuSeedIndex&)=delete;
    GpuSeedIndex& operator=(const GpuSeedIndex&)=delete;
    std::vector<GpuSeedMatch> search(const std::vector<char>& reads,
        const std::vector<GpuSeedQuery>& queries, GpuSeedTiming& timing);
    // A slot has exactly one worker. Upload once, then reuse across adaptive rounds.
    uint64_t uploadReads(const std::vector<char>& reads, size_t slot=0);
    std::vector<GpuSeedMatch> searchPrepared(const std::vector<GpuSeedQuery>& queries,
        GpuSeedTiming& timing, size_t slot, uint64_t generation);
    // Configure once before workers start. Full adaptive searches then remain
    // on the device until the ordered seed lists for a read batch are complete.
    void configureAdaptive(const char* prefixes, uint64_t prefixBytes, uint64_t prefixRecords,
        unsigned prefixBits, const unsigned long long* starts, unsigned prefixBases,
        uint64_t sparse, uint64_t absentFlag, uint64_t nFlag, uint64_t nMask,
        SeedRoundConfig config, size_t eventCapacity=131072, size_t hitCapacity=262144);
    GpuAdaptiveSeeds seedReads(const std::vector<char>& reads,
        const std::vector<SeedReadRange>& ranges, size_t slot, bool deferProjection=false);
    // Optional: drop hits that cannot reach any alignment window of their read
    // (see gpuSeedHits.cuh). Junction arrays are copied; sjdbN may be zero.
    void configureWindowFilter(const uint64_t* sjDstart, const uint64_t* sjAstart, uint64_t sjdbN,
        uint64_t sjGstart, uint64_t sjdbLength, uint64_t sjdbOverhang, unsigned winBinNbits,
        uint64_t winAnchorDistNbins, uint64_t winFlankNbins, uint64_t winAnchorMultimapNmax);
    // Consume a bounded prefix of the remaining reads; seeds stay in this slot.
    std::vector<SeedHitBatch> projectPrepared(size_t firstRead,size_t slot,uint64_t generation,bool partial=true);
    const void* deviceGenome() const;  // resident genome bytes (genomeBytes long)
    double indexUploadSeconds=0;
    uint64_t deviceBytes=0;
    uint64_t pinnedBytes=0;
private:
    std::vector<SeedHitBatch> filterProjected(size_t first,size_t end,size_t slot,unsigned taskCount,
        uint64_t hits,std::vector<SeedHitBatch> result);
    struct State;
    State* state;
};
#endif
