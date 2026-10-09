#ifndef STAR_ALIGN_FILTER_CONFIG_H
#define STAR_ALIGN_FILTER_CONFIG_H
#include "IncludeDefine.h"
// Parsed configuration only; per-read counters/scratch stay in ReadAlign.
struct AlignFilterConfig {
    uint mismatchMax, matchMin, multimapMax;
    double mismatchOverLengthMax, mismatchOverReadLengthMax;
    double scoreOverReadLengthMin, matchOverReadLengthMin;
    intScore scoreMin, multimapScoreRange;
};
#endif
