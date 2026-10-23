#ifndef PACKEDARRAY_DEF
#define PACKEDARRAY_DEF

#include "IncludeDefine.h"
#include "byteOrder.h"
#include <memory>

class PackedArray {
    private:
        uint64 bitRecMask, wordCompLength;
        // Storage ownership is independent of charArray, which may be a view.
        std::unique_ptr<char[]> storage;
    public:
        uint64 wordLength, length, lengthByte;
        uint64 operator [] (uint64 ii) const;
        char* charArray;

    PackedArray();
    // Genome snapshots borrow their source arrays; they must not outlive it.
    PackedArray(const PackedArray& other);
    PackedArray& operator=(const PackedArray& other);
    PackedArray(PackedArray&& other) noexcept;
    PackedArray& operator=(PackedArray&& other) noexcept;
    void defineBits (uint64 Nbits, uint64 lengthIn);
    void writePacked(uint64 jj, uint64 x);
    void allocateArray();
    void deallocateArray();
    void pointArray(char* pointerCharIn);
//     PackedArray(uint64 N);
};

inline uint64 PackedArray::operator [] (uint64 ii) const {
   uint64 b=ii*wordLength;
   uint64 B=b/8;
   uint64 S=b%8;

   uint64 a1 = loadUintLE(charArray+B); // little-endian byte stream (native load on LE)
   a1 = (a1>>S) & bitRecMask; // bitmask instead of double-shift (upstream PR #791)
   return a1;
};

#endif
