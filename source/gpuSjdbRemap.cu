#include "gpuSjdbRemap.h"
#include <cuda_runtime.h>
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace {
using Clock=std::chrono::steady_clock;
double seconds(Clock::time_point start) {return std::chrono::duration<double>(Clock::now()-start).count();}
struct DeviceBuffer {
    void* pointer=nullptr;
    ~DeviceBuffer() {if(pointer) cudaFree(pointer);}
    cudaError_t allocate(size_t bytes) {return cudaMalloc(&pointer,bytes);}
};
void check(cudaError_t error, const char* stage) {
    if(error!=cudaSuccess) throw std::runtime_error(std::string(stage)+": "+cudaGetErrorString(error));
}
__device__ uint64_t remapValue(const unsigned char* source, uint64_t record,
        unsigned bits, uint64_t genomeBytes, uint64_t chromosomeEnd,
        uint64_t junctionLength, const uint32_t* order) {
    uint64_t byte=record*bits/8, value=0;
    for(unsigned i=0;i<8;++i) value|=uint64_t(source[byte+i])<<(8*i);
    value=(value>>(record*bits%8))&((uint64_t(1)<<bits)-1);
    const uint64_t strand=uint64_t(1)<<(bits-1);
    if(value&strand) {
        uint64_t position=genomeBytes-(value&~strand);
        if(position>=chromosomeEnd) {
            uint64_t j=(position-chromosomeEnd)/junctionLength;
            position+=(uint64_t(order[j])-j)*junctionLength;
            value=(genomeBytes-position)|strand;
        }
    } else if(value>=chromosomeEnd) {
        uint64_t j=(value-chromosomeEnd)/junctionLength;
        value+=(uint64_t(order[j])-j)*junctionLength;
    }
    return value;
}
__global__ void remapWords(const unsigned char* source, unsigned char* output,
        uint64_t startWord, uint64_t words, uint64_t records, unsigned bits,
        uint64_t genomeBytes, uint64_t chromosomeEnd, uint64_t junctionLength,
        const uint32_t* order) {
    uint64_t word=uint64_t(blockIdx.x)*blockDim.x+threadIdx.x;
    if(word>=words) return;
    uint64_t bit=(startWord+word)*64, record=bit/bits, packed=0;
    unsigned shift=bit%bits;
    if(record<records) packed=remapValue(source,record,bits,genomeBytes,chromosomeEnd,junctionLength,order)>>shift;
    unsigned occupied=bits-shift;
    while(occupied<64 && ++record<records) {
        packed|=remapValue(source,record,bits,genomeBytes,chromosomeEnd,junctionLength,order)<<occupied;
        occupied+=bits;
    }
    for(unsigned i=0;i<8;++i) output[word*8+i]=(packed>>(8*i))&255;
}
}

GpuSjdbRemapResult gpuSjdbRemap(const GpuSjdbRemapRequest& r) {
    GpuSjdbRemapResult result{GpuSjdbRemapResult::Unavailable,"unsupported request"};
    if(!validGpuSjdbRemap(r)) return result;
    int devices=0;
    auto error=cudaGetDeviceCount(&devices);
    if(error!=cudaSuccess || devices==0) {
        if(error==cudaErrorIllegalAddress || error==cudaErrorAssert) result.status=GpuSjdbRemapResult::Failed;
        result.reason="no usable CUDA device";return result;
    }
    DeviceBuffer input, output, order;
    const size_t chunk=std::min<uint64_t>(r.chunkBytes,((r.bytes+7)/8)*8);
    result.deviceBytes=r.bytes+chunk+r.junctions*sizeof(uint32_t);
    size_t freeBytes=0,totalBytes=0;
    error=cudaMemGetInfo(&freeBytes,&totalBytes);
    if(error!=cudaSuccess) {
        result.status=GpuSjdbRemapResult::Failed;
        result.reason=std::string("device memory preflight failed: ")+cudaGetErrorString(error);
        return result;
    }
    if(result.deviceBytes>freeBytes) {result.reason="insufficient device memory";return result;}
    error=input.allocate(r.bytes);
    if(error==cudaSuccess) error=output.allocate(chunk);
    if(error==cudaSuccess && r.junctions) error=order.allocate(r.junctions*sizeof(uint32_t));
    if(error!=cudaSuccess) {
        if(error!=cudaErrorMemoryAllocation) result.status=GpuSjdbRemapResult::Failed;
        result.reason="device allocation failed before output commitment";return result;
    }
    // Only preflight may fall back. Any transfer/kernel failure after this point
    // is fatal, including after a chunk has been copied into the private SA2.
    try {
        auto start=Clock::now();
        check(cudaMemcpy(input.pointer,r.input,r.bytes,cudaMemcpyHostToDevice),"upload SA");
        if(r.junctions) check(cudaMemcpy(order.pointer,r.oldJunctionOrder,r.junctions*sizeof(uint32_t),cudaMemcpyHostToDevice),"upload junction order");
        result.uploadSeconds=seconds(start);
        for(uint64_t offset=0;offset<r.bytes;offset+=chunk) {
            uint64_t bytes=std::min<uint64_t>(chunk,r.bytes-offset),words=(bytes+7)/8;
            start=Clock::now();
            remapWords<<<(words+255)/256,256>>>(static_cast<unsigned char*>(input.pointer),
                static_cast<unsigned char*>(output.pointer),offset/8,words,r.records,r.bits,
                r.genomeBytes,r.chromosomeEnd,r.junctionLength,static_cast<uint32_t*>(order.pointer));
            check(cudaGetLastError(),"launch remap");
            check(cudaDeviceSynchronize(),"execute remap");
            result.kernelSeconds+=seconds(start);
            start=Clock::now();
            check(cudaMemcpy(r.output+offset,output.pointer,bytes,cudaMemcpyDeviceToHost),"download remap");
            result.downloadSeconds+=seconds(start);
            ++result.chunks;
        }
        result.status=GpuSjdbRemapResult::Complete;
        result.reason="complete";
    } catch(const std::exception& error) {
        result.status=GpuSjdbRemapResult::Failed;result.reason=error.what();
    }
    return result;
}
