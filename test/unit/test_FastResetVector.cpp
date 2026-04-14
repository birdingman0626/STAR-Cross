#include "doctest/doctest.h"
#include "FastResetVector.h"

TEST_CASE("FastResetVector - resize initializes all elements to resetValue") {
    FastResetVector<unsigned short> v;
    v.resize(100, 0xFFFF);
    for (int i = 0; i < 100; i++) {
        CHECK(v[i] == 0xFFFF);
    }
    CHECK(v.size() == 100);
}

TEST_CASE("FastResetVector - set and read back") {
    FastResetVector<unsigned short> v;
    v.resize(10, 0xFFFF);
    v.set(0, 1);
    v.set(5, 42);
    v.set(9, 999);
    CHECK(v[0] == 1);
    CHECK(v[5] == 42);
    CHECK(v[9] == 999);
    // unwritten elements remain at resetValue
    CHECK(v[1] == 0xFFFF);
    CHECK(v[4] == 0xFFFF);
}

TEST_CASE("FastResetVector - reset restores all modified elements to resetValue") {
    FastResetVector<unsigned short> v;
    v.resize(100, 0xFFFF);
    v.set(0, 1);
    v.set(50, 2);
    v.set(99, 3);
    v.reset();
    for (int i = 0; i < 100; i++) {
        CHECK(v[i] == 0xFFFF);
    }
}

TEST_CASE("FastResetVector - set then reset then set again works correctly") {
    FastResetVector<int> v;
    v.resize(10, 0);
    v.set(3, 7);
    v.reset();
    CHECK(v[3] == 0);
    v.set(3, 99);
    CHECK(v[3] == 99);
    v.reset();
    CHECK(v[3] == 0);
}

TEST_CASE("FastResetVector - double-write to same index does not double-track") {
    // If the same index is written twice, _modified must only contain it once.
    // Otherwise reset() would process it twice (harmless but wasteful); more importantly,
    // this verifies the guard: `if (_data[i] == _resetValue) _modified.push_back(i)`.
    FastResetVector<int> v;
    v.resize(10, 0);
    v.set(5, 10);
    v.set(5, 20); // second write: _data[5] != _resetValue, so must NOT push again
    CHECK(v[5] == 20);
    v.reset();
    CHECK(v[5] == 0);
    // After reset, write again — must be tracked correctly
    v.set(5, 30);
    CHECK(v[5] == 30);
    v.reset();
    CHECK(v[5] == 0);
}

TEST_CASE("FastResetVector - write same index to resetValue and back") {
    // Edge case: writing the resetValue itself should not track the index
    // (since after write, _data[i] == _resetValue, and a later read of 0 at that
    // position is indistinguishable from never-written). The guard only pushes when
    // _data[i] == _resetValue BEFORE the write, so writing resetValue explicitly is fine.
    FastResetVector<int> v;
    v.resize(5, 0);
    v.set(2, 0); // writing resetValue: _data[2]==_resetValue before write, so it IS tracked
    v.set(2, 7); // now _data[2]!=_resetValue, NOT tracked again
    CHECK(v[2] == 7);
    v.reset();
    CHECK(v[2] == 0);
}

TEST_CASE("FastResetVector - non-zero resetValue") {
    FastResetVector<unsigned short> v;
    v.resize(50, 0xFFFF); // uintWinBinMax equivalent
    v.set(10, 3);
    v.set(20, 7);
    v.reset();
    for (int i = 0; i < 50; i++) {
        CHECK(v[i] == 0xFFFF);
    }
}

TEST_CASE("FastResetVector - data() pointer matches operator[]") {
    FastResetVector<int> v;
    v.resize(8, -1);
    v.set(3, 42);
    CHECK(v.data()[3] == 42);
    CHECK(v.data()[0] == -1);
}

TEST_CASE("FastResetVector - empty reset is safe") {
    FastResetVector<int> v;
    v.resize(10, 0);
    v.reset(); // no modifications, should not crash
    CHECK(v[5] == 0);
}
