#ifndef INOUTSTREAMS_DEF
#define INOUTSTREAMS_DEF

#include "IncludeDefine.h"
#include SAMTOOLS_BGZF_H
#include <memory>
class AsyncByteWriter;
class ChunkInputDispatcher;

class InOutStreams {
    public:
    std::ostream *logStdOut, *outSAM;
    std::ofstream logStdOutFile, outSAMfile;
    BGZF *outBAMfileUnsorted, *outQuantBAMfile;
    std::unique_ptr<AsyncByteWriter> unsortedWriter,quantWriter,samWriter;
    std::unique_ptr<ChunkInputDispatcher> chunkInputDispatcher;

    std::ofstream outChimSAM, outChimJunction, logMain, logProgress, logFinal, outUnmappedReadsStream[MAX_N_MATES];
    std::ifstream readIn[MAX_N_MATES];

    //compilation-optional streams
    std::ofstream outLocalChains;

    InOutStreams();
    ~InOutStreams();
};

#endif
