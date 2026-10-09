#ifndef STAR_WINDOW_STITCHER_H
#define STAR_WINDOW_STITCHER_H
#include "IncludeDefine.h"
class Parameters;
class Genome;
class Transcript;

// Explicit synchronous window workspace. No read parser, output stream,
// ReadAlign owner, RNG or allocations; caller retains all scratch/result storage.
struct WindowStitcher {
    Parameters& P;
    Genome& mapGen;
    const uint* nWA;
    uiWA** WA;
    intScore* scoreSeedBest;
    uint *scoreSeedBestMM,*scoreSeedBestInd,*seedChain;
    bool* WAincl;
    Transcript &trA,&trA1;
    const Transcript* trInit;
    uint Lread,outFilterMismatchNmaxTotal;
    intScore* maxScoreMate;
    Transcript*** trAll;
    uint* nWinTr;
    void stitch(uint window,uint resultWindow,bool* excluded,char* read);
};
#endif
