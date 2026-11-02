/*
 * Created by Fahimeh Mirhaj on 6/10/19.
*/
using namespace std;

#include "SpliceGraph.h"
#include "GTF.h"
#include "uninitArray.h"
SpliceGraph::SpliceGraph (SuperTranscriptome &superTrome, Parameters &P, ReadAlign *RA) : superTrome(superTrome), P(P), RA(RA)
{
    //find candidate superTr
    seedCountStorage=makeUninitArray<typeSuperTrSeedCount>(2*superTrome.N); superTrSeedCount=seedCountStorage.get();
    
    //Smith-Waterman
    scoringViews=makeUninitArray<typeAlignScore*>(superTrome.sjDonorNmax+2); scoringMatrix=scoringViews.get();
    for (size_t i=0; i<2; ++i) {
        columnStorage[i]=makeUninitArray<typeAlignScore>(maxSeqLength); scoreTwoColumns[i]=columnStorage[i].get();
    }
    scoringStorage.resize(superTrome.sjDonorNmax+2);
    for(uint32 ii = 0; ii < superTrome.sjDonorNmax+2; ii++) {
        scoringStorage[ii]=makeUninitArray<typeAlignScore>(maxSeqLength); scoringMatrix[ii]=scoringStorage[ii].get();
    };
    junctionIndexStorage=makeUninitArray<uint32>(superTrome.sjDonorNmax); sjDindex=junctionIndexStorage.get();
    
};
