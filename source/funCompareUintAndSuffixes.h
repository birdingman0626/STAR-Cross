#ifndef CODE_funCompareUintAndSuffixes
#define CODE_funCompareUintAndSuffixes

#include "IncludeDefine.h"
#include <string.h> //memcmp
#include <algorithm>
#include <type_traits>

// Orders the {insertion point, suffix start} pairs built by sjdbBuildIndex and
// insertSeqSA. Each comparator carries its own genome context, so concurrent
// sorts on different sequences cannot interfere. Both are strict total orders
// (ties on the suffix fall back to the suffix start), so std::sort yields the
// same permutation the former qsort calls did.

struct UintAndSuffix {
    uint64 key; //insertion point in the existing suffix array
    uint64 idx; //suffix start in the sequence
};
static_assert(sizeof(UintAndSuffix)==2*sizeof(uint64) && std::is_standard_layout_v<UintAndSuffix>,
              "UintAndSuffix must overlay one pair of uint64 in indArray");

// Suffixes are compared up to the first chromosome separator (5) they share.
struct CompareUintAndSuffixes {
    const char *G;

    int compare(const UintAndSuffix &a, const UintAndSuffix &b) const {
        if (&a==&b) return 0;
        if (a.key!=b.key) return a.key>b.key ? 1 : -1;
        const char *ga=G+a.idx;
        const char *gb=G+b.idx;
        for (uint64 ig=0; ; ig++) {
            if (ga[ig]>gb[ig]) return 1;
            if (ga[ig]<gb[ig]) return -1;
            if (ga[ig]==5) {//reached the end of chr, compare the indexes for a stable order
                if (a.idx==b.idx) return 0;
                return a.idx>b.idx ? 1 : -1;
            };
        };
    };
    bool operator()(const UintAndSuffix &a, const UintAndSuffix &b) const {
        return compare(a,b)<0;
    };
};

// Suffixes are compared over at most L bytes, never past the sequence length N.
struct CompareUintAndSuffixesMemcmp {
    const char *G;
    uint64 L;
    uint64 N;

    int compare(const UintAndSuffix &a, const UintAndSuffix &b) const {
        if (a.key!=b.key) return a.key>b.key ? 1 : -1;
        const uint64 length=std::min(L, N-std::max(a.idx,b.idx));
        int comp=memcmp(G+a.idx,G+b.idx,length);
        if (comp==0) {
            if (a.idx==b.idx) return 0;
            comp=a.idx>b.idx ? 1 : -1;
        };
        return comp;
    };
    bool operator()(const UintAndSuffix &a, const UintAndSuffix &b) const {
        return compare(a,b)<0;
    };
};

// indArray holds nInd consecutive {key, idx} pairs of uint64.
template <class Compare>
inline void sortUintAndSuffixes(uint64 *indArray, uint64 nInd, const Compare &comp) {
    UintAndSuffix *pairs=reinterpret_cast<UintAndSuffix*>(indArray);
    std::sort(pairs, pairs+nInd, comp);
};

#endif
