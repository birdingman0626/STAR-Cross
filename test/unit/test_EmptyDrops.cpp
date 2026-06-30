// test_EmptyDrops.cpp
// Tests for the EmptyDrops p-value counting logic from source/SoloFeature_emptyDrops_CR.cpp.
//
// The p-value formula in that file is:
//   nLowerP = lower_bound(sorted_sim.begin(), sorted_sim.end(), obsLogProb) - sorted_sim.begin()
//   p = (1 + nLowerP) / (1 + nSim)
//
// We extract this as a standalone function `emptyDropsPvalue` and verify it against
// a linear-scan reference for correctness, and check boundary/edge cases.
// No STAR headers or runtime are required.

#include "doctest/doctest.h"
#include <vector>
#include <algorithm>
#include <cstdint>
#include <cmath>

// Replicated from SoloFeature_emptyDrops_CR.cpp.
// sorted: simulation log-probabilities sorted ascending.
// obsLogProb: observed log-probability for one candidate cell.
// nSim: total number of simulations (== sorted.size()).
// Returns p-value in (0,1].
static double emptyDropsPvalue(const std::vector<double> &sorted,
                                double obsLogProb,
                                uint64_t nSim)
{
    uint32_t nLowerP = static_cast<uint32_t>(
        std::lower_bound(sorted.begin(), sorted.end(), obsLogProb) - sorted.begin());
    return static_cast<double>(1 + nLowerP) / static_cast<double>(1 + nSim);
}

// Linear-scan reference: count how many simulation values are strictly less than obs.
static uint32_t linearCountLower(const std::vector<double> &sorted, double obs) {
    uint32_t n = 0;
    for (double v : sorted)
        if (v < obs) ++n;
    return n;
}

TEST_CASE("EmptyDrops pvalue - empty simulations: p = 1/(1+nSim) with nSim=0") {
    // 0 simulations: nLowerP=0, p = 1/1 = 1.0
    double p = emptyDropsPvalue({}, -10.0, 0);
    CHECK(p == doctest::Approx(1.0));
}

TEST_CASE("EmptyDrops pvalue - single candidate below all simulations: p = 1/(1+nSim)") {
    // obs is less than every sim value → nLowerP = 0 → p = 1/(1+N)
    std::vector<double> sims = {-5.0, -4.0, -3.0, -2.0, -1.0};
    uint64_t nSim = sims.size();
    double p = emptyDropsPvalue(sims, -10.0, nSim);
    double expected = 1.0 / (1.0 + nSim);
    CHECK(p == doctest::Approx(expected));
    CHECK(p < 1.0 / nSim); // very small p-value
}

TEST_CASE("EmptyDrops pvalue - single candidate above all simulations: p = (1+nSim)/(1+nSim) = 1") {
    // obs is greater than every sim value → nLowerP = nSim → p = (1+nSim)/(1+nSim) = 1.0
    std::vector<double> sims = {-5.0, -4.0, -3.0, -2.0, -1.0};
    uint64_t nSim = sims.size();
    double p = emptyDropsPvalue(sims, 0.0, nSim);
    CHECK(p == doctest::Approx(1.0));
}

TEST_CASE("EmptyDrops pvalue - obs equal to a simulation value: uses lower_bound (not less)") {
    // lower_bound finds first element >= obs, so values == obs are NOT counted as lower.
    // obs = -3.0 (present in sims at index 2); nLowerP = 2 (values -5, -4 are < -3)
    std::vector<double> sims = {-5.0, -4.0, -3.0, -2.0, -1.0};
    uint64_t nSim = sims.size();
    double p = emptyDropsPvalue(sims, -3.0, nSim);
    double expected = (1.0 + 2) / (1.0 + nSim); // nLowerP=2
    CHECK(p == doctest::Approx(expected));
}

TEST_CASE("EmptyDrops pvalue - binary search matches linear count for small synthetic data") {
    // Verify binary-search and linear-count agree for several obs values.
    std::vector<double> sims = {-9.0, -7.0, -5.0, -3.0, -1.0, 0.0, 2.0, 4.0};
    uint64_t nSim = sims.size();
    // sims is already sorted ascending

    std::vector<double> obsValues = {-10.0, -8.0, -5.0, -4.0, -0.5, 0.0, 3.0, 5.0};
    for (double obs : obsValues) {
        uint32_t linear = linearCountLower(sims, obs);
        double pBinary = emptyDropsPvalue(sims, obs, nSim);
        double pLinear = static_cast<double>(1 + linear) / static_cast<double>(1 + nSim);
        CHECK(pBinary == doctest::Approx(pLinear));
    }
}

TEST_CASE("EmptyDrops pvalue - p-value is bounded in open-closed interval") {
    std::vector<double> sims = {-8.0, -6.0, -4.0, -2.0, 0.0};
    uint64_t nSim = sims.size();

    std::vector<double> obsValues = {-100.0, -7.0, -4.0, 0.0, 1.0};
    for (double obs : obsValues) {
        double p = emptyDropsPvalue(sims, obs, nSim);
        CHECK(p > 0.0);
        CHECK(p <= 1.0);
    }
}

TEST_CASE("EmptyDrops pvalue - larger nSim with known position") {
    // Build 100 sim values: -100, -99, ..., -1
    std::vector<double> sims;
    sims.reserve(100);
    for (int i = 100; i >= 1; --i) sims.push_back(static_cast<double>(-i));
    // sims is now [-100, -99, ..., -1], sorted ascending
    uint64_t nSim = sims.size();

    // obs = -50: 50 values are < -50 (i.e., -100..-51), so nLowerP = 50
    double p = emptyDropsPvalue(sims, -50.0, nSim);
    double expected = (1.0 + 50) / (1.0 + nSim);
    CHECK(p == doctest::Approx(expected));
}

TEST_CASE("EmptyDrops pvalue - multiple obs against same sim set (vectorised use)") {
    std::vector<double> sims = {-10.0, -8.0, -6.0, -4.0, -2.0};
    uint64_t nSim = sims.size();

    // For obs=-11: nLowerP=0 → p=1/6
    CHECK(emptyDropsPvalue(sims, -11.0, nSim) == doctest::Approx(1.0 / 6.0));
    // For obs=-7: nLowerP=2 (values -10, -8) → p=3/6=0.5
    CHECK(emptyDropsPvalue(sims, -7.0, nSim) == doctest::Approx(3.0 / 6.0));
    // For obs=0: all 5 values are lower → nLowerP=5 → p=6/6=1.0
    CHECK(emptyDropsPvalue(sims, 0.0, nSim) == doctest::Approx(1.0));
}
