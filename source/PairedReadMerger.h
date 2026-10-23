#ifndef STAR_PAIRED_READ_MERGER_H
#define STAR_PAIRED_READ_MERGER_H
#include "IncludeDefine.h"

struct PairedReadMergeResult {
    uint64 overlap, mateStart[2], length;
};

// Numeric mates occupy [mate0][separator][mate1]. Mutates caller storage only
// after a valid overlap and sufficient scratch capacity have been established.
// Complement/reverse-complement generation and remapping belong to the caller.
PairedReadMergeResult mergePairedRead(char* bases, size_t capacity,
                                    uint64 length0, uint64 length1,
                                    uint64 minimumOverlap, double mismatchRatio);
#endif
