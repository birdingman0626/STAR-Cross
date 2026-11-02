#include <iomanip>
#include <chrono>
#include "mapThreadsSpawn.h"
#include "AsyncByteWriter.h"
#include "ChunkInputDispatcher.h"
#include "ChunkInputPrefetcher.h"
#include "ReadAlignChunk.h"
#include "ThreadControl.h"
#include "GlobalVariables.h"
#include "ErrorWarning.h"
#include "bamEndian.h"
#include "SeedBatch.h"
#include <cstdlib>
#include <stdexcept>
using namespace std;
void mapThreadsSpawn (Parameters &P, ReadAlignChunk** RAchunk) {
    std::unique_ptr<SeedBatchExecutor> seeds;
    if(P.seedSearchBackend!="off") seeds=std::make_unique<SeedBatchExecutor>(P,RAchunk[0]->mapGen);
    for(int i=0;i<P.runThreadN;++i) RAchunk[i]->seedBatch=seeds.get();
    P.inOut->logMain<<"TIMING executor_built t="<<std::fixed<<std::setprecision(3)<<std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()<<std::defaultfloat<<"\n";
    if (const char* enabled=std::getenv("STAR_CHUNK_PIPELINE")) {
        if (std::string(enabled)!="1") exitWithError("STAR_CHUNK_PIPELINE must be 1 or unset",std::cerr,P.inOut->logMain,EXIT_CODE_PARAMETER,P);
        if (P.runThreadN>1 && P.outMultimapperOrder.random)
            exitWithError("chunk pipeline with random multimapper ordering requires one logical worker to preserve RNG assignment",std::cerr,P.inOut->logMain,EXIT_CODE_PARAMETER,P);
        P.inOut->chunkInputDispatcher.reset(new ChunkInputDispatcher(P.runThreadN));
    }
    g_threadChunks.mappingFailure=nullptr;
    // Ordered SAM chunks are numbered per mapping pass. Without this reset the
    // two-pass main pass named its chunks after the first pass's count, the
    // concatenator waited for chunk 0, and Aligned.out.sam lost every record.
    g_threadChunks.chunkInN=0;
    g_threadChunks.chunkOutN=0;
    // STAR_INPUT_PREFETCH=<depth>: a producer thread reads up to depth chunks ahead of the workers.
    if (const char* prefetch=std::getenv("STAR_INPUT_PREFETCH"); prefetch && std::string(prefetch)!="0"
        && P.runThreadN>1 && P.outFilterBySJoutStage<2 && !P.inOut->chunkInputDispatcher) {
        const int depth=std::atoi(prefetch);
        if (depth<1 || depth>64) exitWithError("STAR_INPUT_PREFETCH must be a chunk count in [1,64] or 0",std::cerr,P.inOut->logMain,EXIT_CODE_PARAMETER,P);
        auto headerExtra=std::make_shared<std::vector<std::string>>(P.readNends);
        auto newFile=std::make_shared<bool>(false);
        P.inOut->chunkInputPrefetcher.reset(new ChunkInputPrefetcher(size_t(depth),P.readNends,P.chunkInSizeBytesArray,
            [&P,headerExtra,newFile](ChunkInputPrefetcher::Chunk& c) {
                char* in[MAX_N_MATES];
                for (uint64 i=0;i<P.readNends;++i) in[i]=c.storage[i].get();
                pthread_mutex_lock(&g_threadChunks.mutexInRead);
                try {
                    fillInputChunk(P,in,c.sizes,*headerExtra,*newFile,-1,c.noReadsLeft,c.iChunkIn);
                } catch(...) {pthread_mutex_unlock(&g_threadChunks.mutexInRead); throw;}
                pthread_mutex_unlock(&g_threadChunks.mutexInRead);
            },
            [] {
                pthread_mutex_lock(&g_threadChunks.mutexInRead);
                const uint64 i=g_threadChunks.chunkInN++;
                pthread_mutex_unlock(&g_threadChunks.mutexInRead);
                return i;
            }));
        P.inOut->logMain<<"INPUT_PREFETCH depth="<<depth<<"\n";
    }
    if (const char* enabled=std::getenv("STAR_ASYNC_SAM")) {
        if (std::string(enabled)!="1") exitWithError("STAR_ASYNC_SAM must be 1 or unset",std::cerr,P.inOut->logMain,EXIT_CODE_PARAMETER,P);
        if (P.outSAMbool && !P.inOut->samWriter) {
            auto* stream=P.inOut->outSAM;
            P.inOut->samWriter.reset(new AsyncByteWriter(P.chunkOutBAMsizeBytes,[stream](char* data,size_t size) {
                stream->write(data,size);
                if (!*stream) throw std::runtime_error("asynchronous SAM batch write failed");
            }));
        }
    }
    if (const char* enabled=std::getenv("STAR_ASYNC_BAM")) {
        if (std::string(enabled)!="1") exitWithError("STAR_ASYNC_BAM must be 1 or unset",std::cerr,P.inOut->logMain,EXIT_CODE_PARAMETER,P);
        auto start=[&P](BGZF* stream,std::unique_ptr<AsyncByteWriter>& writer) {
            if (!stream || writer) return;
            writer.reset(new AsyncByteWriter(P.chunkOutBAMsizeBytes,[stream](char* bytes,size_t size) {
                const auto written=bamWriteNativeRecords(stream,bytes,size);
                if (written<0 || static_cast<size_t>(written)!=size)
                    throw std::runtime_error("asynchronous BAM batch write failed");
            }));
        };
        start(P.inOut->outBAMfileUnsorted,P.inOut->unsortedWriter);
        start(P.inOut->outQuantBAMfile,P.inOut->quantWriter);
        // Internal first-pass and matrix/SAM-only jobs have no BAM transport.
        // They retain their existing output path; do not reject valid two-pass jobs.
    }
    for (int ithread=1;ithread<P.runThreadN;ithread++) {//spawn threads
        int threadStatus=pthread_create(&g_threadChunks.threadArray[ithread], NULL, &g_threadChunks.threadRAprocessChunks, (void *) RAchunk[ithread]);
        if (threadStatus>0) {//something went wrong with one of threads
                ostringstream errOut;
                errOut << "EXITING because of FATAL ERROR: phtread error while creating thread # " << ithread <<", error code: "<<threadStatus ;
                exitWithError(errOut.str(),std::cerr, P.inOut->logMain, 1, P);
        };
        pthread_mutex_lock(&g_threadChunks.mutexLogMain);
        P.inOut->logMain << "Created thread # " <<ithread <<"\n"<<flush;
        pthread_mutex_unlock(&g_threadChunks.mutexLogMain);
    };

    try {RAchunk[0]->processChunks();} catch (...) {
        std::lock_guard<std::mutex> lock(g_threadChunks.failureMutex);
        if (!g_threadChunks.mappingFailure) g_threadChunks.mappingFailure=std::current_exception();
    }

    for (int ithread=1;ithread<P.runThreadN;ithread++) {//wait for all threads to complete
        int threadStatus = pthread_join(g_threadChunks.threadArray[ithread], NULL);
        if (threadStatus>0) {//something went wrong with one of threads
                ostringstream errOut;
                errOut << "EXITING because of FATAL ERROR: phtread error while joining thread # " << ithread <<", error code: "<<threadStatus ;
                exitWithError(errOut.str(),std::cerr, P.inOut->logMain, 1, P);
        };
        pthread_mutex_lock(&g_threadChunks.mutexLogMain);
        P.inOut->logMain << "Joined thread # " <<ithread <<"\n"<<flush;
        pthread_mutex_unlock(&g_threadChunks.mutexLogMain);
    };
    if (P.inOut->chunkInputDispatcher) {
        P.inOut->chunkInputDispatcher->finish();
        P.inOut->chunkInputDispatcher.reset();
    }
    if (P.inOut->chunkInputPrefetcher) {
        P.inOut->chunkInputPrefetcher->finish();
        P.inOut->chunkInputPrefetcher.reset();
    }
    for(int i=0;i<P.runThreadN;++i) RAchunk[i]->seedBatch=nullptr;
    if (g_threadChunks.mappingFailure) std::rethrow_exception(g_threadChunks.mappingFailure);
    P.inOut->logMain<<"TIMING threads_joined t="<<std::fixed<<std::setprecision(3)<<std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()<<std::defaultfloat<<"\n";
    if(seeds) seeds->report();
    if(seeds) for(int i=0;i<P.runThreadN;++i) if(RAchunk[i]->cpuWarmupReads)
        P.inOut->logMain<<"SEED_BATCH_WARMUP worker="<<i<<" reads="<<RAchunk[i]->cpuWarmupReads<<'\n';
    if(seeds && seeds->stitchEnabled()) for(int i=0;i<P.runThreadN;++i)
        P.inOut->logMain<<"GPU_STITCH_INJECT worker="<<i<<" injected_reads="<<RAchunk[i]->RA->stitchInjectedReads<<" cpu_stitched_reads="<<RAchunk[i]->RA->stitchCpuReads<<'\n';
};

