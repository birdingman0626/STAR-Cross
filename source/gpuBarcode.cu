#include "gpuBarcode.h"
#include <cuda_runtime.h>
#include <chrono>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
void checked(cudaError_t e) {if(e!=cudaSuccess) throw std::runtime_error(std::string("GPU barcode: ")+cudaGetErrorString(e));}

// binarySearchExact from serviceFuns.cpp, step for step.
__device__ long long searchExact(unsigned long long x,const unsigned long long* X,long long N) {
    if(x>X[N-1] || x<X[0]) return -1;
    int i1=0, i2=int(N-1);
    while(i2>i1+1) {
        const int i3=(i1+i2)/2;
        if(X[i3]>=x) i2=i3; else i1=i3;
    }
    if(x==X[i2]) return i2;
    if(x==X[i1]) return i1;
    return -1;
}

// One thread per (read, candidate): candidate 0 is the barcode itself, 3*i+j (j=1..3) its
// neighbour with base i changed. Neighbour hits are OR-ed into the mask, which the host
// ignores when the exact search succeeded.
__global__ void lookupKernel(const unsigned long long* wl,long long n,unsigned length,const unsigned long long* keys,unsigned count,
                             long long* exact,unsigned long long* mask) {
    const unsigned per=3*length+1;
    const unsigned long long t=(unsigned long long)blockIdx.x*blockDim.x+threadIdx.x;
    if(t>=(unsigned long long)count*per) return;
    const unsigned r=unsigned(t/per), c=unsigned(t%per);
    const unsigned long long key=keys[r];
    if(c==0) {exact[r]=searchExact(key,wl,n); return;}
    const unsigned i=(c-1)/3;
    const unsigned long long j=(c-1)%3+1;
    if(searchExact(key^(j<<(2*i)),wl,n)>=0) atomicOr(&mask[r],1ull<<(3*i+unsigned(j-1)));
}

struct Slot {
    cudaStream_t stream=nullptr;
    void* hostKeys=nullptr; void* hostOut=nullptr; void* dev=nullptr;
    size_t capacity=0, submitted=0;
    double seconds=0; uint64_t reads=0;
    ~Slot() {
        if(hostKeys) cudaFreeHost(hostKeys);
        if(hostOut) cudaFreeHost(hostOut);
        if(dev) cudaFree(dev);
        if(stream) cudaStreamDestroy(stream);
    }
    void grow(size_t n) {
        if(n<=capacity) return;
        if(hostKeys) {checked(cudaFreeHost(hostKeys)); hostKeys=nullptr;}
        if(hostOut) {checked(cudaFreeHost(hostOut)); hostOut=nullptr;}
        if(dev) {checked(cudaFree(dev)); dev=nullptr;}
        capacity=0;
        const size_t want=n+n/4+256;
        checked(cudaHostAlloc(&hostKeys,want*8,cudaHostAllocDefault));
        checked(cudaHostAlloc(&hostOut,want*16,cudaHostAllocDefault));
        checked(cudaMalloc(&dev,want*24)); // keys, exact, mask
        capacity=want;
    }
};
}

struct GpuWhitelist::State {
    unsigned long long* wl=nullptr;
    long long n=0;
    unsigned length=0;
    std::vector<Slot> slots;
    ~State() {if(wl) cudaFree(wl);}
};

GpuWhitelist::GpuWhitelist(const unsigned long long* whitelist,size_t n,unsigned length,size_t slots):state(std::make_unique<State>()) {
    if(n==0) throw std::invalid_argument("GPU barcode: empty whitelist");
    if(length==0 || length>kMaxLength) throw std::invalid_argument("GPU barcode: barcode length out of range");
    state->n=(long long)n; state->length=length;
    checked(cudaMalloc(&state->wl,n*8));
    checked(cudaMemcpy(state->wl,whitelist,n*8,cudaMemcpyHostToDevice));
    deviceBytes=n*8;
    state->slots.resize(slots);
    for(auto& s:state->slots) checked(cudaStreamCreateWithFlags(&s.stream,cudaStreamNonBlocking));
}
GpuWhitelist::~GpuWhitelist()=default;

void GpuWhitelist::totals(double& seconds,uint64_t& reads) const {
    seconds=0; reads=0;
    for(const auto& s:state->slots) {seconds+=s.seconds; reads+=s.reads;}
}

void GpuWhitelist::lookup(const unsigned long long* keys,size_t n,int64_t* exact,uint64_t* mask,unsigned slot) {
    submit(keys,n,slot); collect(exact,mask,slot);
}

void GpuWhitelist::submit(const unsigned long long* keys,size_t n,unsigned slot) {
    auto& s=state->slots.at(slot);
    s.submitted=n;
    if(n==0) return;
    auto start=Clock::now();
    s.grow(n);
    std::memcpy(s.hostKeys,keys,n*8);
    auto* devKeys=static_cast<unsigned long long*>(s.dev);
    auto* devExact=reinterpret_cast<long long*>(devKeys+s.capacity);
    auto* devMask=reinterpret_cast<unsigned long long*>(devExact+s.capacity);
    checked(cudaMemcpyAsync(devKeys,s.hostKeys,n*8,cudaMemcpyHostToDevice,s.stream));
    checked(cudaMemsetAsync(devMask,0,n*8,s.stream));
    const unsigned block=256;
    const unsigned long long threads=(unsigned long long)n*(3*state->length+1);
    lookupKernel<<<unsigned((threads+block-1)/block),block,0,s.stream>>>(state->wl,state->n,state->length,devKeys,unsigned(n),devExact,devMask);
    checked(cudaGetLastError());
    auto* hostExact=static_cast<long long*>(s.hostOut);
    auto* hostMask=reinterpret_cast<unsigned long long*>(hostExact+s.capacity);
    checked(cudaMemcpyAsync(hostExact,devExact,n*8,cudaMemcpyDeviceToHost,s.stream));
    checked(cudaMemcpyAsync(hostMask,devMask,n*8,cudaMemcpyDeviceToHost,s.stream));
    s.seconds+=std::chrono::duration<double>(Clock::now()-start).count();
}

void GpuWhitelist::collect(int64_t* exact,uint64_t* mask,unsigned slot) {
    auto& s=state->slots.at(slot);
    const size_t n=s.submitted;
    s.submitted=0;
    if(n==0) return;
    auto start=Clock::now();
    checked(cudaStreamSynchronize(s.stream));
    const auto* hostExact=static_cast<const long long*>(s.hostOut);
    const auto* hostMask=reinterpret_cast<const unsigned long long*>(hostExact+s.capacity);
    std::memcpy(exact,hostExact,n*8);
    for(size_t i=0;i<n;++i) mask[i]=exact[i]>=0 ? 0 : hostMask[i];
    s.seconds+=std::chrono::duration<double>(Clock::now()-start).count();
    s.reads+=n;
}
