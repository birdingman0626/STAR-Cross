#include <iomanip>
#include <chrono>
#include "ReadAlignChunk.h"
#include "ChunkInputDispatcher.h"
#include "ThreadControl.h"
#include "ErrorWarning.h"
#include "SequenceFuns.h"
#include "GlobalVariables.h"
#include "SeedDiagnostics.h"
#include <chrono>
using namespace std;
void ReadAlignChunk::processChunks() {//read-map-write chunks
    #ifdef STAR_CAPTURE_SEEDS
    if(std::getenv("STAR_SEED_TRACE_DIR") && (P.twoPass.yes || P.outFilterBySJoutStage!=0 || P.wasp.yes || P.peOverlap.yes))
        throw std::runtime_error("diagnostic seed capture requires a fixed one-pass index without remapping or mate-overlap mapping");
    seedTrace::Lifetime diagnostic(iThread);
    #endif
    const double threadBegin=std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    noReadsLeft=false; //true if there no more reads left in the file
    newFile=false;
    headerExtra.resize(P.readNends);
    while (!noReadsLeft) {//continue until the input EOF
            //////////////read a chunk from input files and store in memory
        const auto readStart=std::chrono::steady_clock::now();
        if (P.inOut->chunkInputDispatcher) {
            #ifdef STAR_CAPTURE_SEEDS
            auto* worker=seedTrace::worker();
            #endif
            P.inOut->chunkInputDispatcher->invoke([&]{
                #ifdef STAR_CAPTURE_SEEDS
                seedTrace::BorrowedScope trace(worker);
                #endif
                readChunk();
            });
        } else readChunk();
        phaseSeconds[0]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-readStart).count();

        mapChunk();

        if (iThread==0 && P.runThreadN>1 && P.outSAMorder=="PairedKeepInputOrder") {//concatenate Aligned.* files
            chunkFilesCat(P.inOut->outSAM, P.outFileTmp + "/Aligned.out.sam.chunk", g_threadChunks.chunkOutN);
        };

    };//cycle over input chunks

    if (P.outFilterBySJoutStage!=1 && RA->iRead>0) {//not the first stage of the 2-stage mapping
        if (P.outBAMunsorted) chunkOutBAMunsorted->unsortedFlush();
        if (P.outBAMcoord) chunkOutBAMcoord->coordFlush();
        if (chunkOutBAMquant!=NULL) chunkOutBAMquant->unsortedFlush();

        //the thread is finished mapping reads, concatenate the temp files into output files
        if (P.pCh.segmentMin>0) {
            chunkFstreamCat (RA->chunkOutChimSAM, P.inOut->outChimSAM, P.runThreadN>1, g_threadChunks.mutexOutChimSAM);
            chunkFstreamCat (*RA->chunkOutChimJunction, P.inOut->outChimJunction, P.runThreadN>1, g_threadChunks.mutexOutChimJunction);
        };
        if (P.outReadsUnmapped=="Fastx" ) {
            if (P.runThreadN>1)
                pthread_mutex_lock(&g_threadChunks.mutexOutUnmappedFastx);

            for (uint64 ii=0;ii<P.readNends;ii++) {
                chunkFstreamCat (RA->chunkOutUnmappedReadsStream[ii],P.inOut->outUnmappedReadsStream[ii], false, g_threadChunks.mutexOutUnmappedFastx);
            };

            if (P.runThreadN>1)
                pthread_mutex_unlock(&g_threadChunks.mutexOutUnmappedFastx);
        };
    };
    if (P.runThreadN>1) pthread_mutex_lock(&g_threadChunks.mutexLogMain);
    P.inOut->logMain << "Completed: thread #" <<iThread <<" PHASE_SECONDS read="<<phaseSeconds[0]<<" load="<<phaseSeconds[1]
        <<" seed="<<phaseSeconds[2]<<" project="<<phaseSeconds[3]<<" collect="<<phaseSeconds[4]<<" stitch="<<phaseSeconds[5]
        <<" align="<<phaseSeconds[6]<<" write="<<phaseSeconds[7]<<" clip="<<phaseSeconds[8]<<" input_held="<<inputLockHeldSeconds<<std::fixed<<std::setprecision(3)<<" begin="<<threadBegin<<" end="<<std::fixed<<std::setprecision(3)<<std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()<<std::defaultfloat<<" cpu_warmup_chunks="<<cpuWarmupChunks
        <<" ALIGN_SECONDS map="<<RA->alignPhaseSeconds[0]<<" select="<<RA->alignPhaseSeconds[1]<<" chimeric="<<RA->alignPhaseSeconds[2]
        <<" output="<<RA->alignPhaseSeconds[3]<<" collect_map="<<RA->alignPhaseSeconds[4]<<" annot="<<RA->alignPhaseSeconds[5]<<" cbumi="<<RA->alignPhaseSeconds[6]<<" solo_record="<<RA->alignPhaseSeconds[7]<<" other_output="<<RA->alignPhaseSeconds[8]<<" stitch_pieces="<<RA->alignPhaseSeconds[9]<<" load_read="<<RA->alignPhaseSeconds[10]<<" load_save="<<RA->alignPhaseSeconds[11]<<" restore="<<RA->alignPhaseSeconds[12]<<endl;
    if (P.runThreadN>1) pthread_mutex_unlock(&g_threadChunks.mutexLogMain);
};
