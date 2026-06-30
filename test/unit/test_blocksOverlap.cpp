// test_blocksOverlap.cpp
// Tests for the blocksOverlap() logic from source/blocksOverlap.cpp.
//
// blocksOverlap(t1, t2) iterates exon pairs between two Transcript objects and
// accumulates overlap length. The condition for counting overlap is:
//   1. The read-space intervals [rs1, re1) and [rs2, re2) must overlap (no strict-right check).
//   2. The genomic offsets must match: gs1 - rs1 == gs2 - rs2 (same diagonal).
// If both conditions hold, overlap += min(re1,re2) - max(rs1,rs2).
//
// Because Transcript has heavy STAR dependencies, we replicate the core logic here
// as a standalone function `blocksOverlapCalc` with a minimal exon descriptor, then
// verify the replicated logic matches the expected semantics.  Any future change to
// blocksOverlap.cpp should be reflected here.

#include "doctest/doctest.h"
#include <vector>
#include <algorithm>
#include <cstdint>

// Minimal exon descriptor mirroring the EX_R/EX_G/EX_L layout used by Transcript.
struct Exon {
    uint64_t r; // read start (EX_R)
    uint64_t g; // genome start (EX_G)
    uint64_t l; // length (EX_L)
};

// Replicated core logic from source/blocksOverlap.cpp.
// Relationship: identical algorithm; uses Exon struct instead of Transcript.
static unsigned long long blocksOverlapCalc(const std::vector<Exon> &ex1,
                                             const std::vector<Exon> &ex2)
{
    size_t i1 = 0, i2 = 0;
    unsigned long long nOverlap = 0;

    while (i1 < ex1.size() && i2 < ex2.size()) {
        auto rs1 = ex1[i1].r;
        auto rs2 = ex2[i2].r;
        auto re1 = ex1[i1].r + ex1[i1].l; // 1st base after end
        auto re2 = ex2[i2].r + ex2[i2].l;
        auto gs1 = ex1[i1].g;
        auto gs2 = ex2[i2].g;

        if (rs1 >= re2) {
            ++i2;
        } else if (rs2 >= re1) {
            ++i1;
        } else if (gs1 - rs1 != gs2 - rs2) { // different diagonal: no overlap
            if (re1 >= re2) ++i2;
            if (re2 >= re1) ++i1;
        } else { // overlap
            nOverlap += std::min(re1, re2) - std::max(rs1, rs2);
            if (re1 >= re2) ++i2;
            if (re2 >= re1) ++i1;
        }
    }
    return nOverlap;
}

// ---- helper: build a single exon on the same diagonal (g = r + offset) ----
static Exon makeExon(uint64_t r, uint64_t l, uint64_t offset = 0) {
    return {r, r + offset, l};
}

TEST_CASE("blocksOverlap - empty exon lists return 0") {
    CHECK(blocksOverlapCalc({}, {}) == 0);
    CHECK(blocksOverlapCalc({makeExon(0, 10)}, {}) == 0);
    CHECK(blocksOverlapCalc({}, {makeExon(0, 10)}) == 0);
}

TEST_CASE("blocksOverlap - non-overlapping: t1 entirely before t2") {
    // t1: [0,10), t2: [20,30) — gap of 10 bases
    CHECK(blocksOverlapCalc({makeExon(0, 10)}, {makeExon(20, 10)}) == 0);
}

TEST_CASE("blocksOverlap - non-overlapping: t1 entirely after t2") {
    CHECK(blocksOverlapCalc({makeExon(20, 10)}, {makeExon(0, 10)}) == 0);
}

TEST_CASE("blocksOverlap - touching (adjacent, not overlapping)") {
    // t1: [0,10), t2: [10,20) — touching at read-position 10, length 0 overlap
    // rs1=0, re1=10, rs2=10 → rs2 >= re1 → no overlap
    CHECK(blocksOverlapCalc({makeExon(0, 10)}, {makeExon(10, 10)}) == 0);
}

TEST_CASE("blocksOverlap - identical blocks fully overlapping") {
    // t1: [0,10), t2: [0,10) on same diagonal → overlap = 10
    CHECK(blocksOverlapCalc({makeExon(0, 10)}, {makeExon(0, 10)}) == 10);
}

TEST_CASE("blocksOverlap - one block inside the other") {
    // t1: [0,20), t2: [5,10) — same diagonal, t2 entirely inside t1
    CHECK(blocksOverlapCalc({makeExon(0, 20)}, {makeExon(5, 5)}) == 5);
    // reversed
    CHECK(blocksOverlapCalc({makeExon(5, 5)}, {makeExon(0, 20)}) == 5);
}

TEST_CASE("blocksOverlap - partial overlap on right side") {
    // t1: [0,10), t2: [5,15) → overlap = [5,10) = 5
    CHECK(blocksOverlapCalc({makeExon(0, 10)}, {makeExon(5, 10)}) == 5);
}

TEST_CASE("blocksOverlap - partial overlap on left side") {
    // t1: [5,15), t2: [0,10) → overlap = [5,10) = 5
    CHECK(blocksOverlapCalc({makeExon(5, 10)}, {makeExon(0, 10)}) == 5);
}

TEST_CASE("blocksOverlap - zero-length block produces no overlap") {
    // A zero-length exon [5,5) cannot contribute any overlap
    CHECK(blocksOverlapCalc({makeExon(5, 0)}, {makeExon(0, 10)}) == 0);
    CHECK(blocksOverlapCalc({makeExon(0, 10)}, {makeExon(5, 0)}) == 0);
}

TEST_CASE("blocksOverlap - different diagonal: no overlap even if read intervals overlap") {
    // Same read positions [0,10) but gs1-rs1 != gs2-rs2 → diagonal check fails
    Exon e1{0, 100, 10}; // diagonal = 100
    Exon e2{0, 200, 10}; // diagonal = 200
    CHECK(blocksOverlapCalc({e1}, {e2}) == 0);
}

TEST_CASE("blocksOverlap - two exons each, fully matching") {
    // t1: [0,5) and [10,15), t2: same — total overlap = 5 + 5 = 10
    std::vector<Exon> t1 = {makeExon(0, 5), makeExon(10, 5)};
    std::vector<Exon> t2 = {makeExon(0, 5), makeExon(10, 5)};
    CHECK(blocksOverlapCalc(t1, t2) == 10);
}

TEST_CASE("blocksOverlap - two exons, one matches one doesn't") {
    // t1: [0,5) [10,15); t2: [0,5) [20,25) — first exon overlaps, second doesn't
    std::vector<Exon> t1 = {makeExon(0, 5), makeExon(10, 5)};
    std::vector<Exon> t2 = {makeExon(0, 5), makeExon(20, 5)};
    CHECK(blocksOverlapCalc(t1, t2) == 5);
}

TEST_CASE("blocksOverlap - many exons, interleaved with gaps") {
    // t1: [0,4) [8,12) [16,20)
    // t2: [2,6) [10,14) [18,22)
    // overlaps: [2,4)=2, [10,12)=2, [18,20)=2 → total=6
    std::vector<Exon> t1 = {makeExon(0, 4), makeExon(8, 4), makeExon(16, 4)};
    std::vector<Exon> t2 = {makeExon(2, 4), makeExon(10, 4), makeExon(18, 4)};
    CHECK(blocksOverlapCalc(t1, t2) == 6);
}
