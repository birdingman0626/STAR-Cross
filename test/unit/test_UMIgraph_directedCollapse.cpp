// test_UMIgraph_directedCollapse.cpp
// Tests for the directional UMI collapse logic in
// source/SoloFeature_collapseUMI_Graph.cpp::process1MMpair().
//
// The directional collapse rule (Smith, Heger & Sudbery, Genome Research 2017):
//   Given two UMIs iu and iuu that are 1-mismatch neighbours:
//     If count(iu) > 2*count(iuu) - 1  AND iuu not yet marked absorbed:
//         mark iuu as absorbed (set bit 31 of count field), decrement nU2.
//     Else if count(iuu) > 2*count(iu) - 1  AND iu not yet absorbed:
//         mark iu as absorbed, decrement nU2.
//
// The UMI array layout (stride=3):
//   slot+0: UMI value
//   slot+1: count (lower 31 bits); bit 31 = absorbed flag
//   slot+2: graph color (def_MarkNoColor = (uint32)-1 means uncolored)
//
// We replicate the directional portion of process1MMpair as a standalone
// function `applyDirectionalCollapse` so it can be tested without pulling in
// SoloFeature or full STAR headers.

#include "doctest/doctest.h"
#include <vector>
#include <cstdint>
#include <algorithm>

static const uint32_t BIT_TOP      = 1u << 31;
static const uint32_t BIT_TOP_MASK = ~BIT_TOP;
static const uint32_t MARK_NO_COLOR = static_cast<uint32_t>(-1);

// Mirrors the directional-collapse block inside process1MMpair.
// Relationship: extracted from SoloFeature_collapseUMI_Graph.cpp, directional
// portion only (lines 107-113 in the original file).
//
// umiArr: flat array with stride 3.  iu, iuu: byte offsets of the two entries.
// nU2: current directional count; decremented for each absorbed UMI.
static void applyDirectionalCollapse(uint32_t *umiArr,
                                     uint32_t iu, uint32_t iuu,
                                     uint32_t &nU2)
{
    uint32_t countIu  = umiArr[iu  + 1] & BIT_TOP_MASK;
    uint32_t countIuu = umiArr[iuu + 1] & BIT_TOP_MASK;

    if ((umiArr[iuu + 1] & BIT_TOP) == 0 && countIu > 2u * countIuu - 1u) {
        umiArr[iuu + 1] |= BIT_TOP; // absorb iuu into iu's cluster
        --nU2;
    } else if ((umiArr[iu + 1] & BIT_TOP) == 0 && countIuu > 2u * countIu - 1u) {
        umiArr[iu + 1] |= BIT_TOP; // absorb iu into iuu's cluster
        --nU2;
    }
}

// Convenience: build a 3-slot UMI record at position idx*3 in arr.
static void setUMI(std::vector<uint32_t> &arr, uint32_t idx,
                   uint32_t umiVal, uint32_t count, uint32_t color = MARK_NO_COLOR)
{
    arr[idx * 3 + 0] = umiVal;
    arr[idx * 3 + 1] = count & BIT_TOP_MASK; // clear absorbed flag
    arr[idx * 3 + 2] = color;
}

static bool isAbsorbed(const std::vector<uint32_t> &arr, uint32_t idx) {
    return (arr[idx * 3 + 1] & BIT_TOP) != 0;
}

static uint32_t rawCount(const std::vector<uint32_t> &arr, uint32_t idx) {
    return arr[idx * 3 + 1] & BIT_TOP_MASK;
}

// ---- Tests ---------------------------------------------------------------

TEST_CASE("DirectedCollapse - A absorbs B when count(A) > 2*count(B)-1") {
    // A has count 10, B has count 3: 10 > 2*3-1=5 → B gets absorbed
    std::vector<uint32_t> arr(6);
    setUMI(arr, 0, 0xAA, 10); // A at offset 0
    setUMI(arr, 1, 0xAB, 3);  // B at offset 3

    uint32_t nU2 = 2;
    applyDirectionalCollapse(arr.data(), 0, 3, nU2);

    CHECK_FALSE(isAbsorbed(arr, 0)); // A survives
    CHECK(isAbsorbed(arr, 1));       // B absorbed
    CHECK(nU2 == 1);
}

TEST_CASE("DirectedCollapse - B absorbs A when count(B) > 2*count(A)-1") {
    // B has count 10, A has count 3: 10 > 5 → A gets absorbed
    std::vector<uint32_t> arr(6);
    setUMI(arr, 0, 0xAA, 3);  // A at offset 0
    setUMI(arr, 1, 0xAB, 10); // B at offset 3

    uint32_t nU2 = 2;
    applyDirectionalCollapse(arr.data(), 0, 3, nU2);

    CHECK(isAbsorbed(arr, 0));       // A absorbed
    CHECK_FALSE(isAbsorbed(arr, 1)); // B survives
    CHECK(nU2 == 1);
}

TEST_CASE("DirectedCollapse - equal counts: neither absorbed (2*n-1 == 2n-1, not > threshold)") {
    // count(A)=5, count(B)=5: 5 > 2*5-1=9 is false → no absorption
    std::vector<uint32_t> arr(6);
    setUMI(arr, 0, 0xAA, 5);
    setUMI(arr, 1, 0xAB, 5);

    uint32_t nU2 = 2;
    applyDirectionalCollapse(arr.data(), 0, 3, nU2);

    CHECK_FALSE(isAbsorbed(arr, 0));
    CHECK_FALSE(isAbsorbed(arr, 1));
    CHECK(nU2 == 2); // unchanged
}

TEST_CASE("DirectedCollapse - count 1 vs count 1: no absorption (1 > 1 is false)") {
    std::vector<uint32_t> arr(6);
    setUMI(arr, 0, 0xAA, 1);
    setUMI(arr, 1, 0xAB, 1);

    uint32_t nU2 = 2;
    applyDirectionalCollapse(arr.data(), 0, 3, nU2);

    CHECK_FALSE(isAbsorbed(arr, 0));
    CHECK_FALSE(isAbsorbed(arr, 1));
    CHECK(nU2 == 2);
}

TEST_CASE("DirectedCollapse - already absorbed node is NOT absorbed again") {
    // B is already marked absorbed (bit 31 set). A has high count. nU2 must not decrease again.
    std::vector<uint32_t> arr(6);
    setUMI(arr, 0, 0xAA, 10);
    setUMI(arr, 1, 0xAB, 3);
    arr[1 * 3 + 1] |= BIT_TOP; // pre-mark B as absorbed

    uint32_t nU2 = 1; // already reflected
    applyDirectionalCollapse(arr.data(), 0, 3, nU2);

    // BIT_TOP was already set on B; (umiArr[iuu+1] & BIT_TOP)==0 is false, so no-op
    CHECK(nU2 == 1); // unchanged
}

TEST_CASE("DirectedCollapse - three-node chain with decreasing counts: middle then tail absorbed") {
    // Nodes: A(count=20) → B(count=5) → C(count=1)
    // Pair (A,B): countA=20 > 2*5-1=9 → B absorbed, nU2 goes from 3 to 2.
    // Pair (B,C): iu=B (offset 3), iuu=C (offset 6).
    //   Branch 1 checks: C not yet absorbed AND countB (raw=5) > 2*countC-1=1 → 5>1 TRUE
    //   → C is absorbed, nU2 decrements to 1.
    //   (B being absorbed already does NOT block the count-threshold check for its neighbour.)
    std::vector<uint32_t> arr(9);
    setUMI(arr, 0, 0xA0, 20); // A
    setUMI(arr, 1, 0xA1, 5);  // B
    setUMI(arr, 2, 0xA2, 1);  // C

    uint32_t nU2 = 3;

    // Process pair (A,B)
    applyDirectionalCollapse(arr.data(), 0, 3, nU2);
    CHECK(isAbsorbed(arr, 1)); // B absorbed
    CHECK(nU2 == 2);

    // Process pair (B,C): B's raw count is 5, C's count is 1; 5 > 2*1-1=1 → C absorbed
    applyDirectionalCollapse(arr.data(), 3, 6, nU2);
    CHECK(isAbsorbed(arr, 2)); // C absorbed via B's (raw) count advantage
    CHECK(nU2 == 1);
}

TEST_CASE("DirectedCollapse - isolated node (no pair call): unchanged") {
    // An isolated node is never passed to applyDirectionalCollapse.
    // This test simply verifies the initial state is intact.
    std::vector<uint32_t> arr(3);
    setUMI(arr, 0, 0xCC, 7);

    CHECK_FALSE(isAbsorbed(arr, 0));
    CHECK(rawCount(arr, 0) == 7);
}

TEST_CASE("DirectedCollapse - boundary: count(A)=2, count(B)=1 exactly at threshold") {
    // count(A)=2: 2 > 2*1-1=1 → true → B absorbed
    std::vector<uint32_t> arr(6);
    setUMI(arr, 0, 0xAA, 2);
    setUMI(arr, 1, 0xAB, 1);

    uint32_t nU2 = 2;
    applyDirectionalCollapse(arr.data(), 0, 3, nU2);

    CHECK_FALSE(isAbsorbed(arr, 0));
    CHECK(isAbsorbed(arr, 1));
    CHECK(nU2 == 1);
}

TEST_CASE("DirectedCollapse - rawCount preserved after absorption flag set") {
    // Ensure the count bits are not corrupted when BIT_TOP is set.
    std::vector<uint32_t> arr(6);
    setUMI(arr, 0, 0xAA, 10);
    setUMI(arr, 1, 0xAB, 3);

    uint32_t nU2 = 2;
    applyDirectionalCollapse(arr.data(), 0, 3, nU2);

    CHECK(rawCount(arr, 0) == 10); // A count intact
    CHECK(rawCount(arr, 1) == 3);  // B count intact (only flag bit set)
    CHECK(isAbsorbed(arr, 1));
}
