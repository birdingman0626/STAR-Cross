#ifndef STAR_SOLO_TEXT_IO_H
#define STAR_SOLO_TEXT_IO_H
#include <charconv>
#include <istream>
#include <string>
#include <type_traits>

// Fast text I/O for the STARsolo temporary read files (_STARtmp/solo*).
// Each mapped read writes a line of integers and each line is parsed again
// for counting. Formatted iostream operators cost ~10x more per field with
// MSVC's standard library (and ~2x with libstdc++) than these helpers. The
// file bytes and the parsed values are the same as with << and >>.

// Appends the decimal form of an integer, exactly as operator<< writes it.
template<class T> inline void appendNumber(std::string& line, T value) {
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool> && sizeof(T) > 1,
                  "operator<< prints char types as characters, not numbers");
    char digits[24];
    const auto end = std::to_chars(digits, digits + sizeof(digits), value).ptr;
    line.append(digits, end);
}

namespace soloTextIO {
inline bool isSpace(int c) { return c == ' ' || c == '\n' || c == '\t' || c == '\r' || c == '\v' || c == '\f'; }
// Skips leading white space like operator>>; sets eof+fail and returns EOF at the end.
inline int skipSpace(std::istream& in) {
    auto* buffer = in.rdbuf();
    int c = buffer->sgetc();
    while (c != std::char_traits<char>::eof() && isSpace(c)) {
        buffer->sbumpc();
        c = buffer->sgetc();
    }
    if (c == std::char_traits<char>::eof()) in.setstate(std::ios::eofbit | std::ios::failbit);
    return c;
}
}

// Reads one decimal integer like operator>>: leading white space is skipped, a
// sign is accepted (a '-' on an unsigned type wraps, as strtoull does, so "-1"
// reads as the type's maximum), eofbit is set when the number ends the file,
// and failbit (value 0) when there is no number. Reads straight from the
// stream's buffer, so it can be mixed with getline/ignore on the same stream.
// Values are assumed to fit their type (the writer is STAR itself).
template<class T> inline bool readNumber(std::istream& in, T& value) {
    static_assert(std::is_integral_v<T> && !std::is_same_v<T, bool> && sizeof(T) > 1);
    if (!in.good()) { in.setstate(std::ios::failbit); return false; }
    int c = soloTextIO::skipSpace(in);
    if (c == std::char_traits<char>::eof()) return false;
    auto* buffer = in.rdbuf();
    bool negative = false;
    if (c == '-' || c == '+') {
        negative = c == '-';
        buffer->sbumpc();
        c = buffer->sgetc();
    }
    if (c < '0' || c > '9') {
        value = 0;
        in.setstate(c == std::char_traits<char>::eof() ? std::ios::eofbit | std::ios::failbit : std::ios::failbit);
        return false;
    }
    unsigned long long magnitude = 0;
    do {
        magnitude = magnitude * 10 + unsigned(c - '0');
        buffer->sbumpc();
        c = buffer->sgetc();
    } while (c >= '0' && c <= '9');
    if (c == std::char_traits<char>::eof()) in.setstate(std::ios::eofbit);
    value = static_cast<T>(negative ? 0ULL - magnitude : magnitude);
    return true;
}

// Reads one non-white-space character, like operator>> into a char.
inline bool readChar(std::istream& in, char& value) {
    if (!in.good()) { in.setstate(std::ios::failbit); return false; }
    const int c = soloTextIO::skipSpace(in);
    if (c == std::char_traits<char>::eof()) return false;
    value = static_cast<char>(in.rdbuf()->sbumpc());
    return true;
}
#endif
