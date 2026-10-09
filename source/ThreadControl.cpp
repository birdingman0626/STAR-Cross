#include "ThreadControl.h"
#include "GlobalVariables.h"

void* ThreadControl::threadRAprocessChunks(void* chunk) {
    try {static_cast<ReadAlignChunk*>(chunk)->processChunks();} catch (...) {
        std::lock_guard<std::mutex> lock(g_threadChunks.failureMutex);
        if (!g_threadChunks.mappingFailure) g_threadChunks.mappingFailure=std::current_exception();
    }
    return nullptr;
}

ThreadControl::ThreadControl() {
    chunkInN=0;
    chunkOutN=0;
//     chunkOutBAMposition=new uint [MAX_chunkOutBAMposition];
};
