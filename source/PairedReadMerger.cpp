#include "PairedReadMerger.h"
#include "SequenceFuns.h"
#include <stdexcept>

PairedReadMergeResult mergePairedRead(char* bases, size_t capacity,
                                    uint length0, uint length1,
                                    uint minimumOverlap, double mismatchRatio) {
    if (!bases || length0>=capacity || length1>capacity-length0-1)
        throw std::invalid_argument("paired-read input exceeds buffer capacity");
    const uint total=length0+length1+1;
    const uint s1=localSearchNisMM(bases,length0,bases+length0+1,length1,mismatchRatio);
    const uint s0=localSearchNisMM(bases+length0+1,length1,bases,length0,mismatchRatio);
    const uint o1=min(length1,length0-s1), o0=min(length0,length1-s0);
    PairedReadMergeResult result{max(o0,o1),{0,0},total};
    if (result.overlap<minimumOverlap) {result.overlap=0; return result;}
    if (o1>=o0) {
        result.mateStart[1]=s1;
        if (o1<length1) memmove(bases+length0,bases+length0+1+o1,length1-o1);
    } else {
        // Preserve the legacy in-place layout, including its temporary copy.
        if (length0>capacity-total)
            throw std::invalid_argument("paired-read merge scratch exceeds buffer capacity");
        result.mateStart[0]=s0;
        memmove(bases+total,bases,length0);
        memmove(bases,bases+length0+1,length1);
        if (o0<length0) memmove(bases+length1,bases+total+o0,length0-o0);
    }
    result.length=total-result.overlap-1;
    return result;
}
