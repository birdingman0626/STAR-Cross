#include "gpuSyncMode.h"
#include "gpuSeedSearch.h"
#include <cuda_runtime.h>
#include <cub/device/device_select.cuh>
#include <chrono>
#include <stdexcept>
#include <string>
#include <memory>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <exception>
#include <thread>
#include <vector>

namespace {
using U=uint64_t;
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point t) {return std::chrono::duration<double>(Clock::now()-t).count();}
void checked(cudaError_t e) {if(e!=cudaSuccess) throw std::runtime_error(cudaGetErrorString(e));}
struct Buffer {void* p=nullptr; ~Buffer(){if(p) cudaFree(p);} void allocate(size_t n){checked(cudaMalloc(&p,n));}};
// Large host-to-device upload through a ring of pinned chunks: host threads fill one chunk
// while the copy engine moves the previous ones. Pageable cudaMemcpy of the 28 GB index
// ran at about 7 GB/s.
void uploadLarge(void* device,const void* host,size_t bytes) {
    constexpr size_t chunk=size_t(128)<<20; constexpr int ring=4; constexpr unsigned fillers=4;
    if(bytes<=chunk) {checked(cudaMemcpy(device,host,bytes,cudaMemcpyHostToDevice)); return;}
    // Preferred: page-lock the source in place (milliseconds) and copy directly; on this
    // machine that runs at about 15 GB/s against 8 GB/s for a pageable copy. Host memcpy
    // into staging is capped near 14 GB/s, so staging is only the fallback.
    void* source=const_cast<void*>(host);
    if(cudaHostRegister(source,bytes,cudaHostRegisterReadOnly)==cudaSuccess) {
        const cudaError_t copied=cudaMemcpy(device,host,bytes,cudaMemcpyHostToDevice);
        (void)cudaHostUnregister(source);
        checked(copied);
        return;
    }
    (void)cudaGetLastError();
    cudaStream_t stream; checked(cudaStreamCreateWithFlags(&stream,cudaStreamNonBlocking));
    void* staging[ring]={}; cudaEvent_t done[ring]={};
    std::exception_ptr failure;
    try {
        for(int i=0;i<ring;++i) {checked(cudaHostAlloc(&staging[i],chunk,cudaHostAllocDefault)); checked(cudaEventCreateWithFlags(&done[i],cudaEventDisableTiming));}
        const char* src=static_cast<const char*>(host); char* dst=static_cast<char*>(device);
        for(size_t off=0,i=0;off<bytes;off+=chunk,++i) {
            const int b=int(i%ring); const size_t n=std::min(chunk,bytes-off);
            checked(cudaEventSynchronize(done[b]));  // previous copy out of this chunk finished
            char* to=static_cast<char*>(staging[b]); const char* from=src+off;
            const size_t part=(n+fillers-1)/fillers;
            std::vector<std::thread> pool;
            for(unsigned t=1;t<fillers;++t) {
                const size_t lo=std::min(n,t*part),hi=std::min(n,lo+part);
                if(lo<hi) pool.emplace_back([=]{std::memcpy(to+lo,from+lo,hi-lo);});
            }
            std::memcpy(to,from,std::min(n,part));
            for(auto& th:pool) th.join();
            checked(cudaMemcpyAsync(dst+off,to,n,cudaMemcpyHostToDevice,stream));
            checked(cudaEventRecord(done[b],stream));
        }
        checked(cudaStreamSynchronize(stream));
    } catch(...) {failure=std::current_exception(); cudaStreamSynchronize(stream);}
    for(int i=0;i<ring;++i) {if(done[i]) cudaEventDestroy(done[i]); if(staging[i]) cudaFreeHost(staging[i]);}
    cudaStreamDestroy(stream);
    if(failure) std::rethrow_exception(failure);
}
struct Pinned {void* p=nullptr; ~Pinned(){if(p) cudaFreeHost(p);} void allocate(size_t n){checked(cudaHostAlloc(&p,n,cudaHostAllocDefault));}};
struct Stream {cudaStream_t value=nullptr; Stream(){checked(cudaStreamCreateWithFlags(&value,cudaStreamNonBlocking));}
    ~Stream(){if(value) cudaStreamDestroy(value);}};
struct Event {cudaEvent_t value=nullptr; Event(){checked(cudaEventCreate(&value));}
    ~Event(){if(value) cudaEventDestroy(value);}};
struct Index {const unsigned char *genome,*sa; U bytes; unsigned strand; U records;};
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
__device__ GpuSeedMatch searchOne(Index index,const unsigned char* reads,const GpuSeedQuery& q) {
    const auto* read=reads+q.offset;bool greater;
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
    return {l3,i1,i2,i2-i1+1};
}
__global__ void searchKernel(Index index,const unsigned char* reads,const GpuSeedQuery* queries,
                             GpuSeedMatch* matches,U count) {
    U tid=U(blockIdx.x)*blockDim.x+threadIdx.x;
    if(tid<count) matches[tid]=searchOne(index,reads,queries[tid]);
}
#include "gpuAdaptiveSeed.cuh"
#include "gpuSeedHits.cuh"
}
struct GpuSeedIndex::State {
    struct Slot {
        Stream stream;
        Event begin,uploaded,searched,downloaded;
        Buffer reads,queries,matches;
        Pinned hostQueries,hostMatches;
        Buffer events,counts,summaries,hits,hitTasks;
        Pinned hostEvents,hostCounts,hostSummaries,hostHits,hostHitTasks;
        Buffer flags,taskKept,readTasks,selected,selectedCount,selectTemp;
        Pinned hostTaskKept,hostReadTasks,hostSelectedCount;
        size_t selectTempBytes=0;
        U keptGuess=0; // kept hits downloaded with the counts (from recent batches); the rest needs a second copy
        std::vector<std::vector<unsigned>> readNodes;
        std::vector<U> readLengths,hitCounts;
        size_t bytes=0;
        U generation=0;
    };
    Buffer genome,sa,prefixes;
    std::vector<std::unique_ptr<Slot>> slots;
    int device=0;
    U genomeBytes,records;unsigned strand;
    size_t queryCapacity,readCapacity;
    AdaptiveIndex adaptive{};
    SeedRoundConfig config{};
    unsigned eventCapacity=0;
    unsigned hitCapacity=0;
    Buffer sjD,sjA;
    WindowFilter filter{};
    bool filtering=false;
};
GpuSeedIndex::GpuSeedIndex(const char* genome,U genomeBytes,const char* sa,U saBytes,
        U records,unsigned strand,size_t batchCapacity,size_t readCapacity,size_t slots):state(nullptr) {
    if(!genome || !sa || !records || strand<8 || strand>55 || !batchCapacity || batchCapacity>1048576
       || !readCapacity || !slots || slots>1024 || records>UINT64_MAX/(strand+1) || saBytes!=(records-1)*(strand+1)/8+8
       || genomeBytes>=(U(1)<<strand)) throw std::invalid_argument("unsupported seed index geometry");
    auto s=std::make_unique<State>();s->genomeBytes=genomeBytes;s->records=records;s->strand=strand;
    s->queryCapacity=batchCapacity;s->readCapacity=readCapacity;
    checked(cudaGetDevice(&s->device));
    // Sleep while waiting on the device: spinning host threads take cores from mapping workers.
    (void)cudaSetDeviceFlags(gpuSyncScheduleFlag());
    (void)cudaGetLastError();
    deviceBytes=genomeBytes+saBytes+slots*(readCapacity+batchCapacity*(sizeof(GpuSeedQuery)+sizeof(GpuSeedMatch)));
    size_t available,total;checked(cudaMemGetInfo(&available,&total));
    if(deviceBytes>available) throw std::runtime_error("seed index exceeds available device memory");
    s->genome.allocate(genomeBytes);s->sa.allocate(saBytes);
    for(size_t i=0;i<slots;++i) {
        auto slot=std::make_unique<State::Slot>();
        slot->reads.allocate(readCapacity);slot->queries.allocate(batchCapacity*sizeof(GpuSeedQuery));
        slot->matches.allocate(batchCapacity*sizeof(GpuSeedMatch));
        slot->hostQueries.allocate(batchCapacity*sizeof(GpuSeedQuery));slot->hostMatches.allocate(batchCapacity*sizeof(GpuSeedMatch));
        s->slots.push_back(std::move(slot));
    }
    auto start=Clock::now();uploadLarge(s->genome.p,genome,genomeBytes);
    uploadLarge(s->sa.p,sa,saBytes);indexUploadSeconds=elapsed(start);
    state=s.release();
    pinnedBytes=slots*batchCapacity*(sizeof(GpuSeedQuery)+sizeof(GpuSeedMatch));
}
GpuSeedIndex::~GpuSeedIndex() {delete state;}
const void* GpuSeedIndex::deviceGenome() const {return state->genome.p;}
std::vector<GpuSeedMatch> GpuSeedIndex::search(const std::vector<char>& reads,
        const std::vector<GpuSeedQuery>& queries,GpuSeedTiming& t) {
    auto start=Clock::now();const auto generation=uploadReads(reads);
    const double upload=elapsed(start);
    auto result=searchPrepared(queries,t,0,generation);t.upload+=upload;return result;
}
uint64_t GpuSeedIndex::uploadReads(const std::vector<char>& reads,size_t slotId) {
    if(slotId>=state->slots.size() || reads.size()>state->readCapacity)
        throw std::invalid_argument("seed read pool exceeds slot capacity");
    checked(cudaSetDevice(state->device));
    auto& slot=*state->slots[slotId];
    checked(cudaMemcpyAsync(slot.reads.p,reads.data(),reads.size(),cudaMemcpyHostToDevice,slot.stream.value));
    checked(cudaStreamSynchronize(slot.stream.value));
    slot.bytes=reads.size();return ++slot.generation;
}
std::vector<GpuSeedMatch> GpuSeedIndex::searchPrepared(const std::vector<GpuSeedQuery>& queries,
        GpuSeedTiming& t,size_t slotId,uint64_t generation) {
    t={};
    if(slotId>=state->slots.size() || queries.size()>state->queryCapacity)
        throw std::invalid_argument("seed batch exceeds capacity");
    auto& slot=*state->slots[slotId];
    if(!generation || slot.generation!=generation) throw std::invalid_argument("stale seed read pool generation");
    checked(cudaSetDevice(state->device));
    for(const auto& q:queries) {
        if(!q.length || q.initialLength>q.length || q.forward>1 || q.lower>q.upper || q.upper>=state->records
           || q.offset>slot.bytes || q.readBytes>slot.bytes-q.offset || q.start>=q.readBytes
           || (q.forward ? q.length>q.readBytes-q.start : q.length>q.start+1))
            throw std::invalid_argument("invalid seed query");
    }
    std::vector<GpuSeedMatch> result(queries.size());if(queries.empty()) return result;
    auto start=Clock::now();std::memcpy(slot.hostQueries.p,queries.data(),queries.size()*sizeof(GpuSeedQuery));
    t.upload=elapsed(start);
    checked(cudaEventRecord(slot.begin.value,slot.stream.value));
    checked(cudaMemcpyAsync(slot.queries.p,slot.hostQueries.p,queries.size()*sizeof(GpuSeedQuery),cudaMemcpyHostToDevice,slot.stream.value));
    checked(cudaEventRecord(slot.uploaded.value,slot.stream.value));
    searchKernel<<<(queries.size()+127)/128,128,0,slot.stream.value>>>({static_cast<unsigned char*>(state->genome.p),
        static_cast<unsigned char*>(state->sa.p),state->genomeBytes,state->strand,state->records},static_cast<unsigned char*>(slot.reads.p),
        static_cast<GpuSeedQuery*>(slot.queries.p),static_cast<GpuSeedMatch*>(slot.matches.p),queries.size());
    checked(cudaGetLastError());
    checked(cudaEventRecord(slot.searched.value,slot.stream.value));
    checked(cudaMemcpyAsync(slot.hostMatches.p,slot.matches.p,result.size()*sizeof(GpuSeedMatch),cudaMemcpyDeviceToHost,slot.stream.value));
    checked(cudaEventRecord(slot.downloaded.value,slot.stream.value));
    checked(cudaStreamSynchronize(slot.stream.value));
    start=Clock::now();std::memcpy(result.data(),slot.hostMatches.p,result.size()*sizeof(GpuSeedMatch));t.download=elapsed(start);
    float milliseconds=0;
    checked(cudaEventElapsedTime(&milliseconds,slot.begin.value,slot.uploaded.value));t.upload+=milliseconds/1000.0;
    checked(cudaEventElapsedTime(&milliseconds,slot.uploaded.value,slot.searched.value));t.kernel=milliseconds/1000.0;
    checked(cudaEventElapsedTime(&milliseconds,slot.searched.value,slot.downloaded.value));t.download+=milliseconds/1000.0;
    return result;
}

void GpuSeedIndex::configureAdaptive(const char* prefixes,U prefixBytes,U prefixRecords,
        unsigned prefixBits,const unsigned long long* starts,unsigned prefixBases,
        U sparse,U absentFlag,U nFlag,U nMask,SeedRoundConfig config,size_t eventCapacity,size_t hitCapacity) {
    if(state->eventCapacity) throw std::logic_error("adaptive seed index already configured");
    if(!prefixes || !starts || !prefixRecords || !prefixBases || prefixBases>31
       || prefixBits!=state->strand+3 || prefixRecords>UINT64_MAX/prefixBits
       || prefixBytes!=(prefixRecords-1)*prefixBits/8+8 || starts[prefixBases]!=prefixRecords
       || !sparse || !config.maxSplits || !std::isfinite(config.startFraction)
       || config.startFraction<0 || config.startFraction>1e12
       || absentFlag!=(U(1)<<(state->strand+2)) || nFlag!=(U(1)<<(state->strand+1)) || nMask!=~nFlag
       || !eventCapacity || eventCapacity>131072 || !hitCapacity || hitCapacity>4194304)
        throw std::invalid_argument("invalid adaptive seed index geometry or capacity");
    U expected=0,level=1;
    for(unsigned i=0;i<=prefixBases;++i) {
        if(starts[i]!=expected) throw std::invalid_argument("invalid adaptive prefix offsets");
        state->adaptive.starts[i]=starts[i];
        if(i<prefixBases) {level*=4;expected+=level;}
    }
    checked(cudaSetDevice(state->device));
    const U slotBytes=eventCapacity*sizeof(SeedNode)+sizeof(AdaptiveCounts)+state->queryCapacity*sizeof(SeedSummary)
        +hitCapacity*sizeof(SeedProjectedHit)+eventCapacity*sizeof(HitTask);
    const U additional=prefixBytes+state->slots.size()*slotBytes;
    size_t available,total;checked(cudaMemGetInfo(&available,&total));
    if(additional>available) throw std::runtime_error("adaptive seed index exceeds available device memory");
    state->prefixes.allocate(prefixBytes);
    auto start=Clock::now();checked(cudaMemcpy(state->prefixes.p,prefixes,prefixBytes,cudaMemcpyHostToDevice));
    indexUploadSeconds+=elapsed(start);
    for(auto& slot:state->slots) {
        slot->events.allocate(eventCapacity*sizeof(SeedNode));slot->counts.allocate(sizeof(AdaptiveCounts));
        slot->hostEvents.allocate(eventCapacity*sizeof(SeedNode));slot->hostCounts.allocate(sizeof(AdaptiveCounts));
        slot->summaries.allocate(state->queryCapacity*sizeof(SeedSummary));
        slot->hostSummaries.allocate(state->queryCapacity*sizeof(SeedSummary));
        slot->hits.allocate(hitCapacity*sizeof(SeedProjectedHit));
        slot->hostHits.allocate(hitCapacity*sizeof(SeedProjectedHit));
        slot->hitTasks.allocate(eventCapacity*sizeof(HitTask));
        slot->hostHitTasks.allocate(eventCapacity*sizeof(HitTask));
    }
    state->adaptive.prefixes=static_cast<unsigned char*>(state->prefixes.p);
    state->adaptive.prefixBits=prefixBits;state->adaptive.prefixBases=prefixBases;
    state->adaptive.sparse=sparse;state->adaptive.absentFlag=absentFlag;
    state->adaptive.nFlag=nFlag;state->adaptive.nMask=nMask;
    state->config=config;state->eventCapacity=static_cast<unsigned>(eventCapacity);
    state->hitCapacity=static_cast<unsigned>(hitCapacity);
    deviceBytes+=additional;
    pinnedBytes+=state->slots.size()*slotBytes;
}

GpuAdaptiveSeeds GpuSeedIndex::seedReads(const std::vector<char>& reads,
        const std::vector<SeedReadRange>& ranges,size_t slotId,bool deferProjection) {
    if(!state->eventCapacity) throw std::logic_error("adaptive seed index not configured");
    if(slotId>=state->slots.size() || ranges.size()>state->queryCapacity || reads.size()>state->readCapacity)
        throw std::invalid_argument("adaptive seed batch exceeds slot capacity");
    for(const auto& r:ranges)
        if(r.offset>reads.size() || r.length>reads.size()-r.offset || r.length>UINT32_MAX)
            throw std::invalid_argument("adaptive read range exceeds read pool");
    GpuAdaptiveSeeds output;output.reads.resize(ranges.size());output.summaries.resize(ranges.size());
    if(ranges.empty()) return output;
    // One owner per slot; no host decisions between individual seed searches.
    checked(cudaSetDevice(state->device));
    auto& slot=*state->slots[slotId];
    std::memcpy(slot.hostQueries.p,ranges.data(),ranges.size()*sizeof(SeedReadRange));
    checked(cudaMemcpyAsync(slot.reads.p,reads.data(),reads.size(),cudaMemcpyHostToDevice,slot.stream.value));
    checked(cudaMemcpyAsync(slot.queries.p,slot.hostQueries.p,ranges.size()*sizeof(SeedReadRange),cudaMemcpyHostToDevice,slot.stream.value));
    checked(cudaMemsetAsync(slot.matches.p,0xff,ranges.size()*sizeof(unsigned),slot.stream.value));
    checked(cudaMemsetAsync(slot.counts.p,0,sizeof(AdaptiveCounts),slot.stream.value));
    adaptiveKernel<<<(ranges.size()+127)/128,128,0,slot.stream.value>>>(
        {static_cast<unsigned char*>(state->genome.p),static_cast<unsigned char*>(state->sa.p),state->genomeBytes,state->strand,state->records},
        state->adaptive,state->config,static_cast<unsigned char*>(slot.reads.p),
        static_cast<SeedReadRange*>(slot.queries.p),static_cast<unsigned>(ranges.size()),
        static_cast<unsigned*>(slot.matches.p),static_cast<SeedNode*>(slot.events.p),
        static_cast<AdaptiveCounts*>(slot.counts.p),state->eventCapacity,static_cast<SeedSummary*>(slot.summaries.p));
    checked(cudaGetLastError());
    checked(cudaMemcpyAsync(slot.hostCounts.p,slot.counts.p,sizeof(AdaptiveCounts),cudaMemcpyDeviceToHost,slot.stream.value));
    checked(cudaMemcpyAsync(slot.hostMatches.p,slot.matches.p,ranges.size()*sizeof(unsigned),cudaMemcpyDeviceToHost,slot.stream.value));
    checked(cudaMemcpyAsync(slot.hostSummaries.p,slot.summaries.p,ranges.size()*sizeof(SeedSummary),cudaMemcpyDeviceToHost,slot.stream.value));
    checked(cudaStreamSynchronize(slot.stream.value));
    slot.bytes=reads.size();++slot.generation;
    const auto counts=*static_cast<AdaptiveCounts*>(slot.hostCounts.p);
    if(counts.error || counts.used>state->eventCapacity)
        throw std::runtime_error("adaptive GPU seed error mask="+std::to_string(counts.error)+
            " (1=event capacity, 2=zero stride, 4=no progress, 8=seedPerReadNmax); no partial seeds accepted");
    checked(cudaMemcpyAsync(slot.hostEvents.p,slot.events.p,counts.used*sizeof(SeedNode),cudaMemcpyDeviceToHost,slot.stream.value));
    checked(cudaStreamSynchronize(slot.stream.value));
    const auto* nodes=static_cast<SeedNode*>(slot.hostEvents.p);
    const auto* heads=static_cast<unsigned*>(slot.hostMatches.p);
    const auto* summaries=static_cast<const SeedSummary*>(slot.hostSummaries.p);
    slot.readNodes.resize(ranges.size());slot.readLengths.clear();slot.hitCounts.assign(ranges.size(),0);
    for(const auto& read:ranges) slot.readLengths.push_back(read.length);
    unsigned visited=0;
    for(size_t i=0;i<ranges.size();++i) {// size each read's lists once (pieces is checked against the chain below)
        const size_t expected=std::min<size_t>(summaries[i].pieces,counts.used);
        slot.readNodes[i].clear(); slot.readNodes[i].reserve(expected); output.reads[i].reserve(expected);
    }
    for(size_t i=0;i<ranges.size();++i) for(unsigned node=heads[i];node!=seedListEnd;node=nodes[node].next) {
        if(node>=counts.used || ++visited>counts.used) throw std::runtime_error("invalid GPU seed event chain");
        output.reads[i].push_back(nodes[node].emission);
        slot.readNodes[i].push_back(node);
        slot.hitCounts[i]+=nodes[node].emission.match.upper-nodes[node].emission.match.lower+1;
    }
    if(visited!=counts.used) throw std::runtime_error("incomplete GPU seed event chains");
    std::memcpy(output.summaries.data(),slot.hostSummaries.p,ranges.size()*sizeof(SeedSummary));
    for(size_t i=0;i<ranges.size();++i)
        if(output.summaries[i].pieces!=output.reads[i].size()) throw std::runtime_error("invalid GPU seed summary");
    output.searches=counts.searches;
    output.generation=slot.generation;
    if(!deferProjection) output.projected=projectPrepared(0,slotId,slot.generation,false);
    return output;
}

void GpuSeedIndex::configureWindowFilter(const U* sjDstart,const U* sjAstart,U sjdbN,U sjGstart,
        U sjdbLength,U sjdbOverhang,unsigned binBits,U anchorDist,U flank,U anchorMax) {
    if(!state->eventCapacity) throw std::logic_error("configure adaptive seeds before the window filter");
    if(state->filtering) throw std::logic_error("window filter already configured");
    if((sjdbN && (!sjDstart || !sjAstart || !sjdbLength)) || binBits>=62 || anchorDist>(U(1)<<40) || flank>(U(1)<<40))
        throw std::invalid_argument("invalid window filter geometry");
    checked(cudaSetDevice(state->device));
    auto& f=state->filter;
    if(sjdbN) {
        state->sjD.allocate(sjdbN*sizeof(U));state->sjA.allocate(sjdbN*sizeof(U));
        checked(cudaMemcpy(state->sjD.p,sjDstart,sjdbN*sizeof(U),cudaMemcpyHostToDevice));
        checked(cudaMemcpy(state->sjA.p,sjAstart,sjdbN*sizeof(U),cudaMemcpyHostToDevice));
        deviceBytes+=2*sjdbN*sizeof(U);
    }
    f.sjGstart=sjGstart;f.sjdbLength=sjdbLength;f.sjdbOverhang=sjdbOverhang;f.sjdbN=sjdbN;
    f.sjD=static_cast<const U*>(state->sjD.p);f.sjA=static_cast<const U*>(state->sjA.p);
    f.binBits=binBits;f.reach=anchorDist>flank ? anchorDist:flank;f.anchorMax=anchorMax;
    const size_t hits=state->hitCapacity,tasks=state->eventCapacity,reads=state->queryCapacity+1;
    for(auto& slot:state->slots) {
        size_t temp=0;
        checked(cub::DeviceSelect::Flagged(nullptr,temp,static_cast<SeedProjectedHit*>(nullptr),
            static_cast<unsigned char*>(nullptr),static_cast<SeedProjectedHit*>(nullptr),static_cast<unsigned*>(nullptr),
            static_cast<int>(hits),slot->stream.value));
        slot->selectTempBytes=temp;slot->selectTemp.allocate(temp?temp:1);
        slot->flags.allocate(hits);slot->taskKept.allocate(tasks*sizeof(unsigned));
        slot->readTasks.allocate(reads*sizeof(unsigned));slot->selected.allocate(hits*sizeof(SeedProjectedHit));
        slot->selectedCount.allocate(sizeof(unsigned));
        slot->hostTaskKept.allocate(tasks*sizeof(unsigned));slot->hostReadTasks.allocate(reads*sizeof(unsigned));
        slot->hostSelectedCount.allocate(sizeof(unsigned));
        deviceBytes+=temp+hits+tasks*sizeof(unsigned)+reads*sizeof(unsigned)+hits*sizeof(SeedProjectedHit)+sizeof(unsigned);
        pinnedBytes+=tasks*sizeof(unsigned)+reads*sizeof(unsigned)+sizeof(unsigned);
    }
    state->filtering=true;
}

std::vector<SeedHitBatch> GpuSeedIndex::projectPrepared(size_t first,size_t slotId,U generation,bool partial) {
    if(slotId>=state->slots.size()) throw std::invalid_argument("invalid seed projection slot");
    auto& slot=*state->slots[slotId];
    if(!generation || generation!=slot.generation) throw std::invalid_argument("stale seed projection generation");
    if(first>=slot.readNodes.size()) throw std::invalid_argument("invalid first projected read");
    size_t end=first;U hits=0;
    while(end<slot.readNodes.size() && slot.hitCounts[end]<=state->hitCapacity-hits)
        hits+=slot.hitCounts[end++];
    if(end==first || (!partial && end<slot.readNodes.size()))
        throw std::runtime_error("GPU hit capacity exceeded (mask=16); no alignments truncated");
    std::vector<SeedHitBatch> result(end-first);
    const auto* nodes=static_cast<const SeedNode*>(slot.hostEvents.p);
    auto* tasks=static_cast<HitTask*>(slot.hostHitTasks.p);
    unsigned taskCount=0,offset=0;
    auto* readTasks=state->filtering ? static_cast<unsigned*>(slot.hostReadTasks.p) : nullptr;
    for(size_t read=first;read<end;++read) {
        if(readTasks) readTasks[read-first]=taskCount;
        for(const unsigned node:slot.readNodes[read]) {
            if(taskCount>=state->eventCapacity) throw std::logic_error("invalid seed projection task count");
            tasks[taskCount++]={node,offset,slot.readLengths[read]};
            result[read-first].offsets.push_back(offset);
            const auto count=nodes[node].emission.match.upper-nodes[node].emission.match.lower+1;
            result[read-first].counts.push_back(count);
            result[read-first].expanded+=count;
            offset+=static_cast<unsigned>(count);
        }
    }
    if(readTasks) readTasks[end-first]=taskCount;
    if(offset!=hits) throw std::logic_error("invalid seed projection size");
    checked(cudaSetDevice(state->device));
    if(taskCount) {
        checked(cudaMemcpyAsync(slot.hitTasks.p,tasks,taskCount*sizeof(HitTask),cudaMemcpyHostToDevice,slot.stream.value));
        expandSeedHits<<<taskCount,32,0,slot.stream.value>>>(
            {static_cast<unsigned char*>(state->genome.p),static_cast<unsigned char*>(state->sa.p),state->genomeBytes,state->strand,state->records},
            static_cast<SeedNode*>(slot.events.p),static_cast<HitTask*>(slot.hitTasks.p),taskCount,
            static_cast<SeedProjectedHit*>(slot.hits.p));
        checked(cudaGetLastError());
        if(state->filtering) return filterProjected(first,end,slotId,taskCount,hits,std::move(result));
        checked(cudaMemcpyAsync(slot.hostHits.p,slot.hits.p,hits*sizeof(SeedProjectedHit),cudaMemcpyDeviceToHost,slot.stream.value));
        checked(cudaStreamSynchronize(slot.stream.value));
    }
    const auto* projected=static_cast<const SeedProjectedHit*>(slot.hostHits.p);
    offset=0;
    for(size_t read=first;read<end;++read) {
        auto& output=result[read-first];
        output.hits.assign(projected+offset,projected+offset+slot.hitCounts[read]);
        for(auto& start:output.offsets) start-=offset;
        offset+=static_cast<unsigned>(slot.hitCounts[read]);
    }
    return result;
}

std::vector<SeedHitBatch> GpuSeedIndex::filterProjected(size_t first,size_t end,size_t slotId,
        unsigned taskCount,U hits,std::vector<SeedHitBatch> result) {
    auto& slot=*state->slots[slotId];
    const unsigned reads=static_cast<unsigned>(end-first);
    checked(cudaMemcpyAsync(slot.readTasks.p,slot.hostReadTasks.p,(reads+1)*sizeof(unsigned),cudaMemcpyHostToDevice,slot.stream.value));
    filterSeedHits<<<reads,filterThreads,0,slot.stream.value>>>(state->filter,static_cast<SeedNode*>(slot.events.p),
        static_cast<HitTask*>(slot.hitTasks.p),static_cast<unsigned*>(slot.readTasks.p),reads,
        static_cast<SeedProjectedHit*>(slot.hits.p),static_cast<unsigned char*>(slot.flags.p),static_cast<unsigned*>(slot.taskKept.p));
    checked(cudaGetLastError());
    size_t temp=slot.selectTempBytes;
    checked(cub::DeviceSelect::Flagged(slot.selectTemp.p,temp,static_cast<SeedProjectedHit*>(slot.hits.p),
        static_cast<unsigned char*>(slot.flags.p),static_cast<SeedProjectedHit*>(slot.selected.p),
        static_cast<unsigned*>(slot.selectedCount.p),static_cast<int>(hits),slot.stream.value));
    checked(cudaMemcpyAsync(slot.hostTaskKept.p,slot.taskKept.p,taskCount*sizeof(unsigned),cudaMemcpyDeviceToHost,slot.stream.value));
    checked(cudaMemcpyAsync(slot.hostSelectedCount.p,slot.selectedCount.p,sizeof(unsigned),cudaMemcpyDeviceToHost,slot.stream.value));
    // Kept hits are compacted to the front of `selected`. Copy a guessed prefix in the same round trip;
    // only the first `selected` hits are ever read, so a too-large guess just moves unused bytes.
    const U guess=std::min<U>(hits,slot.keptGuess);
    if(guess) checked(cudaMemcpyAsync(slot.hostHits.p,slot.selected.p,guess*sizeof(SeedProjectedHit),cudaMemcpyDeviceToHost,slot.stream.value));
    checked(cudaStreamSynchronize(slot.stream.value));
    const unsigned selected=*static_cast<unsigned*>(slot.hostSelectedCount.p);
    const auto* kept=static_cast<const unsigned*>(slot.hostTaskKept.p);
    U total=0;for(unsigned t=0;t<taskCount;++t) total+=kept[t];
    if(total!=selected || selected>hits) throw std::runtime_error("GPU window filter count mismatch");
    if(selected>guess) {
        checked(cudaMemcpyAsync(static_cast<SeedProjectedHit*>(slot.hostHits.p)+guess,static_cast<SeedProjectedHit*>(slot.selected.p)+guess,
            (selected-guess)*sizeof(SeedProjectedHit),cudaMemcpyDeviceToHost,slot.stream.value));
        checked(cudaStreamSynchronize(slot.stream.value));
    }
    slot.keptGuess=std::max<U>(slot.keptGuess*7/8,U(selected)+U(selected)/4); // follows the batch size, decays slowly
    const auto* projected=static_cast<const SeedProjectedHit*>(slot.hostHits.p);
    unsigned task=0;size_t offset=0;
    for(auto& output:result) {
        size_t readHits=0;
        for(size_t piece=0;piece<output.counts.size();++piece,++task) {
            output.offsets[piece]=readHits;output.counts[piece]=kept[task];readHits+=kept[task];
        }
        output.hits.assign(projected+offset,projected+offset+readHits);
        offset+=readHits;
    }
    if(task!=taskCount || offset!=selected) throw std::logic_error("invalid filtered projection layout");
    return result;
}
