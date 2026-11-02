#ifndef STAR_GPU_BARCODE_H
#define STAR_GPU_BARCODE_H
#include <cstddef>
#include <cstdint>
#include <memory>

// Cell-barcode whitelist lookups on the device: exact match, and on a miss the set of
// one-mismatch neighbours present in the whitelist. Same search as binarySearchExact.
class GpuWhitelist {
public:
    static constexpr unsigned kMaxLength=16; // the CPU 1MM loop shifts a 32-bit value; longer barcodes stay on the CPU
    // Whitelist and keys are STAR uint64 (unsigned long long) arrays, not uint64_t (unsigned long on LP64 Linux).
    GpuWhitelist(const unsigned long long* whitelist, std::size_t n, unsigned length, std::size_t slots);
    ~GpuWhitelist();
    // exact[i]: whitelist index or -1; mask[i]: neighbour bits when exact[i]==-1, else 0.
    void lookup(const unsigned long long* keys, std::size_t n, int64_t* exact, uint64_t* mask, unsigned slot);
    // The same lookup in two halves, so other device work can be queued in between:
    // submit() queues it on the slot's stream and returns; collect() waits and writes the results.
    void submit(const unsigned long long* keys, std::size_t n, unsigned slot);
    void collect(int64_t* exact, uint64_t* mask, unsigned slot);
    void totals(double& seconds, uint64_t& reads) const;
    std::size_t deviceBytes=0;
private:
    struct State;
    std::unique_ptr<State> state;
};
#endif
