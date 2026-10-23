#include "sjAlignSplit.h"

bool sjAlignSplit(uint64 a1,uint64 aLength, const Genome &mapGen, uint64 &a1D, uint64 &aLengthD, uint64 &a1A, uint64 &aLengthA, uint64 &isj) {
    uint64 sj1=(a1-mapGen.sjGstart)%mapGen.sjdbLength;
    if (sj1<mapGen.sjdbOverhang && sj1+aLength>mapGen.sjdbOverhang) {//align crosses the junctions
        isj=(a1-mapGen.sjGstart)/mapGen.sjdbLength;
        aLengthD=mapGen.sjdbOverhang-sj1;
        aLengthA=aLength-aLengthD;
        a1D=mapGen.sjDstart[isj]+sj1;
        a1A=mapGen.sjAstart[isj];
        return true;
    } else {
        return false;
    };
};
