#ifndef STAR_STITCH_WINDOW_ALIGNS_H
#define STAR_STITCH_WINDOW_ALIGNS_H
#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "extendAlign.h"
#include "stitchAlignToTranscript.h"
struct WindowAlignmentContext {
    uint mismatchLimit;
    const uint* readLength;
    intScore* maxScoreMate;
};

void stitchWindowAligns(uint iA, uint nA, int Score, bool WAincl[], uint tR2, uint tG2, Transcript trA, \
                        uint Lread, uiWA* WA, char* R, Genome &mapGen, \
                        Parameters& P, Transcript** wTr, uint* nWinTr, const WindowAlignmentContext& context);
    //recursively stitch aligns for one gene
    //*nWinTr - number of transcripts for the current window
#endif
