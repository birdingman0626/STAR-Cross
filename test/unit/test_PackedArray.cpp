#include "doctest/doctest.h"
#include "PackedArray.h"

// Compute the value mask for a given bit-width (bitRecMask is private in PackedArray).
static uint makeMask(uint wordLen) {
    return wordLen >= 64 ? ~(uint)0 : ((uint)1 << wordLen) - 1;
}

// Helper: allocate a PackedArray with given bit-width and length, write values, read back.
static void roundtrip(uint wordLen, uint length, uint value) {
    uint mask = makeMask(wordLen);
    PackedArray pa;
    pa.defineBits(wordLen, length);
    pa.allocateArray();
    for (uint i = 0; i < length; i++)
        pa.writePacked(i, value & mask);
    for (uint i = 0; i < length; i++)
        CHECK(pa[i] == (value & mask));
    pa.deallocateArray();
}

TEST_CASE("PackedArray - defineBits sets wordLength and length") {
    PackedArray pa;
    pa.defineBits(5, 100);
    CHECK(pa.wordLength == 5);
    CHECK(pa.length == 100);
}

TEST_CASE("PackedArray - 1-bit words round-trip") {
    roundtrip(1, 64, 1);
    roundtrip(1, 64, 0);
}

TEST_CASE("PackedArray - 5-bit words round-trip (typical suffix array use)") {
    PackedArray pa;
    pa.defineBits(5, 100);
    pa.allocateArray();
    for (uint i = 0; i < 100; i++)
        pa.writePacked(i, i % 32); // 5-bit max = 31
    for (uint i = 0; i < 100; i++)
        CHECK(pa[i] == i % 32);
    pa.deallocateArray();
}

TEST_CASE("PackedArray - 16-bit words round-trip") {
    roundtrip(16, 50, 0xABCD);
    roundtrip(16, 50, 0xFFFF);
    roundtrip(16, 50, 0);
}

TEST_CASE("PackedArray - 21-bit words round-trip (genome SA typical)") {
    PackedArray pa;
    pa.defineBits(21, 20);
    pa.allocateArray();
    uint vals[] = {0, 1, (1<<20)-1, (1<<21)-1, 12345, 0x1FFFFF};
    for (uint i = 0; i < 20; i++)
        pa.writePacked(i, vals[i % 6]);
    for (uint i = 0; i < 20; i++)
        CHECK(pa[i] == vals[i % 6]);
    pa.deallocateArray();
}

TEST_CASE("PackedArray - sequential writes do not corrupt adjacent words") {
    // Write ascending values, verify no cross-contamination
    const uint bits = 7;
    const uint len  = 200;
    const uint mask = (1u << bits) - 1; // 0x7F
    PackedArray pa;
    pa.defineBits(bits, len);
    pa.allocateArray();
    for (uint i = 0; i < len; i++)
        pa.writePacked(i, i & mask);
    for (uint i = 0; i < len; i++)
        CHECK(pa[i] == (i & mask));
    pa.deallocateArray();
}

TEST_CASE("PackedArray - overwrite same index does not corrupt neighbors") {
    PackedArray pa;
    pa.defineBits(8, 10);
    pa.allocateArray();
    for (uint i = 0; i < 10; i++)
        pa.writePacked(i, 0xAA);
    pa.writePacked(5, 0x55); // overwrite middle
    for (uint i = 0; i < 10; i++) {
        if (i == 5) CHECK(pa[i] == 0x55);
        else        CHECK(pa[i] == 0xAA);
    }
    pa.deallocateArray();
}

TEST_CASE("PackedArray - pointArray uses external buffer") {
    const uint bits = 8, len = 4;
    PackedArray pa;
    pa.defineBits(bits, len);
    // Allocate enough bytes manually
    char buf[64] = {};
    pa.pointArray(buf);
    pa.writePacked(0, 0x12);
    pa.writePacked(1, 0x34);
    pa.writePacked(2, 0x56);
    pa.writePacked(3, 0x78);
    CHECK(pa[0] == 0x12);
    CHECK(pa[1] == 0x34);
    CHECK(pa[2] == 0x56);
    CHECK(pa[3] == 0x78);
    // pointArray must not call new[] so deallocateArray should be a no-op
    pa.deallocateArray(); // must not free buf
}

TEST_CASE("PackedArray - deallocateArray is idempotent") {
    PackedArray pa;
    pa.defineBits(8, 10);
    pa.allocateArray();
    pa.deallocateArray();
    pa.deallocateArray(); // second call must not crash or double-free
}
