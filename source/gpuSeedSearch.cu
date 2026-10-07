#include "gpuSeedSearch.h"
#include <cuda_runtime.h>
#include <chrono>
#include <stdexcept>
#include <string>
#include <memory>

namespace {
using U=uint64_t;
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point t) {return std::chrono::duration<double>(Clock::now()-t).count();}
void checked(cudaError_t e) {if(e!=cudaSuccess) throw std::runtime_error(cudaGetErrorString(e));}
struct Buffer {void* p=nullptr; ~Buffer(){if(p) cudaFree(p);} void allocate(size_t n){checked(cudaMalloc(&p,n));}};
struct Index {const unsigned char *genome,*sa; U bytes; unsigned strand;};
__device__ U packed(Index index,U i) {
    U bit=i*(index.strand+1),value=0;
    for(unsigned k=0;k<8;++k) value|=U(index.sa[bit/8+k])<<(8*k);
    return (value>>(bit%8))&((U(1)<<(index.strand+1))-1);
}
// Preserve the four CPU strand branches, including the special >3 genome rule.
__device__ U compare(Index index,const unsigned char* read,const GpuSeedQuery& q,
                     U n,U l,U isa,bool& greater) {
    U value=packed(index,isa),position=value&((U(1)<<index.strand)-1);
    bool genomeForward=(value>>index.strand)==0;
    for(U i=l;i<n;++i) {
        unsigned char s=read[q.forward ? q.start+i : q.start-i];
        if(bool(q.forward)!=genomeForward && s<4) s=3-s;
        int64_t coordinate=genomeForward ? int64_t(position)+int64_t(i) : int64_t(index.bytes)-1-int64_t(position)-int64_t(i);
        // Genome::genomeLoad attaches symbol-5 guards on both ends. Preserve
        // those sentinels explicitly rather than reading outside the device file.
        unsigned char g=coordinate<0 || U(coordinate)>=index.bytes ? 5 : index.genome[coordinate];
        if(s!=g) {greater=genomeForward ? s>g : !(s>g || g>3);return i;}
    }
    return n; // greater is not an output on an exact match.
}
__device__ U median(U a,U b) {return a/2+b/2+(a%2+b%2)/2;}
__device__ U range(Index index,const unsigned char* read,const GpuSeedQuery& q,
        U i3,U l3,U i1,U l1,U ia,U la,U ib,U lb) {
    bool greater;
    if(l1<l3) {lb=l1;ib=i1;ia=i3;}
    else if(la<l1) {lb=la;ib=ia;ia=i1;}
    while((ib+1<ia)|(ib>ia+1)) {
        U middle=median(ia,ib),length=compare(index,read,q,l3,lb,middle,greater);
        if(length==l3) ia=middle;else {ib=middle;lb=length;}
    }
    return ia;
}
__global__ void searchKernel(Index index,const unsigned char* reads,const GpuSeedQuery* queries,
                             GpuSeedMatch* matches,U count) {
    U tid=U(blockIdx.x)*blockDim.x+threadIdx.x;if(tid>=count) return;
    auto q=queries[tid];const auto* read=reads+q.offset;bool greater;
    U i1=q.lower,i2=q.upper,l=q.initialLength;
    U l1=compare(index,read,q,q.length,l,i1,greater),l2=compare(index,read,q,q.length,l,i2,greater);
    l=l1<l2?l1:l2;
    U la1=l1,lb1=l1,ia1=i1,ib1=i1,la2=l2,lb2=l2,ia2=i2,ib2=i2,i3=i1,l3=l1;
    while(i1+1<i2) {
        i3=median(i1,i2);l3=compare(index,read,q,q.length,l,i3,greater);
        if(l3==q.length) break;
        if(greater) {
            if(l3>l1) {lb1=la1;la1=l1;ib1=ia1;ia1=i1;}i1=i3;l1=l3;
        } else {
            if(l3>l2) {lb2=la2;la2=l2;ib2=ia2;ia2=i2;}i2=i3;l2=l3;
        }
        l=l1<l2?l1:l2;
    }
    if(l3<q.length) {if(l1>l2) {i3=i1;l3=l1;}else {i3=i2;l3=l2;}}
    i1=range(index,read,q,i3,l3,i1,l1,ia1,la1,ib1,lb1);
    i2=range(index,read,q,i3,l3,i2,l2,ia2,la2,ib2,lb2);
    matches[tid]={l3,i1,i2,i2-i1+1};
}
}
struct GpuSeedIndex::State {
    Buffer genome,sa,reads,queries,matches;
    U genomeBytes,records;unsigned strand;
    size_t queryCapacity,readCapacity;
};
GpuSeedIndex::GpuSeedIndex(const char* genome,U genomeBytes,const char* sa,U saBytes,
        U records,unsigned strand,size_t batchCapacity,size_t readCapacity):state(nullptr) {
    if(!genome || !sa || !records || strand<8 || strand>55 || !batchCapacity || batchCapacity>1048576
       || !readCapacity || records>UINT64_MAX/(strand+1) || saBytes!=(records-1)*(strand+1)/8+8
       || genomeBytes>=(U(1)<<strand)) throw std::invalid_argument("unsupported seed index geometry");
    auto s=std::make_unique<State>();s->genomeBytes=genomeBytes;s->records=records;s->strand=strand;
    s->queryCapacity=batchCapacity;s->readCapacity=readCapacity;
    deviceBytes=genomeBytes+saBytes+readCapacity+batchCapacity*(sizeof(GpuSeedQuery)+sizeof(GpuSeedMatch));
    size_t available,total;checked(cudaMemGetInfo(&available,&total));
    if(deviceBytes>available) throw std::runtime_error("seed index exceeds available device memory");
    s->genome.allocate(genomeBytes);s->sa.allocate(saBytes);s->reads.allocate(readCapacity);
    s->queries.allocate(batchCapacity*sizeof(GpuSeedQuery));s->matches.allocate(batchCapacity*sizeof(GpuSeedMatch));
    auto start=Clock::now();checked(cudaMemcpy(s->genome.p,genome,genomeBytes,cudaMemcpyHostToDevice));
    checked(cudaMemcpy(s->sa.p,sa,saBytes,cudaMemcpyHostToDevice));indexUploadSeconds=elapsed(start);
    state=s.release();
}
GpuSeedIndex::~GpuSeedIndex() {delete state;}
std::vector<GpuSeedMatch> GpuSeedIndex::search(const std::vector<char>& reads,
        const std::vector<GpuSeedQuery>& queries,GpuSeedTiming& t) {
    t={};
    if(queries.size()>state->queryCapacity || reads.size()>state->readCapacity)
        throw std::invalid_argument("seed batch exceeds capacity");
    for(const auto& q:queries) {
        if(!q.length || q.initialLength>q.length || q.forward>1 || q.lower>q.upper || q.upper>=state->records
           || q.offset>reads.size() || q.readBytes>reads.size()-q.offset || q.start>=q.readBytes
           || (q.forward ? q.length>q.readBytes-q.start : q.length>q.start+1))
            throw std::invalid_argument("invalid seed query");
    }
    std::vector<GpuSeedMatch> result(queries.size());if(queries.empty()) return result;
    auto start=Clock::now();checked(cudaMemcpy(state->reads.p,reads.data(),reads.size(),cudaMemcpyHostToDevice));
    checked(cudaMemcpy(state->queries.p,queries.data(),queries.size()*sizeof(GpuSeedQuery),cudaMemcpyHostToDevice));t.upload=elapsed(start);
    start=Clock::now();searchKernel<<<(queries.size()+127)/128,128>>>({static_cast<unsigned char*>(state->genome.p),
        static_cast<unsigned char*>(state->sa.p),state->genomeBytes,state->strand},static_cast<unsigned char*>(state->reads.p),
        static_cast<GpuSeedQuery*>(state->queries.p),static_cast<GpuSeedMatch*>(state->matches.p),queries.size());
    checked(cudaGetLastError());checked(cudaDeviceSynchronize());t.kernel=elapsed(start);
    start=Clock::now();checked(cudaMemcpy(result.data(),state->matches.p,result.size()*sizeof(GpuSeedMatch),cudaMemcpyDeviceToHost));t.download=elapsed(start);
    return result;
}
