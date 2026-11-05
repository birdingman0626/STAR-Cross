#include <doctest/doctest.h>
#include "BAMlimits.h"
#include "htslib/htslib/hts.h"
#include <algorithm>
#include <cstdint>

TEST_CASE("BAM reference-length limit matches the int32 fields of the BAM format") {
    CHECK(BAM_MAX_REFERENCE_LENGTH == 2147483647ULL);
    CHECK(firstOversizedBAMreference({}) == 0);
    CHECK(firstOversizedBAMreference({1000, 2147483647ULL}) == 2); // longest valid reference fits
    CHECK(firstOversizedBAMreference({1000, 2147483648ULL, 5}) == 1); // one past the limit would wrap to negative
    CHECK(firstOversizedBAMreference({4294967296ULL + 10}) == 0); // would wrap to a small positive length
}

TEST_CASE("Long BAM CIGAR gaps split into consecutive operations of the same type") {
    const unsigned int N = 3; // BAM_CIGAR_N
    unsigned int packed[16] = {};
    auto len = [&](int i) { return packed[i] >> 4; };
    auto op  = [&](int i) { return packed[i] & 0xF; };

    CHECK(appendBAMcigarOp(packed, 0, 1000, N) == 1); // short gaps are unchanged
    CHECK(packed[0] == (1000u<<4 | N));

    CHECK(appendBAMcigarOp(packed, 0, BAM_CIGAR_MAX_OP_LENGTH, N) == 1); // longest single op
    CHECK(len(0) == BAM_CIGAR_MAX_OP_LENGTH);

    const unsigned long long gap = BAM_CIGAR_MAX_OP_LENGTH + 1000ULL; // 268436455N -> 268435455N1000N
    REQUIRE(appendBAMcigarOp(packed, 2, gap, N) == 4);
    CHECK(len(2) == BAM_CIGAR_MAX_OP_LENGTH); CHECK(op(2) == N);
    CHECK(len(3) == 1000); CHECK(op(3) == N);

    REQUIRE(appendBAMcigarOp(packed, 0, BAM_MAX_REFERENCE_LENGTH, N) == 9); // longest possible gap adds 8 ops
    unsigned long long total = 0;
    for (int i = 0; i < 9; i++) { total += len(i); CHECK(op(i) == N); }
    CHECK(total == BAM_MAX_REFERENCE_LENGTH);
}

TEST_CASE("BAM bin field matches what htslib writes, including past the 2^29 BAI limit") {
    CHECK(bamBinField(-1, 0) == 4680); // unmapped records
    const long long starts[] = {0, 16383, 16384, (1LL<<29) - 200, (1LL<<29) - 1, 1LL<<29, (1LL<<29) + 70000,
                                1LL<<30, 997000000, 1500000000, 2147483647LL - 300};
    const long long spans[] = {1, 100, 20000, 300000, 70000000};
    for (long long beg : starts) {
        for (long long span : spans) {
            const long long end = std::min(beg + span, 2147483647LL);
            CAPTURE(beg); CAPTURE(end);
            // bam1_core_t::bin is uint16_t: this is the value htslib's bam_write1 stores
            const uint16_t hts = static_cast<uint16_t>(hts_reg2bin(beg, end, 14, 5));
            CHECK(bamBinField(static_cast<int>(beg), static_cast<int>(end)) == hts);
            CHECK(bamBinField(static_cast<int>(beg), static_cast<int>(end)) <= 0xFFFFu);
        }
    }
}
