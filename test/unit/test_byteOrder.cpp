#include "doctest/doctest.h"
#include "byteOrder.h"
#include <cstring>

// Reference little-endian interpretation, independent of host byte order.
static uint refLoadLE(const unsigned char *p) {
    uint v = 0;
    for (uint i = 0; i < sizeof(uint); i++)
        v |= ((uint)p[i]) << (8 * i);
    return v;
}

TEST_CASE("byteOrder - loadUintLE_bytewise matches little-endian reference") {
    // This exercises the big-endian code path's algorithm even on a
    // little-endian CI runner: the byte-wise loader must always interpret the
    // buffer as little-endian regardless of host byte order.
    unsigned char buf[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    CHECK(loadUintLE_bytewise((const char*)buf) == refLoadLE(buf));
    CHECK(loadUintLE_bytewise((const char*)buf) == 0xEFCDAB8967452301ULL);
}

TEST_CASE("byteOrder - storeUintLE_bytewise produces little-endian bytes") {
    char buf[8] = {};
    storeUintLE_bytewise(buf, 0xEFCDAB8967452301ULL);
    unsigned char expect[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
    CHECK(std::memcmp(buf, expect, 8) == 0);
}

TEST_CASE("byteOrder - native loadUintLE agrees with portable byte-wise path") {
    // On little-endian hosts the native single-instruction load must give the
    // same value as the portable byte-wise path; this is the invariant that lets
    // the LE fast path and the BE portable path be used interchangeably.
    const uint vals[] = {0ULL, 1ULL, 0xFFULL, 0x100ULL, 0x1FFFFFULL,
                         12345678901234ULL, ~0ULL, 0xDEADBEEFCAFEBABEULL};
    for (uint v : vals) {
        char buf[8];
        storeUintLE_bytewise(buf, v);
        CHECK(loadUintLE((const char*)buf) == loadUintLE_bytewise((const char*)buf));
        CHECK(loadUintLE((const char*)buf) == v);
    }
}

TEST_CASE("byteOrder - store/load round-trip (both paths)") {
    const uint vals[] = {0ULL, 42ULL, 0x7FFFFFFFFFFFFFFFULL, ~0ULL, 0xA5A5A5A5A5A5A5A5ULL};
    for (uint v : vals) {
        char nbuf[8], bbuf[8];
        storeUintLE(nbuf, v);            // native (LE) or portable (BE), per build
        storeUintLE_bytewise(bbuf, v);   // always portable
        CHECK(loadUintLE((const char*)nbuf) == v);
        CHECK(loadUintLE_bytewise((const char*)bbuf) == v);
        // Both encoders must yield identical on-disk bytes (format stability).
        CHECK(std::memcmp(nbuf, bbuf, 8) == 0);
    }
}
