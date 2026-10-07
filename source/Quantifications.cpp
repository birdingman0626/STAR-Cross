#include "Quantifications.h"

Quantifications::Quantifications (uint32 nGeIn) {

    geneCounts.nType=3;
    geneCounts.cAmbig = ambiguous.data();
    geneCounts.cNone = noFeature.data();

    geneCounts.nGe=nGeIn;
    geneCounts.gCount = countViews.data();

    geneCounts.cMulti=0;
    for (int itype=0; itype<geneCounts.nType; itype++)
    {
        geneCounts.cAmbig[itype]=0;
        geneCounts.cNone[itype]=0;
        counts[itype].reset(new uintQ [geneCounts.nGe]);
        geneCounts.gCount[itype] = counts[itype].get();
        for (uint32 ii=0; ii<geneCounts.nGe; ii++)
        {
            geneCounts.gCount[itype][ii]=0;
        };
    };
};

void Quantifications::addQuants(const Quantifications & quantsIn)
{
    geneCounts.cMulti += quantsIn.geneCounts.cMulti;
    for (int itype=0; itype<geneCounts.nType; itype++)
    {
        geneCounts.cAmbig[itype] += quantsIn.geneCounts.cAmbig[itype];
        geneCounts.cNone[itype] += quantsIn.geneCounts.cNone[itype];
        for (uint32 ii=0; ii<geneCounts.nGe; ii++)
        {
            geneCounts.gCount[itype][ii] += quantsIn.geneCounts.gCount[itype][ii];
        };
    };
};
