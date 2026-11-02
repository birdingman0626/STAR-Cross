#ifndef H_GTF
#define H_GTF

#include "IncludeDefine.h"
#include "Parameters.h"
#include "SjdbClass.h"
#include "Genome.h"
#include "SuperTranscriptome.h"

class GTF {
private:
    Genome &genome;
    Parameters &P;
    std::string dirOut;
    SjdbClass &sjdbLoci;
public:
    bool gtfYes; 

    enum {exT,exS,exE,exG,exL}; //indexes in the exonLoci array
    uint64 exonN;
    std::vector<std::array<uint64,exL>> exonLoci;
    std::vector<uint32> transcriptStrand;
    std::vector <std::string> transcriptID, geneID;    
    std::vector<std::array<std::string,2>> geneAttr;

    std::vector<std::vector<uint8>> transcriptSeq;//sequences of normal transcripts
    std::vector<std::array<uint64,2>> transcriptStartEnd;//normal transcripts start/end in the normal genome

    SuperTranscriptome superTrome;
    
    GTF(Genome &genomeIn, Parameters &Pin, const std::string &dirOut, SjdbClass &sjdbLoci);
    uint64 transcriptGeneSJ(const std::string &dirOutArg);
    void superTranscript();

private:
    struct SuperTranscriptLayout {//intermediate results of superTranscript()
        uint64 nMinusStrandOffset=0;//(-)strand exons are converted with this genome length
        std::vector<std::array<uint64,2>> mergedIntervals;//union of all exons, full genome coordinates
        std::vector<std::array<uint64,2>> superTrStartEnd;//superTranscripts, condensed genome coordinates
        std::vector<uint64> mergedIntervalsSuperTrIndex;//superTranscript of each merged interval
    };
    void superTranscriptWriteFullGenome();
    void superTranscriptFillMinusStrand();
    void superTranscriptConvertMinusStrand(const SuperTranscriptLayout &layout);
    void superTranscriptCondenseGenome(SuperTranscriptLayout &layout);
    void superTranscriptMergeTranscripts(SuperTranscriptLayout &layout);
    void superTranscriptSequences(const SuperTranscriptLayout &layout);
    void superTranscriptNormalSequences();
    void superTranscriptJunctions(const SuperTranscriptLayout &layout);
    void superTranscriptReplaceGenome(const SuperTranscriptLayout &layout);
    void superTranscriptWriteConversion(const SuperTranscriptLayout &layout);
    //transcriptGeneSJ steps (GTF_transcriptGeneSJ.cpp)
    void transcriptGeneSJwriteExonGeneInfo(const std::string &dirOutArg);
    void transcriptGeneSJwriteTranscriptInfo(const std::string &dirOutArg);
    uint64 transcriptGeneSJjunctions(uint64 *sjLoci);
    void transcriptGeneSJcollapse(const uint64 *sjLoci, uint64 sjN);
    void transcriptGeneSJwriteList(const std::string &dirOutArg, uint64 sjdbN1);
};

#endif
