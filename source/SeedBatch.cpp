#include "SeedBatch.h"
#include "PreparedRead.h"
#include "SeedSearch.h"
#include "Genome.h"
#include "SequenceFuns.h"
#include "StitchBatch.h"
#include "StitchCoreBridge.h"
#ifdef STAR_HAVE_CUDA_SEEDS
#include "gpuSeedSearch.h"
#include "gpuStitch.h"
#include "gpuClip.h"
#include "gpuBarcode.h"
#endif
#include "ClipCR4.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <exception>
#include <sstream>
#include <thread>

namespace {
using Clock=std::chrono::steady_clock;
double seconds(Clock::time_point t) {return std::chrono::duration<double>(Clock::now()-t).count();}
}
struct SeedBatchExecutor::State {
    Parameters& p;
    SeedRoundIndex index;
    SeedRoundConfig config;
    struct Stats { uint64 reads=0,rounds=0,cpu=0,gpu=0,maxBatch=0,gpuBatches=0,hits=0,expanded=0,hitBatches=0,generation=0; double elapsed=0,seedSeconds=0,barcodeSeconds=0,projectSeconds=0; };
    std::vector<Stats> stats;
#ifdef STAR_HAVE_CUDA_SEEDS
    std::unique_ptr<GpuSeedIndex> gpu;
    std::unique_ptr<GpuStitcher> stitcher;
    std::unique_ptr<GpuClipper> clipper;
    std::unique_ptr<GpuWhitelist> whitelist;
#endif
    bool windowFilter=false;
    std::thread setup;                    // device setup, overlapped with the first CPU-mapped chunks
    std::atomic<bool> settled{true};
    std::exception_ptr setupError;
    std::ostringstream setupLog;          // written to Log.out by report(), not from the setup thread
    ~State() {if(setup.joinable()) setup.join();}
    State(Parameters& parameters,Genome& genome):p(parameters),
        index{static_cast<SeedIndexView>(genome),genome.SAi,genome.genomeSAindexStart,p.pGe.gSAindexNbases,p.pGe.gSAsparseD,
              genome.SAiMarkAbsentMaskC,genome.SAiMarkNmaskC,genome.SAiMarkNmask},
        config{p.maxNsplit,p.seed.splitMin,p.seed.mapMin,p.seed.searchStartLmax,p.seed.searchLmax,p.seed.searchStartLmaxOverLread,
               p.seed.multimapNmax,p.seed.perReadNmax},
        stats(2*size_t(p.runThreadN)) {} // two seed slots per worker
};
SeedBatchExecutor::SeedBatchExecutor(Parameters& p,Genome& genome):state(std::make_unique<State>(p,genome)) {
    if(p.seed.searchBackend!="cpu" && p.seed.searchBackend!="auto" && p.seed.searchBackend!="required")
        throw std::invalid_argument("seedSearchBackend must be off, cpu, auto or required");
    if(!p.seed.batchSize || p.seed.batchSize>65536)
        throw std::invalid_argument("seedBatchSize must be in [1,65536]");
    if(p.twoPass.yes || p.outFilterBySJoutStage!=0 || p.wasp.yes || p.peOverlap.yes || p.pGe.gType==101 || p.pCh.segmentMin>0)
        throw std::invalid_argument("batched seeds currently support fixed-index one-pass mapping without WASP, BySJout, overlap or chimeric remapping");
    if(p.runThreadN>1 && p.outMultimapperOrder.random)
        throw std::invalid_argument("batched seeds with random multimapper ordering require one logical worker");
    if(std::getenv("STAR_SEED_TRACE_DIR"))
        throw std::invalid_argument("capture the original CPU path; legacy seed tracing does not cover batched searches");
    if(p.seed.searchBackend!="cpu") {
#ifdef STAR_HAVE_CUDA_SEEDS
      state->settled=false;
      state->setup=std::thread([this,&p,&genome]{
      auto& log=state->setupLog;
      try {
        auto start=Clock::now();
        try {
            // Projected hits per device round; a batch whose hits exceed it is projected in several rounds.
            const char* hitEnv=std::getenv("STAR_GPU_HIT_CAPACITY");
            const size_t hitCapacity=hitEnv ? std::strtoull(hitEnv,nullptr,10)
                : std::min<size_t>(4194304,std::max<size_t>(262144,512*size_t(p.seed.batchSize))); // ~1 round per batch
            state->gpu=std::make_unique<GpuSeedIndex>(genome.G,genome.nGenome,genome.SA.charArray,genome.SA.lengthByte,
                genome.nSA,genome.GstrandBit,p.seed.batchSize,4*1024*1024,2*size_t(p.runThreadN));
            state->gpu->configureAdaptive(genome.SAi.charArray,genome.SAi.lengthByte,genome.SAi.length,
                genome.SAi.wordLength,genome.genomeSAindexStart,p.pGe.gSAindexNbases,p.pGe.gSAsparseD,
                genome.SAiMarkAbsentMaskC,genome.SAiMarkNmaskC,genome.SAiMarkNmask,state->config,131072,hitCapacity);
            const char* filter=std::getenv("STAR_GPU_WINDOW_FILTER");
            state->windowFilter=!(filter && std::string(filter)=="0");
            if(state->windowFilter)
                state->gpu->configureWindowFilter(genome.sjDstart,genome.sjAstart,genome.sjdbN,genome.sjGstart,
                    genome.sjdbLength,genome.sjdbOverhang,static_cast<unsigned>(p.win.binNbits),p.win.anchorDistNbins,
                    p.win.flankNbins,p.win.anchorMultimapNmax);
        } catch(const std::exception& error) {
            state->gpu.reset();
            if(p.seed.searchBackend=="required") throw;
            log<<"SEED_BATCH preflight_cpu_fallback="<<error.what()<<'\n';
        }
        const char* stitchEnv=std::getenv("STAR_GPU_STITCH");
        if(state->gpu && !(stitchEnv && std::string(stitchEnv)=="0")) {
            try {
                GpuStitchGenome g;
                g.deviceGenome=state->gpu->deviceGenome(); g.nGenome=genome.nGenome;
                g.chrStart.assign(genome.chrStart.begin(),genome.chrStart.end());
                g.chrLength.assign(genome.chrLength.begin(),genome.chrLength.end());
                g.sjdbN=genome.sjdbN;
                g.sjdbStart=reinterpret_cast<const uint64_t*>(genome.sjdbStart);
                g.sjdbEnd=reinterpret_cast<const uint64_t*>(genome.sjdbEnd);
                g.sjdbMotif=genome.sjdbMotif; g.sjdbShiftLeft=genome.sjdbShiftLeft;
                g.sjdbShiftRight=genome.sjdbShiftRight; g.sjdbStrand=genome.sjdbStrand;
                state->stitcher=std::make_unique<GpuStitcher>(g,stitchcore::paramsFrom(p),2*size_t(p.runThreadN), // two batches in flight per worker
                    static_cast<unsigned>(p.seed.perWindowNmax+1));
            } catch(const std::exception& error) {
                state->stitcher.reset();
                if(p.seed.searchBackend=="required") throw;
                log<<"SEED_BATCH stitch_cpu_fallback="<<error.what()<<'\n';
            }
        }
        const char* clipEnv=std::getenv("STAR_GPU_CLIP");
        if(state->gpu && !(clipEnv && std::string(clipEnv)=="0")) {
            try {
                state->clipper=std::make_unique<GpuClipper>(size_t(p.runThreadN));
            } catch(const std::exception& error) {
                state->clipper.reset();
                if(p.seed.searchBackend=="required") throw;
                log<<"SEED_BATCH clip_cpu_fallback="<<error.what()<<'\n';
            }
        }
        const char* barcodeEnv=std::getenv("STAR_GPU_BARCODE");
        const auto& solo=p.pSolo;
        if(state->gpu && !(barcodeEnv && std::string(barcodeEnv)=="0") && solo.type==solo.SoloTypes::CB_UMI_Simple
           && solo.cbWLyes && solo.CBtype.type==1 && p.readFiles.typeN!=10 && !solo.cbWL.empty()
           && solo.cbL>0 && solo.cbL<=GpuWhitelist::kMaxLength) {
            try {
                state->whitelist=std::make_unique<GpuWhitelist>(solo.cbWL.data(),solo.cbWL.size(),solo.cbL,2*size_t(p.runThreadN));
            } catch(const std::exception& error) {
                state->whitelist.reset();
                if(p.seed.searchBackend=="required") throw;
                log<<"SEED_BATCH barcode_cpu_fallback="<<error.what()<<'\n';
            }
        }
        if(state->gpu) log<<"SEED_BATCH setup_seconds="<<seconds(start)
            <<" index_upload_seconds="<<state->gpu->indexUploadSeconds<<" device_bytes="<<state->gpu->deviceBytes<<" pinned_bytes="<<state->gpu->pinnedBytes
            <<" scheduler=device_adaptive consolidation=device hit_expansion=device window_filter="
            <<(state->windowFilter ? "device":"off")<<" stitch="<<(state->stitcher ? "device":"cpu")
            <<" stitch_device_bytes="<<(state->stitcher ? state->stitcher->deviceBytes : 0)
            <<" clip="<<(state->clipper ? "device":"cpu")<<" barcode="<<(state->whitelist ? "device":"cpu")<<"\n";
      } catch(...) {state->setupError=std::current_exception();}
      state->settled.store(true,std::memory_order_release);
      });
#else
        if(p.seed.searchBackend=="required") throw std::runtime_error("CUDA seed backend unavailable in this CPU-only build");
        p.inOut->logMain<<"SEED_BATCH preflight_cpu_fallback=CPU-only build\n";
#endif
    }
}
SeedBatchExecutor::~SeedBatchExecutor()=default;
bool SeedBatchExecutor::clip(const ClipBatch& batch,unsigned worker) {
#ifdef STAR_HAVE_CUDA_SEEDS
    if(!state->settled.load(std::memory_order_acquire) || !state->clipper) return false;
    if(batch.queryLen==0 || batch.queryLen>GpuClipper::kMaxQuery) return false;
    GpuClipScoring sc;
    sc.readLen=batch.targetLen; sc.match=batch.match; sc.mismatch=batch.mismatch; sc.gapOpen=batch.gapOpen; sc.gapExt=batch.gapExt;
    state->clipper->align(sc,batch.codes,batch.n,batch.query,batch.queryLen,batch.out,worker);
    return true;
#else
    (void)batch; (void)worker;
    return false;
#endif
}
bool SeedBatchExecutor::deviceSettled() const {
    // CPU warm-up while the device sets up only in auto mode: required means every read is seeded on the device.
    // STAR_GPU_SETUP_OVERLAP=0 also disables it (tests that must exercise the device).
    static const bool overlapEnv=[]{const char* v=std::getenv("STAR_GPU_SETUP_OVERLAP"); return !(v && std::string(v)=="0");}();
    const bool overlap=overlapEnv && state->p.seed.searchBackend=="auto";
    if(!overlap) while(!state->settled.load(std::memory_order_acquire)) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(!state->settled.load(std::memory_order_acquire)) return false;
    if(state->setupError) std::rethrow_exception(state->setupError);
    return true;
}

void SeedBatchExecutor::run(std::vector<PreparedRead>& reads,unsigned worker) {
    auto start=Clock::now();auto& s=*state;auto& stats=s.stats.at(worker);
    std::vector<char> pool,complement;
    std::vector<uint64> offsets;
    for(const auto& read:reads) { offsets.push_back(pool.size());pool.insert(pool.end(),read.sequence.begin(),read.sequence.end()); }
    if(pool.size()>4*1024*1024) throw std::runtime_error("seed read pool budget exceeded");
#ifdef STAR_HAVE_CUDA_SEEDS
    if(s.gpu && (s.p.seed.searchBackend=="required" || reads.size()>=256)) {
        std::vector<SeedReadRange> ranges;
        for(size_t i=0;i<reads.size();++i) ranges.push_back({offsets[i],reads[i].sequence.size()});
        // The barcode lookup needs only the reads: queue it first, so its round trip overlaps the seed search.
        if(s.whitelist) {const auto b=Clock::now(); barcodeSubmit(reads,worker); stats.barcodeSeconds+=seconds(b);}
        const auto seedStart=Clock::now();
        auto result=s.gpu->seedReads(pool,ranges,worker,true);
        stats.seedSeconds+=seconds(seedStart);
        for(size_t i=0;i<reads.size();++i) {
            reads[i].seeds=std::move(result.reads[i]);
            reads[i].seedSummary=result.summaries[i];reads[i].deviceConsolidated=true;
        }
        if(s.whitelist) {const auto b=Clock::now(); barcodeCollect(reads,worker); stats.barcodeSeconds+=seconds(b);}
        stats.gpu+=result.searches;++stats.gpuBatches;stats.generation=result.generation;
        stats.maxBatch=std::max(stats.maxBatch,uint64(reads.size()));
        stats.reads+=reads.size();stats.elapsed+=seconds(start);
        return;
    }
#endif
    complement.resize(pool.size());complementSeqNumbers(pool.data(),complement.data(),pool.size());
    std::vector<SeedRoundTask> tasks;tasks.reserve(reads.size());
    std::vector<size_t> pending,next;
    std::vector<SeedQuery> queries;
    std::vector<SeedMatch> results;
    size_t events=0;
    for(size_t i=0;i<reads.size();++i) {
        tasks.push_back(seedRounds(reads[i].sequence,offsets[i],s.index,s.config,reads[i].seeds,events));
        if(tasks.back().advance()) pending.push_back(i);
    }
    const SeedSearchEngine cpu(s.index.search);
    while(!pending.empty()) {
        queries.clear();for(const auto i:pending) queries.push_back(tasks[i].query());
        stats.maxBatch=std::max(stats.maxBatch,uint64(queries.size()));++stats.rounds;
        results.resize(queries.size());cpu.searchBatch(pool,complement,queries,results);
        stats.cpu+=queries.size();
        next.clear();
        for(size_t i=0;i<pending.size();++i) {
            auto& task=tasks[pending[i]];task.complete(results[i]);
            if(task.advance()) next.push_back(pending[i]);
        }
        pending.swap(next);
    }
    stats.reads+=reads.size();stats.elapsed+=seconds(start);
}
size_t SeedBatchExecutor::project([[maybe_unused]] std::vector<PreparedRead>& reads,[[maybe_unused]] unsigned worker,[[maybe_unused]] size_t first) {
#ifdef STAR_HAVE_CUDA_SEEDS
    auto start=Clock::now();auto& s=*state;auto& stats=s.stats.at(worker);
    if(!s.gpu) throw std::logic_error("GPU projection without device owner");
    auto result=s.gpu->projectPrepared(first,worker,stats.generation);
    if(result.empty() || result.size()>reads.size()-first) throw std::logic_error("invalid GPU projection prefix");
    for(size_t i=0;i<result.size();++i) {
        stats.hits+=result[i].hits.size();stats.expanded+=result[i].expanded;
        reads[first+i].projected=std::move(result[i]);
    }
    ++stats.hitBatches;stats.elapsed+=seconds(start);stats.projectSeconds+=seconds(start);
    return first+result.size();
#else
    throw std::logic_error("GPU projection in CPU-only build");
#endif
}
namespace {
// Keys of the batch whose barcode lookup is in flight on this thread (submit and collect run on one thread).
thread_local std::vector<uint64> barcodeKeys;
thread_local std::vector<size_t> barcodeOwner;
}
void SeedBatchExecutor::barcodeSubmit(const std::vector<PreparedRead>& reads,unsigned worker) {
#ifdef STAR_HAVE_CUDA_SEEDS
    // Same barcode substring and conversion as SoloReadBarcode::getCBandUMI; reads with Ns
    // or a short barcode read keep no hint and are matched on the CPU as before.
    const auto& solo=state->p.pSolo;
    auto& keys=barcodeKeys; auto& owner=barcodeOwner;
    keys.clear(); owner.clear();
    const size_t start=solo.cbS-1;
    for(size_t i=0;i<reads.size();++i) {
        const auto& ends=reads[i].ends;
        if(solo.barcodeRead>=ends.size()) continue;
        const std::string_view seq=reads[i].view(ends[solo.barcodeRead].sequence);
        if(seq.size()<start+solo.cbL) continue;
        uint64 key=0;
        if(convertNuclStrToInt64(std::string(seq.substr(start,solo.cbL)),key)!=-1) continue;
        keys.push_back(key); owner.push_back(i);
    }
    state->whitelist->submit(keys.data(),keys.size(),worker);
#else
    (void)reads;(void)worker;
#endif
}
void SeedBatchExecutor::barcodeCollect(std::vector<PreparedRead>& reads,unsigned worker) {
#ifdef STAR_HAVE_CUDA_SEEDS
    const auto& keys=barcodeKeys; const auto& owner=barcodeOwner;
    thread_local std::vector<int64_t> exact;
    thread_local std::vector<uint64_t> mask;
    exact.resize(keys.size()); mask.resize(keys.size());
    state->whitelist->collect(exact.data(),mask.data(),worker);
    for(size_t k=0;k<keys.size();++k) {
        auto& h=reads[owner[k]].barcode;
        h.key=keys[k]; h.exact=exact[k]; h.mask=mask[k];
    }
#else
    (void)reads;(void)worker;
#endif
}
bool SeedBatchExecutor::stitchEnabled() const {
#ifdef STAR_HAVE_CUDA_SEEDS
    return state->settled.load(std::memory_order_acquire) && state->stitcher!=nullptr;
#else
    return false;
#endif
}
void SeedBatchExecutor::stitch(const StitchBatch& batch,StitchResults& results,unsigned worker) {
#ifdef STAR_HAVE_CUDA_SEEDS
    if(!state->stitcher) throw std::logic_error("GPU stitch without device owner");
    state->stitcher->run(batch,results,worker); // worker: a stitch slot, 2*thread+parity
#else
    (void)batch;(void)results;(void)worker;
    throw std::logic_error("GPU stitch in CPU-only build");
#endif
}
void SeedBatchExecutor::report() {
    auto& log=state->p.inOut->logMain;
    if(state->setup.joinable()) state->setup.join();
    log<<state->setupLog.str();
#ifdef STAR_HAVE_CUDA_SEEDS
    if(state->clipper) {
        double clipSeconds=0; uint64_t clipReads=0;
        state->clipper->totals(clipSeconds,clipReads);
        log<<"GPU_CLIP reads="<<clipReads<<" seconds="<<clipSeconds<<'\n';
    }
    if(state->whitelist) {
        double barcodeSeconds=0; uint64_t barcodeReads=0;
        state->whitelist->totals(barcodeSeconds,barcodeReads);
        log<<"GPU_BARCODE reads="<<barcodeReads<<" seconds="<<barcodeSeconds<<" device_bytes="<<state->whitelist->deviceBytes<<'\n';
    }
    if(state->stitcher) for(size_t i=0;i<state->stitcher->stats.size();++i) {
        const auto& s=state->stitcher->stats[i];
        log<<"GPU_STITCH worker="<<i<<" batches="<<s.batches<<" reads="<<s.reads<<" windows="<<s.windows
            <<" transcripts="<<s.transcripts<<" rows="<<s.rows<<" cpu_fallback_reads="<<s.fallbacks
            <<" stack_overflow="<<s.stackOverflow<<" slot_overflow="<<s.slotOverflow<<" output_overflow="<<s.outputOverflow
            <<" upload_bytes="<<s.uploadBytes
            <<" download_bytes="<<s.downloadBytes<<" seconds="<<s.seconds<<" kernel_seconds="<<s.kernelSeconds<<'\n';
    }
#endif
    for(size_t i=0;i<state->stats.size();++i) {
        const auto& s=state->stats[i];
        log<<"SEED_BATCH worker="<<i<<" reads="<<s.reads<<" rounds="<<s.rounds<<" max_batch="<<s.maxBatch
            <<" cpu_queries="<<s.cpu<<" gpu_queries="<<s.gpu<<" worker_seconds="<<s.elapsed
            <<" gpu_batches="<<s.gpuBatches<<" gpu_projected_hits="<<s.expanded<<" gpu_retained_hits="<<s.hits<<" gpu_hit_batches="<<s.hitBatches
            <<" seed_seconds="<<s.seedSeconds<<" barcode_seconds="<<s.barcodeSeconds<<" project_seconds="<<s.projectSeconds<<'\n';
    }
}
