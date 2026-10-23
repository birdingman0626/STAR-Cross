#ifndef CODE_SuffixArrayFuns
#define CODE_SuffixArrayFuns

#include "IncludeDefine.h"
#include "SeedIndexView.h"

uint64 medianUint2(uint64, uint64);
uint64 compareSeqToGenome(const SeedIndexView &mapGen, const char* const* s2, uint64 S, uint64 N, uint64 L, uint64 iSA, bool dirR, bool& comparRes);
uint64 findMultRange(const SeedIndexView &mapGen, uint64 i3, uint64 L3, uint64 i1, uint64 L1, uint64 i1a, uint64 L1a, uint64 i1b, uint64 L1b, const char* const* s, bool dirR, uint64 S);
uint64 maxMappableLength(const SeedIndexView &mapGen, const char* const* s, uint64 S, uint64 N, uint64 i1, uint64 i2, bool dirR, uint64& L, uint64* indStartEnd);
#ifdef STAR_SEED_HINT_REPLAY
// Replay only: one preferred interior probe; the eligible bounds stay exact.
uint64 maxMappableLengthHint(const SeedIndexView &mapGen, const char* const* s, uint64 S, uint64 N, uint64 i1, uint64 i2,
                           bool dirR, uint64& L, uint64* indStartEnd, uint64 preferredProbe);
#endif
uint64 suffixArraySearch1(const SeedIndexView &mapGen, const char* const* s2, uint64 S, uint64 N, uint64 gInsert, bool dirR, uint64 i1, uint64 i2, uint64 L);
int64 funCalcSAi(char *G, uint64 iL);
uint64 funCalcSAiFromSA(char* gSeq, PackedArray& gSA, const SeedIndexView &mapGen, uint64 iSA, int L, int & iL4);
#endif
