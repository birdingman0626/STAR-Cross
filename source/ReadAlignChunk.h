#ifndef CODE_ReadAlignChunk
#define CODE_ReadAlignChunk

#include "IncludeDefine.h"
#include "Parameters.h"
#include "ReadAlign.h"
#include "OutSJ.h"
#include "Transcriptome.h"
#include "BAMoutput.h"
#include "Quantifications.h"
#include "FixedStreamBuf.h"

class ReadAlignChunk {//chunk of reads and alignments
public:
    Parameters& P;
    ReadAlign* RA;

    Transcriptome *chunkTr;

    char **chunkIn; //space for the chunk of input reads
    array<uint64, MAX_N_MATES> chunkInSizeBytesTotal;    
    
    char *chunkOutBAM, *chunkOutBAM1;//space for the chunk of output SAM
    OutSJ *chunkOutSJ, *chunkOutSJ1;

    std::unique_ptr<BAMoutput> chunkOutBAMcoord, chunkOutBAMunsorted, chunkOutBAMquant;
    
    FixedIStream** readInStream;
    FixedOStream*  chunkOutBAMstream;
    ofstream chunkOutBAMfile;
    string chunkOutBAMfileName;

    bool noReadsLeft;
    uint iChunkIn; //current chunk # as read from .fastq
    uint iChunkOutSAM; //current chunk # writtedn to Aligned.out.sam
    int iThread; //current thread
    uint chunkOutBAMtotal; //total number of bytes in the write buffer

    ReadAlignChunk(Parameters& Pin, Genome &genomeIn, Transcriptome *TrIn, int iChunk);
    void processChunks();
    void mapChunk();
    void chunkFstreamOpen(string filePrefix, int iChunk, fstream &fstreamOut);
    void chunkFstreamCat (fstream &chunkOut, ofstream &allOut, bool mutexFlag, pthread_mutex_t &mutexVal);
    void chunkFilesCat(ostream *allOut, string filePrefix, uint &iC);

    Genome &mapGen;
private:
    std::unique_ptr<Transcriptome> transcriptomeStorage;
    std::unique_ptr<Quantifications> quantificationStorage;
    std::unique_ptr<char*[]> inputPointers;
    std::unique_ptr<FixedIStream*[]> inputStreamPointers;
    std::vector<std::unique_ptr<char[]>> inputStorage;
    std::vector<std::unique_ptr<FixedIStream>> inputStreams;
    std::unique_ptr<char[]> outputStorage;
    std::unique_ptr<FixedOStream> outputStreamStorage;
    std::unique_ptr<OutSJ> junctionStorage, filteredJunctionStorage;
    std::unique_ptr<ReadAlign> alignStorage, waspStorage, mergedStorage;
};
#endif
