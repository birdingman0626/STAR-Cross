#include "gpuStitch.h"
#include <cuda_runtime.h>
#include <chrono>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using stitchcore::u64;
using stitchcore::Tr;
using stitchcore::Frame;
using Clock=std::chrono::steady_clock;
void checked(cudaError_t e) {if(e!=cudaSuccess) throw std::runtime_error(std::string("GPU stitch: ")+cudaGetErrorString(e));}

// Growable device buffer; contents are not preserved on growth.
struct DeviceArray {
    void* p=nullptr; size_t bytes=0;
    ~DeviceArray() {if(p) cudaFree(p);}
    void* reserve(size_t n,uint64_t& total) {
        if(n<=bytes) return p;
        if(p) {checked(cudaFree(p)); total-=bytes; p=nullptr; bytes=0;}
        const size_t want=n+n/4+256;
        checked(cudaMalloc(&p,want)); bytes=want; total+=want;
        return p;
    }
};

// Growable pinned host buffer for one-copy staging; contents are not preserved on growth.
struct PinnedArray {
    void* p=nullptr; size_t bytes=0;
    ~PinnedArray() {if(p) cudaFreeHost(p);}
    char* reserve(size_t n) {
        if(n<=bytes) return static_cast<char*>(p);
        if(p) {checked(cudaFreeHost(p)); p=nullptr; bytes=0;}
        const size_t want=n+n/4+4096;
        checked(cudaHostAlloc(&p,want,cudaHostAllocDefault)); bytes=want;
        return static_cast<char*>(p);
    }
};
inline size_t aligned(size_t n) {return (n+255)&~size_t(255);}

__global__ void stitchReadsKernel(stitchcore::Params P,stitchcore::GenomeView gv,
        const StitchReadDesc* reads,const unsigned* order,unsigned nReads,const char* seq,const StitchWindowDesc* windows,const u64* wa,
        Frame* stacks,unsigned framesPerRead,Tr* slots,unsigned slotsPerRead,
        StitchReadResult* readResults,StitchWindowResult* windowResults,stitchcore::TrHead* heads,stitchcore::TrRow* rows,
        unsigned long long* cursor,unsigned long long headCapacity,unsigned long long rowCapacity) {
    const unsigned lane=blockIdx.x*blockDim.x+threadIdx.x;
    if(lane>=nReads) return;
    const unsigned r=order[lane];  // reads sorted by estimated work, so a warp's reads finish together
    const StitchReadDesc d=reads[r];
    StitchReadResult out;
    out.status=stitchOk; out.mates[0]=out.mates[1]=out.mates[2]=0; out.resultBase=0; out.resultCount=0;
    Frame* stack=stacks+size_t(lane)*framesPerRead;
    Tr* mine=slots+size_t(lane)*slotsPerRead;
    stitchcore::ReadView forward,reverse;
    forward.R=seq+d.seqOffset; reverse.R=seq+d.seqOffset+d.Lread;
    forward.Lread=reverse.Lread=d.Lread;
    forward.mismatchLimit=reverse.mismatchLimit=d.mismatchLimit;
    for(unsigned i=0;i<stitchcore::maxMates;++i) forward.readLength[i]=reverse.readLength[i]=d.readLength[i];
    for(unsigned k=0;k<d.windowCount;++k) {windowResults[d.windowBegin+k].count=0; windowResults[d.windowBegin+k].first=0;}
    // Later windows read only maxScoreMate and the transcript total, never earlier
    // transcripts, so each window is packed to the output as soon as it is done
    // and every window gets all of the read's slots.
    u64 trNtotal=0;
    unsigned produced=0;
    Tr* pointer[32];
    for(unsigned k=0;k<d.windowCount;++k) {
        const StitchWindowDesc W=windows[d.windowBegin+k];
        if (trNtotal+P.transcriptsPerWindowNmax >= P.transcriptsPerReadNmax) break; // CPU logs and breaks
        for(unsigned s=0;s<slotsPerRead;++s) pointer[s]=&mine[s];
        Tr& init=*pointer[0];
        stitchcore::resetTr(init);
        init.nExons=0; init.Chr=W.Chr; init.Str=W.Str; init.roStr=W.roStr; init.maxScore=0;
        init.intronMotifs[0]=init.intronMotifs[1]=init.intronMotifs[2]=0;
        init.sjMotifStrand=0; init.sjYes=false; init.iFrag=0; init.mappedLength=0;
        stitchcore::WindowOut w{pointer,0,slotsPerRead,false};
        const bool ok=stitchcore::stitchWindow(wa+W.waOffset*stitchcore::waSize,W.nA,init,P,
            W.roStr==0 ? forward : reverse,gv,stack,framesPerRead,w,out.mates);
        if(!ok) out.status|=stitchStackOverflow;
        if(w.overflow) out.status|=stitchSlotOverflow;
        if(out.status) break;
        if(w.n) {
            unsigned long long nRows=0;
            for(u64 j=0;j<w.n;++j) nRows+=stitchcore::liveRows(*w.slot[j]);
            // cursor[0]: headers, cursor[1]: rows. A read that fails later leaves a gap, which is harmless.
            const unsigned long long base=atomicAdd(cursor,static_cast<unsigned long long>(w.n));
            unsigned long long row=atomicAdd(cursor+1,nRows);
            if(base+w.n>headCapacity || row+nRows>rowCapacity) {out.status|=stitchOutputOverflow; break;}
            for(u64 j=0;j<w.n;++j) {
                stitchcore::packTr(*w.slot[j],heads[base+j],rows+row,row);
                row+=stitchcore::liveRows(*w.slot[j]);
            }
            windowResults[d.windowBegin+k].first=static_cast<unsigned>(base);  // absolute; resultBase stays 0
        }
        windowResults[d.windowBegin+k].count=static_cast<unsigned>(w.n);
        trNtotal+=w.n; produced+=static_cast<unsigned>(w.n);
    }
    if(out.status==stitchOk) out.resultCount=produced;
    readResults[r]=out;
}
} // namespace

struct GpuStitcher::State {
    struct Slot {
        cudaStream_t stream=nullptr;
        cudaEvent_t kernelStart=nullptr,kernelEnd=nullptr;
        DeviceArray up,stacks,slots,results,heads,rows;  // results: read results, window results, cursors
        PinnedArray hostUp,hostDown,hostOut;
        unsigned long long headGuess=0,rowGuess=0; // heads/rows downloaded with the small results (recent batches)
        std::vector<unsigned long long> cost; std::vector<unsigned> order, lanes;
        ~Slot() {if(kernelStart) cudaEventDestroy(kernelStart); if(kernelEnd) cudaEventDestroy(kernelEnd); if(stream) cudaStreamDestroy(stream);}
    };
    int device=0;
    stitchcore::Params params;
    stitchcore::GenomeView view;
    DeviceArray chrStart,chrLength,sjdbStart,sjdbEnd,sjdbMotif,sjdbShiftLeft,sjdbShiftRight,sjdbStrand;
    std::vector<std::unique_ptr<Slot>> slots;
    unsigned framesPerRead=0,slotsPerRead=0;
};

GpuStitcher::GpuStitcher(const GpuStitchGenome& g,const stitchcore::Params& params,size_t slots,
        unsigned framesPerRead,unsigned slotsPerRead):state(nullptr) {
    if(!g.deviceGenome || !g.nGenome || g.chrLength.empty() || g.chrStart.size()<g.chrLength.size()
       || !slots || !framesPerRead || !slotsPerRead || slotsPerRead>32 || (g.sjdbN && (!g.sjdbStart || !g.sjdbEnd || !g.sjdbMotif
       || !g.sjdbShiftLeft || !g.sjdbShiftRight || !g.sjdbStrand)))
        throw std::invalid_argument("invalid GPU stitch configuration");
    auto s=std::make_unique<State>();
    checked(cudaGetDevice(&s->device));
    s->params=params; s->framesPerRead=framesPerRead; s->slotsPerRead=slotsPerRead;
    auto upload=[&](DeviceArray& a,const void* host,size_t bytes) {
        void* p=a.reserve(bytes ? bytes : 1,deviceBytes);
        if(bytes) checked(cudaMemcpy(p,host,bytes,cudaMemcpyHostToDevice));
        return p;
    };
    auto& v=s->view;
    v.G=static_cast<const char*>(g.deviceGenome); v.nGenome=g.nGenome;
    v.chrStart=static_cast<const u64*>(upload(s->chrStart,g.chrStart.data(),g.chrStart.size()*sizeof(u64)));
    v.chrLength=static_cast<const u64*>(upload(s->chrLength,g.chrLength.data(),g.chrLength.size()*sizeof(u64)));
    v.sjdbN=g.sjdbN;
    v.sjdbStart=static_cast<const u64*>(upload(s->sjdbStart,g.sjdbStart,g.sjdbN*sizeof(u64)));
    v.sjdbEnd=static_cast<const u64*>(upload(s->sjdbEnd,g.sjdbEnd,g.sjdbN*sizeof(u64)));
    v.sjdbMotif=static_cast<const unsigned char*>(upload(s->sjdbMotif,g.sjdbMotif,g.sjdbN));
    v.sjdbShiftLeft=static_cast<const unsigned char*>(upload(s->sjdbShiftLeft,g.sjdbShiftLeft,g.sjdbN));
    v.sjdbShiftRight=static_cast<const unsigned char*>(upload(s->sjdbShiftRight,g.sjdbShiftRight,g.sjdbN));
    v.sjdbStrand=static_cast<const unsigned char*>(upload(s->sjdbStrand,g.sjdbStrand,g.sjdbN));
    for(size_t i=0;i<slots;++i) {
        auto slot=std::make_unique<State::Slot>();
        checked(cudaStreamCreateWithFlags(&slot->stream,cudaStreamNonBlocking));
        checked(cudaEventCreate(&slot->kernelStart)); checked(cudaEventCreate(&slot->kernelEnd));
        s->slots.push_back(std::move(slot));
    }
    stats.resize(slots);
    state=s.release();
}
GpuStitcher::~GpuStitcher() {delete state;}

void GpuStitcher::run(const StitchBatch& batch,StitchResults& results,size_t slotId) {
    if(slotId>=state->slots.size()) throw std::invalid_argument("invalid GPU stitch slot");
    const auto start=Clock::now();
    auto& slot=*state->slots[slotId];
    auto& st=stats[slotId];
    const size_t nReads=batch.reads.size(),nWindows=batch.windows.size();
    results.reads.assign(nReads,StitchReadResult{});
    results.windows.assign(nWindows,StitchWindowResult{});
    results.transcripts.clear(); results.rows.clear(); results.batch=&batch;
    if(!nReads) return;
    if(nReads>(1u<<24)) throw std::invalid_argument("GPU stitch batch too large");
    checked(cudaSetDevice(state->device));
    auto& total=deviceBytes;
    const unsigned F=state->framesPerRead,S=state->slotsPerRead;
    // One upload: read descriptors, sequences, windows and anchor rows packed into pinned memory.
    const size_t bReads=nReads*sizeof(StitchReadDesc), bSeq=batch.seq.size(), bWindows=nWindows*sizeof(StitchWindowDesc),
                 bWa=batch.wa.size()*sizeof(u64);
    const size_t bOrder=nReads*sizeof(unsigned);
    const size_t oSeq=aligned(bReads), oWindows=oSeq+aligned(bSeq), oWa=oWindows+aligned(bWindows), oOrder=oWa+aligned(bWa),
                 upBytes=oOrder+aligned(bOrder);
    char* hUp=slot.hostUp.reserve(upBytes);
    std::memcpy(hUp,batch.reads.data(),bReads);
    if(bSeq) std::memcpy(hUp+oSeq,batch.seq.data(),bSeq);
    if(bWindows) std::memcpy(hUp+oWindows,batch.windows.data(),bWindows);
    if(bWa) std::memcpy(hUp+oWa,batch.wa.data(),bWa);
    {   // Work estimate: the stitching search grows with anchors per window, roughly quadratically.
        auto& cost=slot.cost; cost.resize(nReads);
        auto& order=slot.order; order.resize(nReads);
        for(size_t i=0;i<nReads;++i) {
            const auto& rd=batch.reads[i]; unsigned long long c=1;
            for(unsigned k=0;k<rd.windowCount;++k) {const auto n=batch.windows[rd.windowBegin+k].nA; c+=n*n;}
            cost[i]=c; order[i]=static_cast<unsigned>(i);
        }
        std::stable_sort(order.begin(),order.end(),[&](unsigned a,unsigned b){return cost[a]>cost[b];});
        // Divergent lanes of a warp run one after another, so a warp costs about the sum of
        // its reads. Deal reads round-robin by rank so every warp gets a similar total.
        auto& lanes=slot.lanes; lanes.resize(nReads);
        const size_t nWarps=(nReads+31)/32;
        size_t lane=0;
        for(size_t w=0;w<nWarps;++w)
            for(size_t rank=w;rank<nReads;rank+=nWarps) lanes[lane++]=order[rank];
        std::memcpy(hUp+oOrder,lanes.data(),bOrder);
    }
    char* dUp=static_cast<char*>(slot.up.reserve(upBytes,total));
    auto* dStacks=slot.stacks.reserve(nReads*F*sizeof(Frame),total);
    auto* dSlots=slot.slots.reserve(nReads*S*sizeof(Tr),total);
    // Small results in one block so they come back in one copy: cursors, read results, window results.
    const size_t bReadResults=nReads*sizeof(StitchReadResult), bWindowResults=nWindows*sizeof(StitchWindowResult);
    const size_t oReadResults=256, oWindowResults=oReadResults+aligned(bReadResults), smallBytes=oWindowResults+aligned(bWindowResults);
    char* dSmall=static_cast<char*>(slot.results.reserve(smallBytes,total));
    // Batch output budget: S transcripts per read on average (about 5 are used)
    // and 2 exon rows per transcript (about 1.05 are used). A read whose output
    // does not fit falls back to the CPU.
    const unsigned long long headCapacity=nReads*S, rowCapacity=headCapacity*2;
    auto* dHeads=slot.heads.reserve(headCapacity*sizeof(stitchcore::TrHead),total);
    auto* dRows=slot.rows.reserve(rowCapacity*sizeof(stitchcore::TrRow),total);
    cudaStream_t stream=slot.stream;
    checked(cudaMemcpyAsync(dUp,hUp,upBytes,cudaMemcpyHostToDevice,stream));
    checked(cudaMemsetAsync(dSmall,0,2*sizeof(unsigned long long),stream));
    const unsigned threads=64,blocks=static_cast<unsigned>((nReads+threads-1)/threads);
    checked(cudaEventRecord(slot.kernelStart,stream));
    stitchReadsKernel<<<blocks,threads,0,stream>>>(state->params,state->view,
        reinterpret_cast<const StitchReadDesc*>(dUp),reinterpret_cast<const unsigned*>(dUp+oOrder),static_cast<unsigned>(nReads),dUp+oSeq,
        reinterpret_cast<const StitchWindowDesc*>(dUp+oWindows),reinterpret_cast<const u64*>(dUp+oWa),
        static_cast<Frame*>(dStacks),F,static_cast<Tr*>(dSlots),S,
        reinterpret_cast<StitchReadResult*>(dSmall+oReadResults),reinterpret_cast<StitchWindowResult*>(dSmall+oWindowResults),
        static_cast<stitchcore::TrHead*>(dHeads),static_cast<stitchcore::TrRow*>(dRows),
        reinterpret_cast<unsigned long long*>(dSmall),headCapacity,rowCapacity);
    checked(cudaGetLastError());
    checked(cudaEventRecord(slot.kernelEnd,stream));
    char* hSmall=slot.hostDown.reserve(smallBytes);
    checked(cudaMemcpyAsync(hSmall,dSmall,smallBytes,cudaMemcpyDeviceToHost,stream));
    // Heads and rows are written from the front of their arrays. Copy a guessed prefix of each in the
    // same round trip; only the produced ones are read, so a too-large guess just moves unused bytes.
    const unsigned long long headGuess=std::min(headCapacity,slot.headGuess), rowGuess=std::min(rowCapacity,slot.rowGuess);
    const size_t gHeads=headGuess*sizeof(stitchcore::TrHead), gRows=rowGuess*sizeof(stitchcore::TrRow);
    const size_t oOutRows=aligned(headCapacity*sizeof(stitchcore::TrHead));
    char* hOut=slot.hostOut.reserve(oOutRows+rowCapacity*sizeof(stitchcore::TrRow));
    if(gHeads) checked(cudaMemcpyAsync(hOut,dHeads,gHeads,cudaMemcpyDeviceToHost,stream));
    if(gRows) checked(cudaMemcpyAsync(hOut+oOutRows,dRows,gRows,cudaMemcpyDeviceToHost,stream));
    checked(cudaStreamSynchronize(stream));
    unsigned long long produced[2]; std::memcpy(produced,hSmall,sizeof(produced));
    std::memcpy(results.reads.data(),hSmall+oReadResults,bReadResults);
    if(bWindowResults) std::memcpy(results.windows.data(),hSmall+oWindowResults,bWindowResults);
    const unsigned long long nHeads=produced[0]<headCapacity ? produced[0]:headCapacity;
    const unsigned long long nRows=produced[1]<rowCapacity ? produced[1]:rowCapacity;
    for(size_t i=0;i<nReads;++i) {results.reads[i].windowBegin=batch.reads[i].windowBegin; results.reads[i].windowCount=batch.reads[i].windowCount;}
    results.transcripts.resize(nHeads); results.rows.resize(nRows);
    const size_t bHeads=nHeads*sizeof(stitchcore::TrHead), bRows=nRows*sizeof(stitchcore::TrRow);
    if(bHeads>gHeads || bRows>gRows) {//the guess was short: fetch the rest
        if(bHeads>gHeads) checked(cudaMemcpyAsync(hOut+gHeads,static_cast<char*>(static_cast<void*>(dHeads))+gHeads,bHeads-gHeads,cudaMemcpyDeviceToHost,stream));
        if(bRows>gRows) checked(cudaMemcpyAsync(hOut+oOutRows+gRows,static_cast<char*>(static_cast<void*>(dRows))+gRows,bRows-gRows,cudaMemcpyDeviceToHost,stream));
        checked(cudaStreamSynchronize(stream));
    }
    if(bHeads) std::memcpy(results.transcripts.data(),hOut,bHeads);
    if(bRows) std::memcpy(results.rows.data(),hOut+oOutRows,bRows);
    slot.headGuess=std::max(slot.headGuess*7/8,nHeads+nHeads/4);
    slot.rowGuess=std::max(slot.rowGuess*7/8,nRows+nRows/4);
    ++st.batches; st.reads+=nReads; st.windows+=nWindows; st.transcripts+=nHeads; st.rows+=nRows;
    for(const auto& r:results.reads) if(r.status!=stitchOk) {
        ++st.fallbacks;
        if(r.status&stitchStackOverflow) ++st.stackOverflow;
        if(r.status&stitchSlotOverflow) ++st.slotOverflow;
        if(r.status&stitchOutputOverflow) ++st.outputOverflow;
    }
    st.uploadBytes+=nReads*sizeof(StitchReadDesc)+batch.seq.size()+nWindows*sizeof(StitchWindowDesc)+batch.wa.size()*sizeof(u64);
    st.downloadBytes+=nReads*sizeof(StitchReadResult)+nWindows*sizeof(StitchWindowResult)
        +nHeads*sizeof(stitchcore::TrHead)+nRows*sizeof(stitchcore::TrRow);
    float kernelMs=0; checked(cudaEventElapsedTime(&kernelMs,slot.kernelStart,slot.kernelEnd)); st.kernelSeconds+=kernelMs/1000.0;
    st.seconds+=std::chrono::duration<double>(Clock::now()-start).count();
}
