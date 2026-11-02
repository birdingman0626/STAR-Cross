#ifndef STAR_GPU_STITCH_H
#define STAR_GPU_STITCH_H
// Device stitching of collected alignment windows (StitchCore on CUDA).
// One thread per read keeps the read's window order, maxScoreMate and
// transcript-capacity state; reads whose work exceeds a bounded resource are
// reported with a non-zero status and stitched again on the CPU.
#include "StitchBatch.h"
#include <cstddef>
#include <cstdint>
#include <vector>

struct GpuStitchGenome {
    const void* deviceGenome=nullptr;      // genome bytes already resident on the device
    uint64_t nGenome=0;
    std::vector<uint64_t> chrStart, chrLength;
    uint64_t sjdbN=0;
    const uint64_t *sjdbStart=nullptr, *sjdbEnd=nullptr;
    const unsigned char *sjdbMotif=nullptr, *sjdbShiftLeft=nullptr, *sjdbShiftRight=nullptr, *sjdbStrand=nullptr;
};
struct GpuStitchStats {
    uint64_t batches=0, reads=0, windows=0, transcripts=0, rows=0, fallbacks=0, uploadBytes=0, downloadBytes=0;
    uint64_t stackOverflow=0, slotOverflow=0, outputOverflow=0;  // fallback reasons (a read may have several)
    double seconds=0, kernelSeconds=0;
};
class GpuStitcher {
public:
    GpuStitcher(const GpuStitchGenome& genome, const stitchcore::Params& params, size_t slots,
                unsigned framesPerRead, unsigned slotsPerRead=32);
    ~GpuStitcher();
    GpuStitcher(const GpuStitcher&)=delete;
    GpuStitcher& operator=(const GpuStitcher&)=delete;
    // Fills results.reads/windows/transcripts for every read of the batch.
    void run(const StitchBatch& batch, StitchResults& results, size_t slot);
    std::vector<GpuStitchStats> stats;
    uint64_t deviceBytes=0;
private:
    struct State;
    State* state;
};
#endif
