#include "doctest/doctest.h"
#include "SoloTextIO.h"
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// The helpers must read and write exactly what operator>> and operator<< do,
// so temporary Solo files and the counts parsed from them are unchanged.

template<class T> static void checkFormat(T value) {
    std::ostringstream reference;
    reference << value;
    std::string line;
    appendNumber(line, value);
    CHECK(line == reference.str());
}

TEST_CASE("SoloTextIO - appendNumber formats integers like operator<<") {
    checkFormat<int32_t>(-1);
    checkFormat<int32_t>(0);
    checkFormat<int32_t>(std::numeric_limits<int32_t>::min());
    checkFormat<uint32_t>(std::numeric_limits<uint32_t>::max());
    checkFormat<uint64_t>(std::numeric_limits<uint64_t>::max());
    checkFormat<uint64_t>(123456789012ULL);
    checkFormat<int64_t>(-987654321098LL);
    checkFormat<size_t>(42);
}

// Reads the same tokens with >> and with the helpers, comparing value and stream state after each.
template<class T> static void checkParse(const std::string& text, int tokens) {
    std::istringstream a(text), b(text);
    for (int i = 0; i < tokens; ++i) {
        T x{}, y{};
        a >> x;
        const bool ok = readNumber(b, y);
        CHECK(ok == !a.fail());
        CHECK(b.fail() == a.fail());
        CHECK(b.eof() == a.eof());
        if (!a.fail()) CHECK(y == x);
    }
}

TEST_CASE("SoloTextIO - readNumber matches operator>> values and stream state") {
    checkParse<uint64_t>("12 0 18446744073709551615\n", 4);
    checkParse<uint32_t>("-1 4294967295 7", 4);              // "-1" wraps to the maximum, as with >>
    checkParse<int32_t>("-1 0 2147483647 -2147483648\n\n", 5);
    checkParse<int64_t>("  \t\r\n-42\n", 2);
    checkParse<uint32_t>("5 x 6", 3);                         // a non-number sets failbit
    checkParse<int32_t>("", 1);
    checkParse<int32_t>("-", 1);
    checkParse<uint64_t>("99", 2);                            // number ending the file sets eofbit
}

TEST_CASE("SoloTextIO - readChar matches operator>> into char") {
    const std::string text = "17 F 23 :\n";
    std::istringstream a(text), b(text);
    for (int i = 0; i < 2; ++i) {
        uint32_t n1, n2; char c1, c2;
        a >> n1 >> c1;
        readNumber(b, n2); readChar(b, c2);
        CHECK(n1 == n2);
        CHECK(c1 == c2);
    }
    char c1, c2;
    a >> c1;
    CHECK_FALSE(readChar(b, c2));
    CHECK(b.fail() == a.fail());
    CHECK(b.eof() == a.eof());
}

TEST_CASE("SoloTextIO - mixes with ignore on the same stream, like a Solo record file") {
    // umi iread flag feature cbmatch cb ; second line is skipped with ignore
    std::istringstream in("100 0 3 -1 0 77\n200 1 3 5 0 78\n300 2 1 9 2 11 F 12 :\n");
    uint64_t umi, iread; uint32_t flag, feature; int32_t cbmatch; int64_t cb;
    REQUIRE(readNumber(in, umi));
    readNumber(in, iread); readNumber(in, flag); readNumber(in, feature); readNumber(in, cbmatch); readNumber(in, cb);
    CHECK(umi == 100);
    CHECK(feature == std::numeric_limits<uint32_t>::max());
    CHECK(cb == 77);
    readNumber(in, umi);
    in.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
    REQUIRE(readNumber(in, umi));
    CHECK(umi == 300);
    readNumber(in, iread); readNumber(in, flag); readNumber(in, feature); readNumber(in, cbmatch);
    CHECK(cbmatch == 2);
    std::vector<std::pair<uint32_t, char>> matches(2);
    for (auto& m : matches) { readNumber(in, m.first); readChar(in, m.second); }
    CHECK(matches[0] == std::make_pair(11u, 'F'));
    CHECK(matches[1] == std::make_pair(12u, ':'));
    CHECK_FALSE(readNumber(in, umi));
    CHECK(in.eof());
}
