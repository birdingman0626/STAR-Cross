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
}
void ReadAlignChunk::mapChunk() {//map one chunk. Input reads stream has to be setup in RA->readInStream[ii]
    
    // While the device index is still uploading, map whole chunks with the original CPU path.
    SeedBatchExecutor* batch = (seedBatch && (P.seedSearchBackend=="cpu" || seedBatch->deviceSettled())) ? seedBatch : nullptr;
    if(seedBatch && !batch) ++cpuWarmupChunks;
    auto clipStart=PhaseClock::now();
    for (uint32 im=0; im<1; im++) {//hardcoded mate 1 5p onyl for now
        if (batch && P.seedSearchBackend!="cpu")
            RA->clipMates[im][0].clipChunk(chunkIn[im], chunkInSizeBytesTotal[im],
                [&](const ClipBatch& b) {return batch->clip(b,iThread);});
        else
            RA->clipMates[im][0].clipChunk(chunkIn[im], chunkInSizeBytesTotal[im]);
    };
    
    phaseSeconds[8]+=since(clipStart);
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

    int readStatus=0;
    if(!mapBuffers) mapBuffers=std::make_shared<MapBuffers>();
    MapBuffers& buffers=*static_cast<MapBuffers*>(mapBuffers.get());
    std::vector<PreparedRead>& prepared=buffers.prepared;
    prepared.clear();
    size_t preparedNext=0,preparedProjectedEnd=0;
    bool preparedEof=false;
    StitchBatch& stitchBatch=buffers.stitchBatch;
    StitchResults& stitchResults=buffers.stitchResults;
    bool stitchActive=false;
    // Pipelined device path (GPU stitching on). A ring of K batches per worker, in input order:
    // q[0] is aligned on the CPU, q[1] is stitched on the device, q[2..K-1] are seeded and
    // projected on the device. Every read still runs the same CPU steps in input order.
    // At most two batches seed and two stitch at once, so each has two device slots per worker.
    bool pipelined = batch && batch->stitchEnabled();
    unsigned warmupReads=0;
    static const unsigned nStages=[]{
        const char* v=std::getenv("STAR_PIPELINE_STAGES");
        return v && std::string(v)=="3" ? 3u : maxStages;
    }();
    Stage* stages=buffers.stages;
    for(unsigned i=0;i<maxStages;++i) stages[i].state=StageState::empty;
    Stage* q[maxStages];
    for(unsigned i=0;i<maxStages;++i) q[i]=&stages[i];
    Stage*& stageA=q[0];
    unsigned stitchLaunches=0, seedLaunches=0;
    auto loadBatch=[&](std::vector<PreparedRead>& batch) {
        // Reuse the previous batch's strings and vectors: savePreparedRead overwrites every field.
        size_t n=0, bytes=0;
        while(n<P.seedBatchSize && bytes<16*1024*1024) { // host memory per batch; reads also bound by seedBatchSize
            auto t0=PhaseClock::now();
            const int loaded=RA->loadRead();
            RA->alignPhaseSeconds[10]+=since(t0);
            if(loaded<0) { preparedEof=true; break; }
            if(n==batch.size()) batch.emplace_back();
            RA->savePreparedRead(batch[n]);
            RA->alignPhaseSeconds[11]+=since(t0);
            bytes+=batch[n].bytes();
            ++n;
        }
        batch.resize(n);
    };
    auto seedAndProject=[this](std::vector<PreparedRead>* batch,unsigned slot) {//device work only; runs off the worker thread
        seedBatch->run(*batch,slot);
        size_t end=0;
        for(size_t i=0;i<batch->size();++i)
            if((*batch)[i].deviceConsolidated && i>=end) end=seedBatch->project(*batch,slot,i);
    };
    auto collect=[&](std::vector<PreparedRead>& batch, StitchBatch& sb) {
        sb.clear();
        RA->stitchCollect=&sb;
        for(auto& read:batch) {
            sb.reads.emplace_back();
            RA->restorePreparedRead(read);
            RA->alignLoadedRead();
        }
        RA->stitchCollect=nullptr;
    };
    auto tick=PhaseClock::now();
    while (readStatus==0) {//main cycle over all reads

        if(pipelined) {
            if(stageA->state!=StageState::ready || preparedNext==stageA->reads.size()) {
                stageA->state=StageState::empty;
                while(true) {
                    Stage* oldest=q[2]; // the oldest batch still seeding, if any
                    if(oldest->state==StageState::seeding) {//device seeds ready: collect windows, start device stitching
                        oldest->seeds.get();
                        phaseSeconds[3]+=since(tick);
                        collect(oldest->reads,oldest->sb);
                        phaseSeconds[4]+=since(tick);
                        const unsigned slot=2*iThread+(stitchLaunches++&1u); // the two stitching batches use different slots
                        oldest->stitch=std::async(std::launch::async,[this,oldest,slot]{seedBatch->stitch(oldest->sb,oldest->sr,slot);});
                        oldest->state=StageState::stitching;
                    }
                    Stage* freed=q[0];
                    for(unsigned i=0;i+1<nStages;++i) q[i]=q[i+1];
                    q[nStages-1]=freed;
                    Stage* fresh=q[nStages-1];
                    if(!preparedEof) {
                        loadBatch(fresh->reads);
                        phaseSeconds[1]+=since(tick);
                        if(!fresh->reads.empty()) {
                            const unsigned slot=2*iThread+(seedLaunches++&1u); // the two seeding batches use different slots
                            fresh->seeds=std::async(std::launch::async,seedAndProject,&fresh->reads,slot);
                            fresh->state=StageState::seeding;
                        }
                    }
                    if(stageA->state==StageState::stitching) {
                        stageA->stitch.get();
                        phaseSeconds[5]+=since(tick); // wait for device stitching
                        stageA->state=StageState::ready;
                        break;
                    }
                    bool drained=preparedEof;
                    for(unsigned i=1;i<nStages && drained;++i) drained=q[i]->state==StageState::empty;
                    if(drained) break;
                }
                preparedNext=0;
                if(stageA->state!=StageState::ready) readStatus=-1;
            }
            if(readStatus==0) {
                RA->stitchInject=&stageA->sr; RA->stitchInjectRead=preparedNext;
                auto& read=stageA->reads[preparedNext++];
                auto t0=PhaseClock::now();
                RA->restorePreparedRead(read);
                RA->alignPhaseSeconds[12]+=since(t0);
                readStatus=RA->alignLoadedRead();
                RA->stitchInject=nullptr;
                read.projected=SeedHitBatch{};
                phaseSeconds[6]+=since(tick);
            }
        } else if(batch) {
            if(preparedNext==prepared.size() && !preparedEof) {
                prepared.clear(); preparedNext=0; preparedProjectedEnd=0;
                size_t bytes=0;
                while(prepared.size()<P.seedBatchSize && bytes<4*1024*1024) {
                    if(RA->loadRead()<0) { preparedEof=true; break; }
                    prepared.emplace_back();
                    RA->savePreparedRead(prepared.back());
                    bytes+=prepared.back().bytes();
                }
                phaseSeconds[1]+=since(tick);
                if(!prepared.empty()) seedBatch->run(prepared,2*iThread);
                phaseSeconds[2]+=since(tick);
                stitchActive=false;
                if(!prepared.empty() && seedBatch->stitchEnabled()) {//collect every read's windows, stitch them on the device
                    stitchBatch.clear();
                    RA->stitchCollect=&stitchBatch;
                    for(size_t i=0;i<prepared.size();++i) {
                        if(prepared[i].deviceConsolidated && i>=preparedProjectedEnd) {
                            phaseSeconds[4]+=since(tick);
                            preparedProjectedEnd=seedBatch->project(prepared,2*iThread,i);
                            phaseSeconds[3]+=since(tick);
                        }
                        stitchBatch.reads.emplace_back();
                        RA->restorePreparedRead(prepared[i]);
                        RA->alignLoadedRead();
                    }
                    RA->stitchCollect=nullptr;
                    phaseSeconds[4]+=since(tick);
                    seedBatch->stitch(stitchBatch,stitchResults,iThread);
                    phaseSeconds[5]+=since(tick);
                    stitchActive=true;
                }
            }
            if(preparedNext==prepared.size()) readStatus=-1;
            else {
                if(prepared[preparedNext].deviceConsolidated && preparedNext>=preparedProjectedEnd) {
                    preparedProjectedEnd=seedBatch->project(prepared,2*iThread,preparedNext);
                    phaseSeconds[3]+=since(tick);
                }
                if(stitchActive) {RA->stitchInject=&stitchResults; RA->stitchInjectRead=preparedNext;}
                auto& read=prepared[preparedNext++];
                RA->restorePreparedRead(read);
                readStatus=RA->alignLoadedRead();
                RA->stitchInject=nullptr;
                read.projected=SeedHitBatch{}; // release each consumed hit payload before the next chunk
                phaseSeconds[6]+=since(tick);
            }
        } else {//original synchronous mapper
            readStatus=RA->oneRead(); phaseSeconds[6]+=since(tick);
            if(seedBatch && !batch && readStatus==0) ++cpuWarmupReads;
            // Device setup finished during this chunk: the remaining reads take the device path.
            if(seedBatch && !batch && (++warmupReads&1023u)==0 && seedBatch->deviceSettled()) {
                batch=seedBatch; pipelined=batch->stitchEnabled();
            }
        }

        if (readStatus==0) {//there was a read processed
            RA->iRead++;
//         chunkOutBAMtotal=(uint64) RA->outSAMstream->tellp();
            chunkOutBAMtotal+=RA->outBAMbytes;
//             uint64 ddd=(uint64) RA->outSAMstream->tellp();
        };

        //write SAM aligns to chunk buffer
        if (P.outSAMbool) {
            if ( chunkOutBAMtotal > P.chunkOutBAMsizeBytes ) {//this should not happen!
                ostringstream errOut;
                errOut <<"EXITING because of fatal error: buffer size for SAM/BAM output is too small\n";
                errOut <<"Solution: increase input parameter --limitOutSAMoneReadBytes\n";
                exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
            } else if ( chunkOutBAMtotal + P.limitOutSAMoneReadBytes > P.chunkOutBAMsizeBytes || (readStatus==-1 && noReadsLeft) ) {//write buffer to disk because it's almost full, or all reads are mapped
                if ( P.outSAMorder == "PairedKeepInputOrder" && P.runThreadN>1 ) {//output chunks into separate files
                    chunkOutBAMfile.write(chunkOutBAM,chunkOutBAMtotal);
                    chunkOutBAMfile.clear(); //in case 0 bytes were written which could set fail bit
                    //chunkOutBAMfile.flush(); //not needed
                } else {//standard way, directly into Aligned.out.sam file
                    if (P.inOut->samWriter) {
                        P.inOut->samWriter->submit(chunkOutBAM,chunkOutBAMtotal);
                    } else {
                    //SAM output
                    if (P.runThreadN>1) pthread_mutex_lock(&g_threadChunks.mutexOutSAM);
                    P.inOut->outSAM->write(chunkOutBAM,chunkOutBAMtotal);
                    P.inOut->outSAM->clear();//in case 0 bytes were written which could set fail bit
                    //P.inOut->outSAM->flush(); //not needed
                    if (P.runThreadN>1) pthread_mutex_unlock(&g_threadChunks.mutexOutSAM);
                    }
                };
                RA->outSAMstream->seekp(0,ios::beg); //rewind the chunk storage
                chunkOutBAMtotal=0;
            };
        };

        //collapse SJ buffer if needed
        if ( !P.outSJ.yes ) {
            //do nothing
        } else if ( chunkOutSJ->N > chunkOutSJ->Nstore ) {//this means the number of collapsed junctions is larger than the chunks size
            ostringstream errOut;
            errOut <<"EXITING because of fatal error: buffer size for SJ output is too small\n";
            errOut <<"Solution: increase input parameter --limitOutSJoneRead\n";
            exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
        } else if ( chunkOutSJ->N + P.limitOutSJoneRead > chunkOutSJ->Nstore || (readStatus==-1 && noReadsLeft) ) {//write buffer to disk because it's almost full, or all reads are mapped
            chunkOutSJ->collapseSJ();
            if ( chunkOutSJ->N + 2*P.limitOutSJoneRead > chunkOutSJ->Nstore ) {
                /*
                ostringstream errOut;
                errOut <<"EXITING because of fatal error: buffer size for SJ output is too small\n";
                errOut <<"Solution: increase input parameter --limitOutSJcollapsed\n";
                exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
                */
                chunkOutSJ->dataSizeIncrease();
                OptionalMutexLock logLock(g_threadChunks.mutexLogMain, P.runThreadN>1);
                P.inOut->logMain << "Increased the size of chunkOutSJ to " << chunkOutSJ->Nstore <<'\n';
            };
        };

        //collapse SJ1 buffer if needed
        if ( P.outFilterBySJoutStage != 1 ) {//no outFilterBySJoutStage
            //do nothing
        } else if ( chunkOutSJ1->N > chunkOutSJ1->Nstore ) {//this means the number of collapsed junctions is larger than the chunks size
            ostringstream errOut;
            errOut <<"EXITING because of fatal error: buffer size for SJ output is too small\n";
            errOut <<"Solution: increase input parameter --limitOutSJoneRead\n";
            exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
        } else if ( chunkOutSJ1->N + P.limitOutSJoneRead > chunkOutSJ1->Nstore || (readStatus==-1 && noReadsLeft) ) {//write buffer to disk because it's almost full, or all reads are mapped
            chunkOutSJ1->collapseSJ();
            if ( chunkOutSJ1->N + 2*P.limitOutSJoneRead > chunkOutSJ1->Nstore ) {
                /*
                ostringstream errOut;
                errOut <<"EXITING because of fatal error: buffer size for SJ output is too small\n";
                errOut <<"Solution: increase input parameter --limitOutSJcollapsed\n";
                exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
                */
                chunkOutSJ1->dataSizeIncrease();//grow the stage-1 store itself; growing chunkOutSJ let chunkOutSJ1 overrun its array
                OptionalMutexLock logLock(g_threadChunks.mutexLogMain, P.runThreadN>1);
                P.inOut->logMain << "Increased the size of chunkOutSJ1 to " << chunkOutSJ1->Nstore <<'\n';
            };
        };

        phaseSeconds[7]+=since(tick);
    }; //reads cycle

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

    //add stats, write progress if needed
    if (P.runThreadN>1) pthread_mutex_lock(&g_threadChunks.mutexStats);
    g_statsAll.addStats(RA->statsRA);
    g_statsAll.progressReport(P.inOut->logProgress);
    if (P.runThreadN>1) pthread_mutex_unlock(&g_threadChunks.mutexStats);
};
