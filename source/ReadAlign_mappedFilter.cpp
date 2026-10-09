#include "ReadAlign.h"
#include "AlignmentFilter.h"

void ReadAlign::mappedFilter() {//filter mapped read, add to stats
    AlignmentFilterInput read{};
    read.windows=nW;
    if (nW) {
        read.matches=trBest->nMatch; read.mismatches=trBest->nMM;
        read.readLength=Lread; read.alignedLength=trBest->rLength;
        read.alignments=nTr; read.mismatchLimit=outFilterMismatchNmaxTotal;
        read.score=trBest->maxScore;
    }
    unmapType=classifyAlignment(P.alignFilter,read);
    switch (unmapType) {
        case 0: ++statsRA.unmappedOther; break;
        case 1: ++statsRA.unmappedShort; break;
        case 2: ++statsRA.unmappedMismatch; break;
        case 3: ++statsRA.unmappedMulti; break;
    }

    return;
};
