#include "ReadAlignChunk.h"
#include "AsyncByteWriter.h"
#include "GlobalVariables.h"
#include "ThreadControl.h"
#include "ErrorWarning.h"
#include "SeedBatch.h"
#include "PreparedRead.h"
#include "StitchBatch.h"
#include SAMTOOLS_BGZF_H
#include <chrono>
#include <future>
#include <cstdlib>
#include <memory>
#include <string>
using namespace std;
namespace {
using PhaseClock=std::chrono::steady_clock;
inline double since(PhaseClock::time_point& t) {auto n=PhaseClock::now(); double s=std::chrono::duration<double>(n-t).count(); t=n; return s;}
enum class StageState {empty, seeding, stitching, ready};
struct Stage {
    std::vector<PreparedRead> reads; StitchBatch sb; StitchResults sr;
    std::future<void> seeds, stitch; StageState state=StageState::empty;
};
constexpr unsigned maxStages=4;
// Batch vectors live as long as the ReadAlignChunk, so their capacity is reused from chunk
// to chunk instead of being reallocated for every chunk. Every chunk drains all stages.
struct MapBuffers {
    std::vector<PreparedRead> prepared;
    StitchBatch stitchBatch;
    StitchResults stitchResults;
    Stage stages[maxStages];
};

unsigned pipelineStages()
{// STAR_PIPELINE_STAGES=3 shortens the device ring (diagnostic)
    static const unsigned n=[]{
        const char* v=std::getenv("STAR_PIPELINE_STAGES");
        return v && std::string(v)=="3" ? 3u : maxStages;
    }();
    return n;
}

// Maps the reads of one chunk, one per nextRead() call, by one of three paths:
//  - pipelined (device stitching on): a ring of K batches per worker, in input order.
//    q[0] is aligned on the CPU, q[1] is stitched on the device, q[2..K-1] are seeded and
//    projected on the device. At most two batches seed and two stitch at once, so each has
//    two device slots per worker.
//  - batched: one batch at a time is loaded, seeded (and stitched) on the device, then aligned.
//  - CPU: the original synchronous mapper, used while the device index is still uploading.
// Every read still runs the same CPU steps in input order.
class ChunkMapper {
public:
    PhaseClock::time_point tick;

    ChunkMapper(ReadAlignChunk &chunk, SeedBatchExecutor *batchIn)
        : c(chunk), RA(chunk.RA), P(chunk.P), batch(batchIn),
          buffers(buffersOf(chunk)), prepared(buffers.prepared),
          stitchBatch(buffers.stitchBatch), stitchResults(buffers.stitchResults),
          nStages(pipelineStages())
    {
        prepared.clear();
        pipelined = batch && batch->stitchEnabled();
        for(unsigned i=0;i<maxStages;++i) buffers.stages[i].state=StageState::empty;
        for(unsigned i=0;i<maxStages;++i) q[i]=&buffers.stages[i];
        tick=PhaseClock::now();
    }

    int nextRead()
    {// 0: a read was processed; -1: no reads left in the chunk
        if (pipelined) return pipelinedRead();
        if (batch) return batchedRead();
        return cpuRead();
    }

private:
    ReadAlignChunk &c;
    ReadAlign *RA;
    Parameters &P;
    SeedBatchExecutor *batch;
    MapBuffers &buffers;
    std::vector<PreparedRead> &prepared;
    StitchBatch &stitchBatch;
    StitchResults &stitchResults;
    const unsigned nStages;
    bool pipelined=false;
    size_t preparedNext=0, preparedProjectedEnd=0;
    bool preparedEof=false;
    bool stitchActive=false;
    unsigned warmupReads=0;
    Stage* q[maxStages];
    unsigned stitchLaunches=0, seedLaunches=0;

    static MapBuffers& buffersOf(ReadAlignChunk &chunk)
    {
        if(!chunk.mapBuffers) chunk.mapBuffers=std::make_shared<MapBuffers>();
        return *static_cast<MapBuffers*>(chunk.mapBuffers.get());
    }

    Stage*& stageA() {return q[0];}

    void loadBatch(std::vector<PreparedRead>& reads)
    {// Reuse the previous batch's strings and vectors: savePreparedRead overwrites every field.
        size_t n=0, bytes=0;
        while(n<P.seedBatchSize && bytes<16*1024*1024) { // host memory per batch; reads also bound by seedBatchSize
            auto t0=PhaseClock::now();
            const int loaded=RA->loadRead();
            RA->alignPhaseSeconds[10]+=since(t0);
            if(loaded<0) { preparedEof=true; break; }
            if(n==reads.size()) reads.emplace_back();
            RA->savePreparedRead(reads[n]);
            RA->alignPhaseSeconds[11]+=since(t0);
            bytes+=reads[n].bytes();
            ++n;
        }
        reads.resize(n);
    }

    void collect(std::vector<PreparedRead>& reads, StitchBatch& sb)
    {// record every read's windows for device stitching
        sb.clear();
        RA->stitchCollect=&sb;
        for(auto& read:reads) {
            sb.reads.emplace_back();
            RA->restorePreparedRead(read);
            RA->alignLoadedRead();
        }
        RA->stitchCollect=nullptr;
    }

    void advancePipeline()
    {// rotate the ring until q[0] holds a stitched batch, or every stage is drained
        Stage*& a=stageA();
        a->state=StageState::empty;
        while(true) {
            Stage* oldest=q[2]; // the oldest batch still seeding, if any
            if(oldest->state==StageState::seeding) {//device seeds ready: collect windows, start device stitching
                oldest->seeds.get();
                c.phaseSeconds[3]+=since(tick);
                collect(oldest->reads,oldest->sb);
                c.phaseSeconds[4]+=since(tick);
                const unsigned slot=2*c.iThread+(stitchLaunches++&1u); // the two stitching batches use different slots
                SeedBatchExecutor* sbx=c.seedBatch;
                oldest->stitch=std::async(std::launch::async,[sbx,oldest,slot]{sbx->stitch(oldest->sb,oldest->sr,slot);});
                oldest->state=StageState::stitching;
            }
            Stage* freed=q[0];
            for(unsigned i=0;i+1<nStages;++i) q[i]=q[i+1];
            q[nStages-1]=freed;
            Stage* fresh=q[nStages-1];
            if(!preparedEof) {
                loadBatch(fresh->reads);
                c.phaseSeconds[1]+=since(tick);
                if(!fresh->reads.empty()) {
                    const unsigned slot=2*c.iThread+(seedLaunches++&1u); // the two seeding batches use different slots
                    SeedBatchExecutor* sbx=c.seedBatch;
                    fresh->seeds=std::async(std::launch::async,[sbx](std::vector<PreparedRead>* reads,unsigned s) {//device work only; runs off the worker thread
                        sbx->run(*reads,s);
                        size_t end=0;
                        for(size_t i=0;i<reads->size();++i)
                            if((*reads)[i].deviceConsolidated && i>=end) end=sbx->project(*reads,s,i);
                    },&fresh->reads,slot);
                    fresh->state=StageState::seeding;
                }
            }
            if(a->state==StageState::stitching) {
                a->stitch.get();
                c.phaseSeconds[5]+=since(tick); // wait for device stitching
                a->state=StageState::ready;
                break;
            }
            bool drained=preparedEof;
            for(unsigned i=1;i<nStages && drained;++i) drained=q[i]->state==StageState::empty;
            if(drained) break;
        }
        preparedNext=0;
    }

    int pipelinedRead()
    {
        if(stageA()->state!=StageState::ready || preparedNext==stageA()->reads.size()) {
            advancePipeline();
            if(stageA()->state!=StageState::ready) return -1;
        }
        Stage* a=stageA();
        RA->stitchInject=&a->sr; RA->stitchInjectRead=preparedNext;
        auto& read=a->reads[preparedNext++];
        auto t0=PhaseClock::now();
        RA->restorePreparedRead(read);
        RA->alignPhaseSeconds[12]+=since(t0);
        const int readStatus=RA->alignLoadedRead();
        RA->stitchInject=nullptr;
        read.projected=SeedHitBatch{};
        c.phaseSeconds[6]+=since(tick);
        return readStatus;
    }

    void loadAndSeedBatch()
    {// load the next batch, seed it on the device and, with device stitching, stitch all its reads
        prepared.clear(); preparedNext=0; preparedProjectedEnd=0;
        size_t bytes=0;
        while(prepared.size()<P.seedBatchSize && bytes<4*1024*1024) {
            if(RA->loadRead()<0) { preparedEof=true; break; }
            prepared.emplace_back();
            RA->savePreparedRead(prepared.back());
            bytes+=prepared.back().bytes();
        }
        c.phaseSeconds[1]+=since(tick);
        if(!prepared.empty()) c.seedBatch->run(prepared,2*c.iThread);
        c.phaseSeconds[2]+=since(tick);
        stitchActive=false;
        if(!prepared.empty() && c.seedBatch->stitchEnabled()) {//collect every read's windows, stitch them on the device
            stitchBatch.clear();
            RA->stitchCollect=&stitchBatch;
            for(size_t i=0;i<prepared.size();++i) {
                if(prepared[i].deviceConsolidated && i>=preparedProjectedEnd) {
                    c.phaseSeconds[4]+=since(tick);
                    preparedProjectedEnd=c.seedBatch->project(prepared,2*c.iThread,i);
                    c.phaseSeconds[3]+=since(tick);
                }
                stitchBatch.reads.emplace_back();
                RA->restorePreparedRead(prepared[i]);
                RA->alignLoadedRead();
            }
            RA->stitchCollect=nullptr;
            c.phaseSeconds[4]+=since(tick);
            c.seedBatch->stitch(stitchBatch,stitchResults,c.iThread);
            c.phaseSeconds[5]+=since(tick);
            stitchActive=true;
        }
    }

    int batchedRead()
    {
        if(preparedNext==prepared.size() && !preparedEof) loadAndSeedBatch();
        if(preparedNext==prepared.size()) return -1;
        if(prepared[preparedNext].deviceConsolidated && preparedNext>=preparedProjectedEnd) {
            preparedProjectedEnd=c.seedBatch->project(prepared,2*c.iThread,preparedNext);
            c.phaseSeconds[3]+=since(tick);
        }
        if(stitchActive) {RA->stitchInject=&stitchResults; RA->stitchInjectRead=preparedNext;}
        auto& read=prepared[preparedNext++];
        RA->restorePreparedRead(read);
        const int readStatus=RA->alignLoadedRead();
        RA->stitchInject=nullptr;
        read.projected=SeedHitBatch{}; // release each consumed hit payload before the next chunk
        c.phaseSeconds[6]+=since(tick);
        return readStatus;
    }

    int cpuRead()
    {
        const int readStatus=RA->oneRead(); c.phaseSeconds[6]+=since(tick);
        if(c.seedBatch && readStatus==0) ++c.cpuWarmupReads;
        // Device setup finished during this chunk: the remaining reads take the device path.
        if(c.seedBatch && (++warmupReads&1023u)==0 && c.seedBatch->deviceSettled()) {
            batch=c.seedBatch; pipelined=batch->stitchEnabled();
        }
        return readStatus;
    }
};
} // namespace

void ReadAlignChunk::mapChunk() {//map one chunk. Input reads stream has to be setup in RA->readInStream[ii]

    // While the device index is still uploading, map whole chunks with the original CPU path.
    SeedBatchExecutor* batch = (seedBatch && (P.seedSearchBackend=="cpu" || seedBatch->deviceSettled())) ? seedBatch : nullptr;
    if(seedBatch && !batch) ++cpuWarmupChunks;
    clipChunkMates(batch);
    RA->statsRA.resetN();

    for (uint64 ii=0;ii<P.readNends;ii++) {//update buffer size and rewind the input streams
        readInStream[ii]->setBuffer(chunkIn[ii], chunkInSizeBytesTotal[ii]);
    };

    if ( P.outSAMorder == "PairedKeepInputOrder" && P.runThreadN>1 ) {//open chunk file
        ostringstream name1("");
        name1 << P.outFileTmp + "/Aligned.tmp.sam.chunk"<<iChunkIn;
        chunkOutBAMfileName = name1.str();
        chunkOutBAMfile.open(chunkOutBAMfileName.c_str());
    };

    ChunkMapper mapper(*this, batch);
    int readStatus=0;
    while (readStatus==0) {//main cycle over all reads
        readStatus=mapper.nextRead();
        if (readStatus==0) {//there was a read processed
            RA->iRead++;
            chunkOutBAMtotal+=RA->outBAMbytes;
        };
        const bool lastRead = readStatus==-1 && noReadsLeft;
        if (P.outSAMbool) flushChunkSAM(lastRead);
        if (P.outSJ.yes) collapseChunkSJ(chunkOutSJ, "chunkOutSJ", lastRead);
        if (P.outFilterBySJoutStage == 1) collapseChunkSJ(chunkOutSJ1, "chunkOutSJ1", lastRead);
        phaseSeconds[7]+=since(mapper.tick);
    };

    finishChunk();
};

void ReadAlignChunk::clipChunkMates(SeedBatchExecutor* batch)
{// clip the whole chunk before mapping; on the device path the clip runs in batches on the device
    auto clipStart=PhaseClock::now();
    for (uint32 im=0; im<1; im++) {//hardcoded mate 1 5p only for now
        if (batch && P.seedSearchBackend!="cpu")
            RA->clipMates[im][0].clipChunk(chunkIn[im], chunkInSizeBytesTotal[im],
                [&](const ClipBatch& b) {return batch->clip(b,iThread);});
        else
            RA->clipMates[im][0].clipChunk(chunkIn[im], chunkInSizeBytesTotal[im]);
    };
    phaseSeconds[8]+=since(clipStart);
}

void ReadAlignChunk::flushChunkSAM(bool lastRead)
{// write the SAM/BAM chunk buffer when it is almost full, or after the last read
    if ( chunkOutBAMtotal > P.chunkOutBAMsizeBytes ) {//this should not happen!
        ostringstream errOut;
        errOut <<"EXITING because of fatal error: buffer size for SAM/BAM output is too small\n";
        errOut <<"Solution: increase input parameter --limitOutSAMoneReadBytes\n";
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    };
    if ( chunkOutBAMtotal + P.limitOutSAMoneReadBytes <= P.chunkOutBAMsizeBytes && !lastRead ) return;

    if ( P.outSAMorder == "PairedKeepInputOrder" && P.runThreadN>1 ) {//output chunks into separate files
        chunkOutBAMfile.write(chunkOutBAM,chunkOutBAMtotal);
        chunkOutBAMfile.clear(); //in case 0 bytes were written which could set fail bit
    } else if (P.inOut->samWriter) {//standard way, directly into Aligned.out.sam file
        P.inOut->samWriter->submit(chunkOutBAM,chunkOutBAMtotal);
    } else {
        if (P.runThreadN>1) pthread_mutex_lock(&g_threadChunks.mutexOutSAM);
        P.inOut->outSAM->write(chunkOutBAM,chunkOutBAMtotal);
        P.inOut->outSAM->clear();//in case 0 bytes were written which could set fail bit
        if (P.runThreadN>1) pthread_mutex_unlock(&g_threadChunks.mutexOutSAM);
    };
    RA->outSAMstream->seekp(0,ios::beg); //rewind the chunk storage
    chunkOutBAMtotal=0;
}

void ReadAlignChunk::collapseChunkSJ(OutSJ *sj, const char *name, bool lastRead)
{// collapse a junction buffer when it is almost full, or after the last read; grow it if collapsing is not enough
    if ( sj->N > sj->Nstore ) {//this means the number of collapsed junctions is larger than the chunks size
        ostringstream errOut;
        errOut <<"EXITING because of fatal error: buffer size for SJ output is too small\n";
        errOut <<"Solution: increase input parameter --limitOutSJoneRead\n";
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    };
    if ( sj->N + P.limitOutSJoneRead <= sj->Nstore && !lastRead ) return;

    sj->collapseSJ();
    if ( sj->N + 2*P.limitOutSJoneRead > sj->Nstore ) {
        sj->dataSizeIncrease();//grow this store itself; growing chunkOutSJ for chunkOutSJ1 let the latter overrun its array
        OptionalMutexLock logLock(g_threadChunks.mutexLogMain, P.runThreadN>1);
        P.inOut->logMain << "Increased the size of " << name << " to " << sj->Nstore <<'\n';
    };
}

void ReadAlignChunk::finishChunk()
{// close the per-chunk SAM file, add the chunk's statistics and report progress
    if ( P.outSAMbool && P.outSAMorder == "PairedKeepInputOrder" && P.runThreadN>1 ) {//write the remaining part of the buffer, close and rename chunk files
        chunkOutBAMfile.write(chunkOutBAM,chunkOutBAMtotal);
        chunkOutBAMfile.clear(); //in case 0 bytes were written which could set fail bit
        chunkOutBAMfile.close();
        RA->outSAMstream->seekp(0,ios::beg); //rewind the chunk storage
        chunkOutBAMtotal=0;
        ostringstream name2("");
        name2 << P.outFileTmp + "/Aligned.out.sam.chunk"<<iChunkIn;
        rename(chunkOutBAMfileName.c_str(),name2.str().c_str());//marks files as completedly written
    };

    if (P.runThreadN>1) pthread_mutex_lock(&g_threadChunks.mutexStats);
    g_statsAll.addStats(RA->statsRA);
    g_statsAll.progressReport(P.inOut->logProgress);
    if (P.runThreadN>1) pthread_mutex_unlock(&g_threadChunks.mutexStats);
}
