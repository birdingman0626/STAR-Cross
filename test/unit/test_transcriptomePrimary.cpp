#include "doctest/doctest.h"
#include "transcriptomePrimary.h"
#include <vector>

TEST_CASE("Transcriptome primary by read index is deterministic, in range and spread") {
    CHECK(transcriptomePrimaryByReadIndex(777, 5, 0) == 0);
    CHECK(transcriptomePrimaryByReadIndex(777, 5, 1) == 0);
    for (uint64_t n : {2ULL, 3ULL, 7ULL, 100ULL}) {
        std::vector<int> hits(n, 0);
        for (uint64_t read = 0; read < 20000; ++read) {
            uint64_t pick = transcriptomePrimaryByReadIndex(777, read, n);
            REQUIRE(pick < n);
            CHECK(pick == transcriptomePrimaryByReadIndex(777, read, n));
            ++hits[pick];
        }
        for (int h : hits) // every alignment gets roughly its share
            CHECK(h > 20000 / (int)n / 2);
    }
    int differ = 0; // the seed changes the choice
    for (uint64_t read = 0; read < 1000; ++read)
        differ += transcriptomePrimaryByReadIndex(777, read, 4) != transcriptomePrimaryByReadIndex(778, read, 4);
    CHECK(differ > 500);
}
