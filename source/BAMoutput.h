#ifndef CODE_BAMoutput
#define CODE_BAMoutput

#include "IncludeDefine.h"
#include SAMTOOLS_BGZF_H
#include "Parameters.h"
#include <memory>

class BAMoutput {//
public:
    BAMoutput(const BAMoutput&) = delete;
    BAMoutput& operator=(const BAMoutput&) = delete;
    //sorted output
    BAMoutput (int iChunk, string tmpDir, Parameters &Pin);
    void coordOneAlign (char *bamIn, uint bamSize, uint iRead);
    void coordBins ();
    void coordFlush ();
    //unsorted output
    BAMoutput (BGZF *bgzfBAMin, Parameters &Pin);
    void unsortedOneAlign (char *bamIn, uint bamSize, uint bamSize2);
    void unsortedFlush ();
    void coordUnmappedPrepareBySJout();
    void finalize(); //checked close for owned streams; never close borrowed BGZF

    uint32 nBins; //number of bins to split genome into
    std::unique_ptr<uint[]> binTotalN; //total number of aligns in each bin
    std::unique_ptr<uint[]> binTotalBytes;//total size of aligns in each bin
private:
    uint64 bamArraySize; //this size will be allocated
    std::unique_ptr<char[]> bamArray; //uninitialized storage; no extra full-buffer zero fill
    uint64 binSize, binSize1;//storage size of each bin
    uint64 binGlen;//bin genomic length
    std::unique_ptr<char*[]> binStart; //borrowed interior pointers, not allocation owners
    std::unique_ptr<uint64[]> binBytes;
    uint64 binBytes1;//number of bytes currently written to each bin
    std::vector<std::unique_ptr<ofstream>> binStream;
    BGZF *bgzfBAM=nullptr; //borrowed from Parameters; never closed by this owner
    Parameters &P;
    string bamDir;
    void checkStream(uint32 bin);
};

#endif
