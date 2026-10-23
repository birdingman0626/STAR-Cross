#ifndef STAR_STITCH_WINDOW_ALIGNS_H
#define STAR_STITCH_WINDOW_ALIGNS_H
#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "extendAlign.h"
#include "stitchAlignToTranscript.h"
struct WindowAlignmentContext {
    uint64 mismatchLimit;
    const uint64* readLength;
    intScore* maxScoreMate;
};

void stitchWindowAligns(uint64 iA, uint64 nA, int Score, bool WAincl[], uint64 tR2, uint64 tG2, Transcript trA, \
                        uint64 Lread, uiWA* WA, char* R, Genome &mapGen, \
                        Parameters& P, Transcript** wTr, uint64* nWinTr, const WindowAlignmentContext& context);
    //recursively stitch aligns for one gene
    //*nWinTr - number of transcripts for the current window
#endif
