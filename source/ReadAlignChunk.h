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
#include <memory>
class SeedBatchExecutor;

void fillInputChunk(Parameters& P,char** chunkIn,std::array<uint64,MAX_N_MATES>& chunkInSizeBytesTotal,
                    std::vector<std::string>& headerExtra,bool& newFile,int iThread,bool& noReadsLeft,uint64& iChunkIn);

class ReadAlignChunk {//chunk of reads and alignments
public:
    Parameters& P;
    ReadAlign* RA;
    double phaseSeconds[9]={0,0,0,0,0,0,0,0,0}; //read, load, seed, project, collect, stitch, align, write, clip
    unsigned cpuWarmupChunks=0; //chunks mapped on the CPU path while the device index was uploading
    double inputLockHeldSeconds=0; //time this worker held the input mutex
    uint64 cpuWarmupReads=0; //reads in those chunks mapped before the device path took over
    SeedBatchExecutor* seedBatch=nullptr; //borrowed for one joined mapping pass
    std::shared_ptr<void> mapBuffers; //batch buffers kept across mapChunk calls (see ReadAlignChunk_mapChunk.cpp)

    Transcriptome *chunkTr;

    char **chunkIn; //space for the chunk of input reads
    std::array<uint64, MAX_N_MATES> chunkInSizeBytesTotal;    
    
    char *chunkOutBAM, *chunkOutBAM1;//space for the chunk of output SAM
    OutSJ *chunkOutSJ, *chunkOutSJ1;

    std::unique_ptr<BAMoutput> chunkOutBAMcoord, chunkOutBAMunsorted, chunkOutBAMquant;
    
    FixedIStream** readInStream;
    FixedOStream*  chunkOutBAMstream;
    std::ofstream chunkOutBAMfile;
    std::string chunkOutBAMfileName;

    bool noReadsLeft;
    uint64 iChunkIn; //current chunk # as read from .fastq
    uint64 iChunkOutSAM; //current chunk # writtedn to Aligned.out.sam
    int iThread; //current thread
    uint64 chunkOutBAMtotal; //total number of bytes in the write buffer

    ReadAlignChunk(Parameters& Pin, Genome &genomeIn, Transcriptome *TrIn, int iChunk);
    void processChunks();
    void readChunk();
    void mapChunk();
    void chunkFstreamOpen(std::string filePrefix, int iChunk, std::fstream &fstreamOut);
    void chunkFstreamCat (std::fstream &chunkOut, std::ofstream &allOut, bool mutexFlag, pthread_mutex_t &mutexVal);
    void chunkFilesCat(std::ostream *allOut, std::string filePrefix, uint64 &iC);

    Genome &mapGen;
private:
    //mapChunk steps (ReadAlignChunk_mapChunk.cpp)
    void clipChunkMates(SeedBatchExecutor* batch);
    void flushChunkSAM(bool lastRead);
    void collapseChunkSJ(OutSJ *sj, const char *name, bool lastRead);
    void finishChunk();

    bool newFile=false;
    std::vector<std::string> headerExtra;
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
