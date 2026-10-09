#include "doctest/doctest.h"
#include "funCompareUintAndSuffixes.h"
#include "funCompareUintAndSuffixesMemcmp.h"

TEST_CASE("Inserted suffix order terminates at separator and is reflexive") {
    char bases[]={0,1,5,0,1,5,0,2,5};
    g_funCompareUintAndSuffixes_G=bases;
    uint64_t a[]={4,0},b[]={4,3},c[]={4,6},lower[]={3,6};
    CHECK(funCompareUintAndSuffixes(a,a)==0);
    CHECK(funCompareUintAndSuffixes(a,b)<0);
    CHECK(funCompareUintAndSuffixes(b,a)>0);
    CHECK(funCompareUintAndSuffixes(b,c)<0);
    CHECK(funCompareUintAndSuffixes(c,b)>0);
    CHECK(funCompareUintAndSuffixes(lower,a)<0);
    uint64_t copy[]={4,0};
    CHECK(funCompareUintAndSuffixes(a,copy)==0);
    g_funCompareUintAndSuffixes_G=nullptr;
}

TEST_CASE("Finite inserted suffix comparison never reads beyond storage") {
    char bases[]={0,1,5,0,1,5};
    g_funCompareUintAndSuffixesMemcmp_G=bases;
    g_funCompareUintAndSuffixesMemcmp_N=sizeof(bases);
    g_funCompareUintAndSuffixesMemcmp_L=10000;
    uint64_t a[]={4,0},b[]={4,3},copy[]={4,0};
    CHECK(funCompareUintAndSuffixesMemcmp(a,b)<0);
    CHECK(funCompareUintAndSuffixesMemcmp(b,a)>0);
    CHECK(funCompareUintAndSuffixesMemcmp(a,copy)==0);
    CHECK(funCompareUintAndSuffixesMemcmp(a,a)==0);
    g_funCompareUintAndSuffixesMemcmp_G=nullptr;
}
