#ifndef STAR_GPU_CLIP_H
#define STAR_GPU_CLIP_H
#include <cstddef>
#include <cstdint>
#include <memory>

// CellRanger4 5' adapter clipping on the device: the same semi-global affine
// alignment as ClipCR4::align, one read per thread, in exact int32 arithmetic.
struct GpuClipScoring {
    unsigned readLen=0;   // target bases per read (shorter reads are padded with N=4)
    int match=0, mismatch=0, gapOpen=0, gapExt=0;
};

class GpuClipper {
public:
    static constexpr unsigned kMaxQuery=128;
    explicit GpuClipper(std::size_t slots);
    ~GpuClipper();
    // codes: n*scoring.readLen base codes, read-major. out: 2*n ints, (score, endLocationTarget) per read.
    void align(const GpuClipScoring& scoring, const uint8_t* codes, std::size_t n,
               const uint8_t* query, unsigned queryLen, int* out, unsigned slot);
    void totals(double& seconds, uint64_t& reads) const;
private:
    struct State;
    std::unique_ptr<State> state;
};
#endif
