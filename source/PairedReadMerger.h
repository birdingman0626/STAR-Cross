#ifndef STAR_PAIRED_READ_MERGER_H
#define STAR_PAIRED_READ_MERGER_H
#include "IncludeDefine.h"

struct PairedReadMergeResult {
    uint overlap, mateStart[2], length;
};

// Numeric mates occupy [mate0][separator][mate1]. Mutates caller storage only
// after a valid overlap and sufficient scratch capacity have been established.
// Complement/reverse-complement generation and remapping belong to the caller.
PairedReadMergeResult mergePairedRead(char* bases, size_t capacity,
                                    uint length0, uint length1,
                                    uint minimumOverlap, double mismatchRatio);
#endif
