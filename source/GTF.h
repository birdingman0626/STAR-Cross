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
    uint64 transcriptGeneSJ(const std::string &dirOut);
    void superTranscript();
};

#endif
