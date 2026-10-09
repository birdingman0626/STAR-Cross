#ifndef STAR_ALIGNMENT_FILTER_H
#define STAR_ALIGNMENT_FILTER_H
#include "AlignFilterConfig.h"

struct AlignmentFilterInput {
    uint windows, matches, mismatches, readLength, alignedLength, alignments, mismatchLimit;
    intScore score;
};
// Preserve STAR's ordered classification and inclusive threshold semantics.
inline int classifyAlignment(const AlignFilterConfig& config, AlignmentFilterInput read) {
    if (!read.windows) return 0;
    if (read.score<config.scoreMin || read.score<(intScore)(config.scoreOverReadLengthMin*(read.readLength-1))
        || read.matches<config.matchMin || read.matches<(uint)(config.matchOverReadLengthMin*(read.readLength-1))) return 1;
    if (read.mismatches>read.mismatchLimit
        || double(read.mismatches)/double(read.alignedLength)>config.mismatchOverLengthMax) return 2;
    if (read.alignments>config.multimapMax) return 3;
    return -1;
}
#endif
