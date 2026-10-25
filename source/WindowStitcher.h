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
    const uint64* nWA;
    uiWA** WA;
    intScore* scoreSeedBest;
    uint64 *scoreSeedBestMM,*scoreSeedBestInd,*seedChain;
    bool* WAincl;
    Transcript &trA,&trA1;
    const Transcript* trInit;
    uint64 Lread,outFilterMismatchNmaxTotal;
    intScore* maxScoreMate;
    Transcript*** trAll;
    uint64* nWinTr;
    void stitch(uint64 window,uint64 resultWindow,bool* excluded,char* read);
};
#endif
