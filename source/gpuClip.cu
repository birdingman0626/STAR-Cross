#include "gpuClip.h"
#include <cuda_runtime.h>
#include <chrono>
#include <climits>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock=std::chrono::steady_clock;
void checked(cudaError_t e) {if(e!=cudaSuccess) throw std::runtime_error(std::string("GPU clip: ")+cudaGetErrorString(e));}

// Mirrors overlapAllLanes in ClipCR4.cpp for one sequence: free leading and
// trailing gaps in both sequences; best score on the last query row (first
// target position wins ties), replaced by the best in the last target column
// only when strictly larger.
__global__ void clipKernel(const uint8_t* codes,unsigned readLen,unsigned n,const uint8_t* query,unsigned queryLen,
                           int match,int mismatch,int gapOpen,int gapExt,int2* out) {
    const unsigned r=blockIdx.x*blockDim.x+threadIdx.x;
    if(r>=n) return;
    const int negInf=INT_MIN/2;
    int H[GpuClipper::kMaxQuery+1], E[GpuClipper::kMaxQuery+1];
    for(unsigned i=0;i<=queryLen;++i) {H[i]=0; E[i]=negInf;}
    int best=negInf, bestEnd=-1;
    const uint8_t* target=codes+size_t(r)*readLen;
    for(unsigned col=0;col<readLen;++col) {
        const int t=target[col];
        int diag=0, gapF=negInf;
        for(unsigned row=1;row<=queryLen;++row) {
            const int q=min(int(query[row-1]),4);
            const int qMatch=q==4 ? 0 : match;
            const int previous=H[row];
            const int eNew=max(previous-gapOpen,E[row]-gapExt);
            const int fNew=max(H[row-1]-gapOpen,gapF-gapExt);
            const int sub=t==q ? qMatch : mismatch;
            const int hNew=max(max(eNew,fNew),diag+sub);
            E[row]=eNew; gapF=fNew; diag=previous; H[row]=hNew;
        }
        if(H[queryLen]>best) {best=H[queryLen]; bestEnd=int(col);}
    }
    int lastColumn=negInf;
    for(unsigned row=1;row<=queryLen;++row) lastColumn=max(lastColumn,H[row]);
    if(lastColumn>best) {best=lastColumn; bestEnd=int(readLen)-1;}
    out[r]=make_int2(best,bestEnd);
}

struct Slot {
    cudaStream_t stream=nullptr;
    void* hostCodes=nullptr; size_t hostCodesBytes=0;
    void* hostOut=nullptr; size_t hostOutBytes=0;
    void* devCodes=nullptr; size_t devCodesBytes=0;
    void* devOut=nullptr; size_t devOutBytes=0;
    void* devQuery=nullptr;
    std::vector<uint8_t> query;
    double seconds=0; uint64_t reads=0;
    ~Slot() {
        if(hostCodes) cudaFreeHost(hostCodes);
        if(hostOut) cudaFreeHost(hostOut);
        if(devCodes) cudaFree(devCodes);
        if(devOut) cudaFree(devOut);
        if(devQuery) cudaFree(devQuery);
        if(stream) cudaStreamDestroy(stream);
    }
    static void growHost(void*& p,size_t& have,size_t need) {
        if(need<=have) return;
        if(p) {checked(cudaFreeHost(p)); p=nullptr; have=0;}
        const size_t want=need+need/4+4096;
        checked(cudaHostAlloc(&p,want,cudaHostAllocDefault)); have=want;
    }
    static void growDevice(void*& p,size_t& have,size_t need) {
        if(need<=have) return;
        if(p) {checked(cudaFree(p)); p=nullptr; have=0;}
        const size_t want=need+need/4+4096;
        checked(cudaMalloc(&p,want)); have=want;
    }
};
}

struct GpuClipper::State {
    std::vector<Slot> slots;
};

GpuClipper::GpuClipper(size_t slots):state(std::make_unique<State>()) {
    state->slots.resize(slots);
    for(auto& s:state->slots) {
        checked(cudaStreamCreateWithFlags(&s.stream,cudaStreamNonBlocking));
        checked(cudaMalloc(&s.devQuery,kMaxQuery));
    }
}
GpuClipper::~GpuClipper()=default;

void GpuClipper::totals(double& seconds,uint64_t& reads) const {
    seconds=0; reads=0;
    for(const auto& s:state->slots) {seconds+=s.seconds; reads+=s.reads;}
}

void GpuClipper::align(const GpuClipScoring& sc,const uint8_t* codes,size_t n,const uint8_t* query,unsigned queryLen,int* out,unsigned slot) {
    if(sc.readLen==0) throw std::invalid_argument("GPU clip: empty target length");
    if(queryLen==0 || queryLen>kMaxQuery) throw std::invalid_argument("GPU clip: adapter length out of range");
    if(n==0) return;
    auto start=Clock::now();
    auto& s=state->slots.at(slot);
    const size_t codeBytes=n*sc.readLen, outBytes=n*sizeof(int2);
    Slot::growHost(s.hostCodes,s.hostCodesBytes,codeBytes);
    Slot::growHost(s.hostOut,s.hostOutBytes,outBytes);
    Slot::growDevice(s.devCodes,s.devCodesBytes,codeBytes);
    Slot::growDevice(s.devOut,s.devOutBytes,outBytes);
    if(s.query.size()!=queryLen || std::memcmp(s.query.data(),query,queryLen)!=0) {
        s.query.assign(query,query+queryLen);
        checked(cudaMemcpyAsync(s.devQuery,s.query.data(),queryLen,cudaMemcpyHostToDevice,s.stream));
    }
    std::memcpy(s.hostCodes,codes,codeBytes);
    checked(cudaMemcpyAsync(s.devCodes,s.hostCodes,codeBytes,cudaMemcpyHostToDevice,s.stream));
    const unsigned block=128;
    clipKernel<<<unsigned((n+block-1)/block),block,0,s.stream>>>(static_cast<const uint8_t*>(s.devCodes),sc.readLen,unsigned(n),
        static_cast<const uint8_t*>(s.devQuery),queryLen,sc.match,sc.mismatch,sc.gapOpen,sc.gapExt,static_cast<int2*>(s.devOut));
    checked(cudaGetLastError());
    checked(cudaMemcpyAsync(s.hostOut,s.devOut,outBytes,cudaMemcpyDeviceToHost,s.stream));
    checked(cudaStreamSynchronize(s.stream));
    std::memcpy(out,s.hostOut,outBytes);
    s.seconds+=std::chrono::duration<double>(Clock::now()-start).count();
    s.reads+=n;
}
