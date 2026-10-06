#include "doctest/doctest.h"
#include "samAux.h"

TEST_CASE("SAM auxiliary parser preserves all B subtypes and numeric boundaries") {
    const char *tags[] = {"XA:B:c,-128,127", "XA:B:C,0,255", "XA:B:s,-32768,32767",
        "XA:B:S,0,65535", "XA:B:i,-2147483648,2147483647",
        "XA:B:I,0,4294967295", "XA:B:f,-1.25,2.5"};
    for (const auto tag : tags) {
        const auto bytes = samAuxBytes(tag, 100);
        REQUIRE(bytes.size() >= 10);
        CHECK(bytes[2] == 'B');
        CHECK(bytes[3] == tag[5]);
        CHECK(bytes[4] == 2); // two elements, little-endian count
        CHECK(bytes[5] == 0);
        CHECK(bytes[6] == 0);
        CHECK(bytes[7] == 0);
        bam1_t *record = bam_init1();
        REQUIRE(record != nullptr);
        REQUIRE(bam_set1(record, 1, "a", 4, -1, 0, 0, 0, nullptr, -1, 0, 0,
                         0, nullptr, nullptr, 0) >= 0);
        REQUIRE(bam_aux_append(record, "XA", 'B', bytes.size()-3, bytes.data()+3) == 0);
        auto *aux = bam_aux_get(record, "XA");
        REQUIRE(aux != nullptr);
        CHECK(bam_auxB_len(aux) == 2);
        if (tag[5] == 'f') {
            CHECK(bam_auxB2f(aux, 0) == doctest::Approx(-1.25));
            CHECK(bam_auxB2f(aux, 1) == doctest::Approx(2.5));
        } else if (tag[5] == 'I') {
            CHECK(bam_auxB2i(aux, 1) == 4294967295LL);
        }
        bam_destroy1(record);
    }
}

TEST_CASE("SAM auxiliary parser rejects invalid input and buffer overflow") {
    CHECK(samAuxBytes("", 0).empty());
    CHECK_THROWS(samAuxBytes("XA:B:q,1", 100));
    CHECK_THROWS(samAuxBytes("XA:B:I,4294967296", 100));
    CHECK_THROWS(samAuxBytes("XA:B:i,1,2", 4));
    CHECK_THROWS(samAuxBytes("XA:Q:hello", 100));
    const auto mixed = samAuxBytes("XI:i:42\tXZ:Z:index\tXA:A:Y\tXF:f:1.5\tXB:B:i,1,2", 100);
    CHECK(mixed.size() > 20);
}
