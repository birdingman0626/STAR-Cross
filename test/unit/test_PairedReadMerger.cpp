#include "doctest/doctest.h"
#include "PairedReadMerger.h"
#include "SequenceFuns.h"
#include <random>
#include <stdexcept>

namespace {
// Frozen pre-extraction algorithm, independent of the component under test.
PairedReadMergeResult legacy(std::vector<char>& bases, uint l0, uint l1, uint minimum, double mismatch) {
    uint s1=localSearchNisMM(bases.data(),l0,bases.data()+l0+1,l1,mismatch);
    uint s0=localSearchNisMM(bases.data()+l0+1,l1,bases.data(),l0,mismatch);
    uint o1=min(l1,l0-s1),o0=min(l0,l1-s0),total=l0+l1+1;
    PairedReadMergeResult r{max(o0,o1),{0,0},total};
    if (r.overlap<minimum) {r.overlap=0;return r;}
    if(o1>=o0) {
        r.mateStart[1]=s1;
        if(o1<l1) memmove(bases.data()+l0,bases.data()+l0+1+o1,l1-o1);
    } else {
        r.mateStart[0]=s0;
        memmove(bases.data()+total,bases.data(),l0);
        memmove(bases.data(),bases.data()+l0+1,l1);
        if(o0<l0) memmove(bases.data()+l1,bases.data()+total+o0,l0-o0);
    }
    r.length=total-r.overlap-1;return r;
}
}

TEST_CASE("Paired merger matches legacy buffer bytes and overlap decisions") {
    std::mt19937 rng(8126);
    bool forward=false,reverse=false,none=false;
    for(unsigned trial=0;trial<1000;++trial) {
        uint l0=1+rng()%30,l1=1+rng()%30,minimum=1+rng()%12;
        std::vector<char> expected(128,5);
        for(uint i=0;i<l0+l1+1;++i) expected[i]=rng()%5;
        auto actual=expected;
        double mismatch=(trial%3)*0.1;
        auto old=legacy(expected,l0,l1,minimum,mismatch);
        auto result=mergePairedRead(actual.data(),actual.size(),l0,l1,minimum,mismatch);
        CHECK(result.overlap==old.overlap); CHECK(result.length==old.length);
        CHECK(result.mateStart[0]==old.mateStart[0]); CHECK(result.mateStart[1]==old.mateStart[1]);
        CHECK(actual==expected);
        none|=!result.overlap;
        forward|=result.overlap && !result.mateStart[0];
        reverse|=result.overlap && result.mateStart[0];
    }
    CHECK(forward); CHECK(reverse); CHECK(none);
}

TEST_CASE("Paired merger rejects insufficient scratch before mutation") {
    // Mate 0 is the suffix of mate 1: the reverse-direction branch needs scratch.
    std::vector<char> bases{2,3,5,0,1,2,3};
    const auto saved=bases;
    CHECK_THROWS_AS(mergePairedRead(bases.data(),bases.size(),2,4,2,0),std::invalid_argument);
    CHECK(bases==saved);
    CHECK_THROWS_AS(mergePairedRead(bases.data(),6,2,4,2,0),std::invalid_argument);
    CHECK_THROWS_AS(mergePairedRead(nullptr,0,2,4,2,0),std::invalid_argument);
}
