#ifndef STAR_GPU_SEED_SEARCH_H
#define STAR_GPU_SEED_SEARCH_H
#include <cstdint>
#include <cstddef>
#include <vector>

// Isolated experimental search adapter. No alignment/candidate selection changes.
struct GpuSeedQuery {
    uint64_t offset, readBytes, start, length, lower, upper, initialLength, forward;
};
struct GpuSeedMatch {uint64_t length, lower, upper, multiplicity;};
struct GpuSeedTiming {double upload=0, kernel=0, download=0;};
class GpuSeedIndex {
public:
    GpuSeedIndex(const char* genome, uint64_t genomeBytes, const char* sa,
                 uint64_t saBytes, uint64_t records, unsigned strandBit,
                 size_t batchCapacity=65536, size_t readCapacity=16*1024*1024);
    ~GpuSeedIndex();
    GpuSeedIndex(const GpuSeedIndex&)=delete;
    GpuSeedIndex& operator=(const GpuSeedIndex&)=delete;
    std::vector<GpuSeedMatch> search(const std::vector<char>& reads,
        const std::vector<GpuSeedQuery>& queries, GpuSeedTiming& timing);
    double indexUploadSeconds=0;
    uint64_t deviceBytes=0;
private:
    struct State;
    State* state;
};
#endif
