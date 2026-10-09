#ifndef THREAD_CONTROL_DEF
#define THREAD_CONTROL_DEF

#include "ReadAlignChunk.h"
#include <mutex>
#include <exception>
#ifdef _WIN32
    #include "wincompat.h"
#else
    #include <pthread.h>
#endif

#define MAX_chunkOutBAMposition 100000

class ThreadControl {
public:
    bool threadBool;

    pthread_t *threadArray;
    pthread_mutex_t mutexInRead, mutexOutSAM, mutexOutBAM1, mutexOutChimSAM, mutexOutChimJunction, mutexOutUnmappedFastx, mutexOutFilterBySJout;
    pthread_mutex_t mutexStats, mutexLogMain, mutexBAMsortBins, mutexError;

    uint chunkInN,chunkOutN;
    std::mutex failureMutex;
    std::exception_ptr mappingFailure;

    ThreadControl();

    static void* threadRAprocessChunks(void *RAchunk);
};

#endif

