#ifndef H_SuperTranscriptome
#define H_SuperTranscriptome

#include "IncludeDefine.h"
#include "Parameters.h"

struct sjInfo {
    uint32 start;
    uint32 end;
    uint32 tr;
    uint32 super;
};

class SuperTranscript {//one supertranscript
public:
    uint8 *seqP;//pointer to sequence
    uint32 length;
    std::vector<std::array<uint32,3>> sjC;//collapsed junctions
    std::vector<uint32> sjDonor;//SJ donor coordinates, sorted
};


class SuperTranscriptome {
    private:
    Parameters &P;
public:   
    std::vector<uint8> seqConcat;//concatenated sequences of supertranscripts, a.k.a. Condensed Genome 
    std::vector<std::vector<uint8>> seq;//sequences of supertranscripts
    std::vector<uint64> trIndex;//superTr's index this tr belongs to
    std::vector<std::array<uint64,2>> trStartEnd;//tr start/end in the superTr it belongs to
    std::vector<sjInfo> sj;//all junctions
    
    std::vector<SuperTranscript> superTrs;
    uint32 sjNmax, sjDonorNmax;//max number of SJs per superTr, SJ donors
    uint32 N; //number of superTr
    
    
    SuperTranscriptome(Parameters &P) : P(P) {};
    void sjCollapse();
    void load(char *G, std::vector<uint64> &chrStart, std::vector<uint64> &chrLength);
};
#endif
