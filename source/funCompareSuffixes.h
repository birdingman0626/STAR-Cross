#ifndef CODE_funCompareSuffixes
#define CODE_funCompareSuffixes

#include "IncludeDefine.h"
#include "byteOrder.h"

// Orders suffix-array indices during genomeGenerate. G is the genome re-filled
// backwards for sorting, L the maximum compared suffix length in 8-byte words.
// wordStart=1 skips the first word for ranges that share it (upstream PR #2687).
// Genome words are read with loadUintLE, so the order is byte-order independent.
// This is a strict total order on distinct indices (equal suffixes are ordered by
// descending index), so std::sort yields the same permutation as qsort did.
struct CompareSuffixes {
    const char *G;
    uint64 L;
    uint64 wordStart;

    int compare(uint64 a, uint64 b) const {
        const char *ga=((G-7LLU)+a) - 8*wordStart;
        const char *gb=((G-7LLU)+b) - 8*wordStart;

        for (uint64 jj=0; jj+wordStart < L; jj++) {
            uint64 va=loadUintLE(ga-8*jj); // little-endian 8-byte load (native on LE)
            uint64 vb=loadUintLE(gb-8*jj);

            if (has5(va) && has5(vb)) {//there is 5 in the sequence - only compare bytes before 5
                for (int ii=7;ii>=0;ii--) {//extract byte ii via shift: portable regardless of host byte order
                    uchar va1=(uchar)((va>>(8*ii))&0xffLLU);
                    uchar vb1=(uchar)((vb>>(8*ii))&0xffLLU);
                    if (va1>vb1) {
                        return 1;
                    } else if (va1<vb1) {
                        return -1;
                    } else if (va1==5) {//va=vb at the end of chr
                        return a>b ? -1 : 1;//anti-stable order, since indexes are sorted in the reverse order
                    };
                };
            } else if (va!=vb) {//no 5, simple comparison
                return va>vb ? 1 : -1;
            };
        };

        //suffixes are equal up to L, simply compare the indexes
        return a>b ? -1 : 1;//anti-stable order, since indexes are sorted in the reverse order
    };
    bool operator()(uint64 a, uint64 b) const {
        return a!=b && compare(a,b)<0;
    };

    static bool has5(uint64 v) {
        const uint64 x=v^0x0505050505050505LLU;
        return ((x - 0x0101010101010101LLU) & ~x & 0x8080808080808080LLU) != 0;
    };
};

#endif
