#include "doctest/doctest.h"
#include "binarySearch2.h"  // includes IncludeDefine.h which defines uint

// binarySearch2(x, y, X, Y, N):
//   binary-search sorted X[] for x, then linear-scan for matching y in Y[].
//   Returns index if (x,y) found.
//   Returns -1 if x is not in X at all, or if the back-scan found x but then exhausted
//     back to index 0 before finding y (uncommon: only when there are many x duplicates
//     left of the binary-search landing point and y is not among them).
//   Returns -2 if forward-scan exhausted N without finding y (x IS in X, y is not).

TEST_CASE("binarySearch2 - empty array returns -1") {
    CHECK(binarySearch2(5, 5, nullptr, nullptr, 0) == -1);
}

TEST_CASE("binarySearch2 - single element match") {
    uint X[] = {10};
    uint Y[] = {20};
    CHECK(binarySearch2(10, 20, X, Y, 1) == 0);
}

TEST_CASE("binarySearch2 - single element no match on y") {
    uint X[] = {10};
    uint Y[] = {20};
    // x=10 IS in the array; y=99 is not found; forward-scan exhausts N → returns -2
    CHECK(binarySearch2(10, 99, X, Y, 1) == -2);
}

TEST_CASE("binarySearch2 - x out of range low") {
    uint X[] = {10, 20, 30};
    uint Y[] = {1, 2, 3};
    CHECK(binarySearch2(5, 1, X, Y, 3) == -1);
}

TEST_CASE("binarySearch2 - x out of range high") {
    uint X[] = {10, 20, 30};
    uint Y[] = {1, 2, 3};
    CHECK(binarySearch2(40, 3, X, Y, 3) == -1);
}

TEST_CASE("binarySearch2 - finds first element") {
    uint X[] = {10, 20, 30, 40, 50};
    uint Y[] = {1,  2,  3,  4,  5};
    CHECK(binarySearch2(10, 1, X, Y, 5) == 0);
}

TEST_CASE("binarySearch2 - finds last element") {
    uint X[] = {10, 20, 30, 40, 50};
    uint Y[] = {1,  2,  3,  4,  5};
    CHECK(binarySearch2(50, 5, X, Y, 5) == 4);
}

TEST_CASE("binarySearch2 - finds middle element") {
    uint X[] = {10, 20, 30, 40, 50};
    uint Y[] = {1,  2,  3,  4,  5};
    CHECK(binarySearch2(30, 3, X, Y, 5) == 2);
}

TEST_CASE("binarySearch2 - duplicate x values, finds correct y") {
    // x=20 appears at indices 1,2,3 with different y values
    uint X[] = {10, 20, 20, 20, 30};
    uint Y[] = {1,  5,  6,  7,  8};
    CHECK(binarySearch2(20, 5, X, Y, 5) == 1);
    CHECK(binarySearch2(20, 6, X, Y, 5) == 2);
    CHECK(binarySearch2(20, 7, X, Y, 5) == 3);
}

TEST_CASE("binarySearch2 - duplicate x values, y not present") {
    uint X[] = {10, 20, 20, 20, 30};
    uint Y[] = {1,  5,  6,  7,  8};
    // x=20 is in the array; y=99 is not; -1 or -2 depending on scan path
    int r = binarySearch2(20, 99, X, Y, 5);
    CHECK((r == -1 || r == -2));
}

TEST_CASE("binarySearch2 - x present but no matching y") {
    uint X[] = {10, 20, 30};
    uint Y[] = {1,  2,  3};
    // x=20 is in the array; y=99 is not; -1 or -2 depending on scan path
    int r = binarySearch2(20, 99, X, Y, 3);
    CHECK((r == -1 || r == -2));
}

TEST_CASE("binarySearch2 - large array, known position") {
    const int N = 1000;
    uint X[N], Y[N];
    for (int i = 0; i < N; i++) {
        X[i] = (uint)i * 2;  // 0,2,4,...,1998
        Y[i] = (uint)i * 3;
    }
    // search for x=500, y=750 → index 250
    CHECK(binarySearch2(500, 750, X, Y, N) == 250);
    // search for odd x (not in array)
    CHECK(binarySearch2(501, 750, X, Y, N) == -1);
}
