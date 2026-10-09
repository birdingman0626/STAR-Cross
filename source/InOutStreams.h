#ifndef INOUTSTREAMS_DEF
#define INOUTSTREAMS_DEF

#include "IncludeDefine.h"
#include SAMTOOLS_BGZF_H
#include <memory>
class AsyncByteWriter;
class ChunkInputDispatcher;

class InOutStreams {
    public:
    ostream *logStdOut, *outSAM;
    ofstream logStdOutFile, outSAMfile;
    BGZF *outBAMfileUnsorted, *outQuantBAMfile;
    std::unique_ptr<AsyncByteWriter> unsortedWriter,quantWriter,samWriter;
    std::unique_ptr<ChunkInputDispatcher> chunkInputDispatcher;

    ofstream outChimSAM, outChimJunction, logMain, logProgress, logFinal, outUnmappedReadsStream[MAX_N_MATES];
    ifstream readIn[MAX_N_MATES];

    //compilation-optional streams
    ofstream outLocalChains;

    InOutStreams();
    ~InOutStreams();
};

#endif
