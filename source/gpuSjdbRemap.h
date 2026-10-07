#ifndef STAR_GPU_SJDB_REMAP_H
#define STAR_GPU_SJDB_REMAP_H
#include <cstddef>
#include <cstdint>
#include <string>

// First experiment only: no new suffixes/strand-width changes. Existing junction
// permutations are supported. Output words have exclusive ownership (no packed
// read-modify-write race); scientific SA ordering is never changed.
struct GpuSjdbRemapRequest {
    const char* input;
    char* output;
    uint64_t records, bytes, genomeBytes, chromosomeEnd, junctionLength;
    const uint32_t* oldJunctionOrder;
    uint64_t junctions;
    unsigned bits;
    size_t chunkBytes = 128*1024*1024;
};
struct GpuSjdbRemapResult {
    enum Status { Complete, Unavailable, Failed } status;
    std::string reason;
    double uploadSeconds=0, kernelSeconds=0, downloadSeconds=0;
    uint64_t deviceBytes=0, chunks=0;
};
GpuSjdbRemapResult gpuSjdbRemap(const GpuSjdbRemapRequest& request);
inline bool validGpuSjdbRemap(const GpuSjdbRemapRequest& r) {
    if (!r.input || !r.output || r.input==r.output || !r.records || r.bits<2 || r.bits>56
        || r.records>UINT64_MAX/r.bits || r.bytes!=(r.records-1)*r.bits/8+8
        || !r.junctionLength || r.chromosomeEnd>r.genomeBytes
        || r.genomeBytes >= (uint64_t(1)<<(r.bits-1))
        || r.junctions!=(r.genomeBytes-r.chromosomeEnd)/r.junctionLength
        || (r.genomeBytes-r.chromosomeEnd)%r.junctionLength
        || (r.junctions && !r.oldJunctionOrder) || r.chunkBytes<8 || r.chunkBytes%8
        || r.chunkBytes>1024*1024*1024)
        return false;
    for(uint64_t j=0;j<r.junctions;++j)
        if(r.oldJunctionOrder[j]>=r.junctions) return false;
    return true;
}
#endif
