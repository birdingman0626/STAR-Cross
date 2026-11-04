#include "doctest/doctest.h"
#include "funCompareUintAndSuffixes.h"
#include "funCompareSuffixes.h"
#include <cstdlib>
#include <random>
#include <vector>
using namespace std;

// Former qsort comparators, kept verbatim (apart from names) as the reference
// order. Their genome context lives in file-local globals, as it did in source.
namespace legacy {
char* uintG;
int compareUintAndSuffixes ( const void *a, const void *b){
    if (a==b) return 0;
    uint64_t* va= ((uint64_t*) a);
    uint64_t* vb= ((uint64_t*) b);
    if (va[0]>vb[0]) {
        return 1;
    } else if (va[0]<vb[0]) {
        return -1;
    } else {
        char* ga=uintG + *( ((uint64_t*) a)+1);
        char* gb=uintG + *( ((uint64_t*) b)+1);
        int ig=0;
        while (true) {
            if (ga[ig]>gb[ig]) {
                return 1;
            } else if (ga[ig]<gb[ig]) {
                return -1;
            } else if (ga[ig]==5) {
                if (va[1]==vb[1]) return 0;
                return va[1]>vb[1] ? 1 : -1;
            } else {
                ig++;
            };
        };
    };
};

char* memcmpG;
uint64_t memcmpL, memcmpN;
int compareUintAndSuffixesMemcmp ( const void *a, const void *b){
    uint64_t* va= ((uint64_t*) a);
    uint64_t* vb= ((uint64_t*) b);
    if (va[0]>vb[0]) {
        return 1;
    } else if (va[0]<vb[0]) {
        return -1;
    } else {
        const auto length=std::min(memcmpL, memcmpN-std::max(va[1],vb[1]));
        int comp=memcmp(memcmpG+va[1],memcmpG+vb[1],length);
        if (comp==0) {
            if (va[1]==vb[1]) return 0;
            comp=va[1]>vb[1] ? 1 : -1;
        };
        return comp;
    };
};

char* globalG;
uint64 globalL;
inline int compareSuffixesFromWord ( const void *a, const void *b, uint64 wordStart){
    const char *ga=((globalG-7LLU)+(*((uint64*)a))) - 8*wordStart;
    const char *gb=((globalG-7LLU)+(*((uint64*)b))) - 8*wordStart;
    uint64 jj=0;
    int  ii=0;
    uint64 va=0,vb=0;
    while (jj+wordStart < globalL) {
        va=loadUintLE(ga-8*jj);
        vb=loadUintLE(gb-8*jj);
        #define has5(v) ((((v)^0x0505050505050505) - 0x0101010101010101) & ~((v)^0x0505050505050505) & 0x8080808080808080)
        if (has5(va) && has5(vb)) {
            for (ii=7;ii>=0;ii--) {
                uchar va1=(uchar)((va>>(8*ii))&0xffLLU);
                uchar vb1=(uchar)((vb>>(8*ii))&0xffLLU);
                if (va1>vb1) {
                    return 1;
                } else if (va1<vb1) {
                    return -1;
                } else if (va1==5) {
                    return ( *((uint64*)a) > *((uint64*)b) ) ? -1 : 1;
                };
            };
        } else {
            if (va>vb) {
                return 1;
            } else if (va<vb) {
                return -1;
            };
        };
        #undef has5
        jj++;
    };
    return ( *((uint64*)a) > *((uint64*)b) ) ? -1 : 1;
};
int compareSuffixes ( const void *a, const void *b) {return compareSuffixesFromWord(a,b,0);};
int compareSuffixesSkipFirstWord ( const void *a, const void *b) {return compareSuffixesFromWord(a,b,1);};
} // namespace legacy

static int compareUint(const CompareUintAndSuffixes &c, const uint64 *a, const uint64 *b) {
    return c.compare(*reinterpret_cast<const UintAndSuffix*>(a), *reinterpret_cast<const UintAndSuffix*>(b));
}
static int compareMemcmp(const CompareUintAndSuffixesMemcmp &c, const uint64 *a, const uint64 *b) {
    return c.compare(*reinterpret_cast<const UintAndSuffix*>(a), *reinterpret_cast<const UintAndSuffix*>(b));
}

TEST_CASE("Inserted suffix order terminates at separator and is reflexive") {
    char bases[]={0,1,5,0,1,5,0,2,5};
    const CompareUintAndSuffixes comp{bases};
    uint64 a[]={4,0},b[]={4,3},c[]={4,6},lower[]={3,6};
    CHECK(compareUint(comp,a,a)==0);
    CHECK(compareUint(comp,a,b)<0);
    CHECK(compareUint(comp,b,a)>0);
    CHECK(compareUint(comp,b,c)<0);
    CHECK(compareUint(comp,c,b)>0);
    CHECK(compareUint(comp,lower,a)<0);
    uint64 copy[]={4,0};
    CHECK(compareUint(comp,a,copy)==0);
}

TEST_CASE("Finite inserted suffix comparison never reads beyond storage") {
    char bases[]={0,1,5,0,1,5};
    const CompareUintAndSuffixesMemcmp comp{bases,10000,sizeof(bases)};
    uint64 a[]={4,0},b[]={4,3},copy[]={4,0};
    CHECK(compareMemcmp(comp,a,b)<0);
    CHECK(compareMemcmp(comp,b,a)>0);
    CHECK(compareMemcmp(comp,a,copy)==0);
    CHECK(compareMemcmp(comp,a,a)==0);
}

namespace {
// Low-entropy sequence (few letters, repeated blocks) so many suffixes share long
// prefixes and the tie-breaks are exercised; ends with the separator 5.
vector<char> randomSequence(mt19937_64 &rng, uint64_t n, int letters) {
    vector<char> seq(n);
    uniform_int_distribution<int> letter(0,letters-1), sep(0,15), copyLen(4,40);
    for (uint64_t ii=0; ii<n; ii++) {
        seq[ii]= sep(rng)==0 ? 5 : (char) letter(rng);
        if (ii>=64 && sep(rng)==1) {//repeat an earlier block
            uint64_t len=copyLen(rng), from=rng()%(ii-40);
            for (uint64_t jj=0; jj<len && ii<n; jj++,ii++) seq[ii]=seq[from+jj];
            ii--;
        };
    };
    seq[n-1]=5;
    return seq;
}

// nInd distinct suffix starts in [0,idxN) with keys from a small range.
vector<uint64> randomPairs(mt19937_64 &rng, uint64_t nInd, uint64_t idxN, uint64_t keyN) {
    vector<uint64> idx(idxN);
    for (uint64_t ii=0; ii<idxN; ii++) idx[ii]=ii;
    shuffle(idx.begin(), idx.end(), rng);
    vector<uint64> pairs(2*nInd);
    for (uint64_t ii=0; ii<nInd; ii++) {
        pairs[2*ii]=rng()%keyN;
        pairs[2*ii+1]=idx[ii];
    };
    return pairs;
}
} // namespace

TEST_CASE("Context comparators sort {key, suffix} pairs exactly like the former qsort") {
    mt19937_64 rng(20261104);
    for (int iter=0; iter<300; iter++) {
        const int letters=1+iter%4;
        const uint64 n=200+rng()%3000;
        vector<char> seq=randomSequence(rng, n, letters);
        const uint64 nInd=1+rng()%min<uint64>(n-1, 2000);
        const uint64 keyN=1+rng()%(iter%3==0 ? 2 : 50);
        const vector<uint64> pairs=randomPairs(rng, nInd, n-1, keyN);

        vector<uint64> oldOrder=pairs, newOrder=pairs;
        legacy::uintG=seq.data();
        qsort(oldOrder.data(), nInd, 2*sizeof(uint64), legacy::compareUintAndSuffixes);
        sortUintAndSuffixes(newOrder.data(), nInd, CompareUintAndSuffixes{seq.data()});
        CHECK(oldOrder==newOrder);

        const uint64 L=(iter%5==0) ? 10000 : 1+rng()%64;
        uint64 maxIdx=0;
        for (uint64 ii=0; ii<nInd; ii++) maxIdx=max(maxIdx, pairs[2*ii+1]);
        const uint64 N=n-rng()%(n-maxIdx); //N>maxIdx, so some suffixes are cut by N
        oldOrder=pairs; newOrder=pairs;
        legacy::memcmpG=seq.data(); legacy::memcmpL=L; legacy::memcmpN=N;
        qsort(oldOrder.data(), nInd, 2*sizeof(uint64), legacy::compareUintAndSuffixesMemcmp);
        sortUintAndSuffixes(newOrder.data(), nInd, CompareUintAndSuffixesMemcmp{seq.data(), L, N});
        CHECK(oldOrder==newOrder);
    };
}

TEST_CASE("Context suffix-array comparator sorts exactly like the former qsort") {
    mt19937_64 rng(4110626);
    for (int iter=0; iter<300; iter++) {
        const int letters=1+iter%4;
        const uint64 L=1+rng()%6; //words compared
        const uint64 pad=8*L+8;   //bytes read before the suffix start
        const uint64 n=pad+200+rng()%3000;
        vector<char> seq=randomSequence(rng, n, letters);
        const char *G=seq.data();

        const uint64 nInd=1+rng()%min<uint64>(n-pad, 2000);
        vector<uint64> idx(n-pad);
        for (uint64_t ii=0; ii<idx.size(); ii++) idx[ii]=pad+ii;
        shuffle(idx.begin(), idx.end(), rng);
        idx.resize(nInd);

        for (uint64 wordStart=0; wordStart<=1; wordStart++) {
            vector<uint64> oldOrder=idx, newOrder=idx;
            legacy::globalG=const_cast<char*>(G); legacy::globalL=L;
            qsort(oldOrder.data(), nInd, sizeof(uint64),
                  wordStart==0 ? legacy::compareSuffixes : legacy::compareSuffixesSkipFirstWord);
            std::sort(newOrder.begin(), newOrder.end(), CompareSuffixes{G, L, wordStart});
            CHECK(oldOrder==newOrder);
        };
    };
}
