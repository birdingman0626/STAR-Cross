#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "Genome.h"

intScore stitchAlignToTranscript(uint64 rAend, uint64 gAend, uint64 rBstart, uint64 gBstart, uint64 L, uint64 iFragB, uint64 sjAB, Parameters& P, char* R, Genome &mapGen,  Transcript *trA, const uint64 outFilterMismatchNmaxTotal);

