#include <doctest/doctest.h>
#include "BAMlimits.h"

TEST_CASE("BAM reference-length limit matches the int32 fields of the BAM format") {
    CHECK(BAM_MAX_REFERENCE_LENGTH == 2147483647ULL);
    CHECK(firstOversizedBAMreference({}) == 0);
    CHECK(firstOversizedBAMreference({1000, 2147483647ULL}) == 2); // longest valid reference fits
    CHECK(firstOversizedBAMreference({1000, 2147483648ULL, 5}) == 1); // one past the limit would wrap to negative
    CHECK(firstOversizedBAMreference({4294967296ULL + 10}) == 0); // would wrap to a small positive length
}
