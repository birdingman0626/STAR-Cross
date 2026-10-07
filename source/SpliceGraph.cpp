/*
 * Created by Fahimeh Mirhaj on 6/10/19.
*/
using namespace std;

#include "SpliceGraph.h"
#include "GTF.h"
SpliceGraph::SpliceGraph (SuperTranscriptome &superTrome, Parameters &P, ReadAlign *RA) : superTrome(superTrome), P(P), RA(RA)
{
    //find candidate superTr
    seedCountStorage.reset(new typeSuperTrSeedCount[2*superTrome.N]); superTrSeedCount=seedCountStorage.get();
    
    //Smith-Waterman
    scoringViews.reset(new typeAlignScore*[superTrome.sjDonorNmax+2]); scoringMatrix=scoringViews.get();
    for (size_t i=0; i<2; ++i) {
        columnStorage[i].reset(new typeAlignScore[maxSeqLength]); scoreTwoColumns[i]=columnStorage[i].get();
    }
    scoringStorage.resize(superTrome.sjDonorNmax+2);
    for(uint32 ii = 0; ii < superTrome.sjDonorNmax+2; ii++) {
        scoringStorage[ii].reset(new typeAlignScore[maxSeqLength]); scoringMatrix[ii]=scoringStorage[ii].get();
    };
    junctionIndexStorage.reset(new uint32[superTrome.sjDonorNmax]); sjDindex=junctionIndexStorage.get();
    
    //rowCol.reserve(100000);
    //rowSJ.reserve(100000);
    //blockCoord.reserve(100000);
    //blockSJ.reserve(10000);
};
