#include "ReadAlign.h"
#include "WindowStitcher.h"
using namespace std;
void ReadAlign::stitchWindowSeeds(uint64 iW,uint64 iWrec,bool* excluded,char* read) {
    WindowStitcher{P,mapGen,nWA,WA,scoreSeedBest,scoreSeedBestMM,
        scoreSeedBestInd,seedChain,WAincl,trA,trA1,trInit,
        Lread,outFilterMismatchNmaxTotal,maxScoreMate,trAll,nWinTr}
        .stitch(iW,iWrec,excluded,read);
}
