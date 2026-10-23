#ifndef CODE_OutSJ
#define CODE_OutSJ

#include "Parameters.h"
#include "Genome.h"
#include "unaligned.h"

class Junction {//one junction
public:
    const static uint64 startP=0;
    const static uint64 gapP=startP+sizeof(uint64);
    const static uint64 strandP=gapP+sizeof(uint32);
    const static uint64 motifP=strandP+sizeof(char);
    const static uint64 annotP=motifP+sizeof(char);
    const static uint64 countUniqueP=annotP+sizeof(char);
    const static uint64 countMultipleP=countUniqueP+sizeof(uint32);
    const static uint64 overhangLeftP=countMultipleP+sizeof(uint32);
    const static uint64 overhangRightP=overhangLeftP+sizeof(uint16);

    UnalignedPointer<uint64> start;
    UnalignedPointer<uint32> gap;
    char *strand, *motif, *annot;
    UnalignedPointer<uint32> countUnique, countMultiple;
    UnalignedPointer<uint16> overhangLeft, overhangRight;

    const static uint64 dataSize=overhangRightP+sizeof(uint16);

    Junction(Genome &genomeIn);
    void junctionPointer(char* sjPoint, uint64 isj);
    void outputStream(ostream &outStream);
    void collapseOneSJ(char* isj1P, char* isjP, Parameters& P);

private:
    Genome &genOut;
};

class OutSJ {

public:
    //all junctions
    char* data; //sj array[Njunctions][dataSize]
    vector<char> dataVec;
    uint64 N, Nstore; //N=number of junctions stored; Nstore=storage size
    Junction oneSJ;

    OutSJ(uint64 nSJmax, Parameters &Pin, Genome &genomeIn);
    void collapseSJ();//collapse the junctions in data
//     int compareSJ(void* i1, void* i2);

    void dataSizeIncrease();

private:
    Parameters &P;
    Genome &genOut;
};

int compareSJ(const void* i1, const void* i2); //external functions

#endif

