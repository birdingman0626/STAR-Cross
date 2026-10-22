#ifndef PACKEDARRAY_DEF
#define PACKEDARRAY_DEF

#include "IncludeDefine.h"
#include "byteOrder.h"
#include <memory>

class PackedArray {
    private:
        uint bitRecMask, wordCompLength;
        // Storage ownership is independent of charArray, which may be a view.
        std::unique_ptr<char[]> storage;
    public:
        uint wordLength, length, lengthByte;
        uint operator [] (uint ii) const;
        char* charArray;

    PackedArray();
    // Genome snapshots borrow their source arrays; they must not outlive it.
    PackedArray(const PackedArray& other);
    PackedArray& operator=(const PackedArray& other);
    PackedArray(PackedArray&& other) noexcept;
    PackedArray& operator=(PackedArray&& other) noexcept;
    void defineBits (uint Nbits, uint lengthIn);
    void writePacked(uint jj, uint x);
    void allocateArray();
    void deallocateArray();
    void pointArray(char* pointerCharIn);
//     PackedArray(uint N);
};

inline uint PackedArray::operator [] (uint ii) const {
   uint b=ii*wordLength;
   uint B=b/8;
   uint S=b%8;

   uint a1 = loadUintLE(charArray+B); // little-endian byte stream (native load on LE)
   a1 = (a1>>S) & bitRecMask; // bitmask instead of double-shift (upstream PR #791)
   return a1;
};

#endif
