#include "mapThreadsSpawn.h"
#include "AsyncByteWriter.h"
#include "ChunkInputDispatcher.h"
#include "ThreadControl.h"
#include "GlobalVariables.h"
#include "ErrorWarning.h"
#include "bamEndian.h"
#include <cstdlib>
#include <stdexcept>

void mapThreadsSpawn (Parameters &P, ReadAlignChunk** RAchunk) {
    if (const char* enabled=std::getenv("STAR_CHUNK_PIPELINE")) {
        if (std::string(enabled)!="1") exitWithError("STAR_CHUNK_PIPELINE must be 1 or unset",std::cerr,P.inOut->logMain,EXIT_CODE_PARAMETER,P);
        if (P.runThreadN>1 && P.outMultimapperOrder.random)
            exitWithError("chunk pipeline with random multimapper ordering requires one logical worker to preserve RNG assignment",std::cerr,P.inOut->logMain,EXIT_CODE_PARAMETER,P);
        P.inOut->chunkInputDispatcher.reset(new ChunkInputDispatcher(P.runThreadN));
    }
    g_threadChunks.mappingFailure=nullptr;
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
    if (g_threadChunks.mappingFailure) std::rethrow_exception(g_threadChunks.mappingFailure);
};

