#ifndef PACKEDARRAY_DEF
#define PACKEDARRAY_DEF

#include "IncludeDefine.h"
#include "byteOrder.h"
#include <memory>
#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <xmmintrin.h>
#endif

class PackedArray {
    private:
        uint bitRecMask, wordCompLength;
        // Storage ownership is independent of charArray, which may be a view.
        std::unique_ptr<char[]> storage;
    public:
        uint wordLength, length, lengthByte;
        uint operator [] (uint ii) const;
        // Cache hint only: never dereferences, so any ii is safe; no result changes.
        void prefetch(uint ii) const;
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

inline void PackedArray::prefetch(uint ii) const {
#if defined(STAR_NO_SA_PREFETCH)
   (void)ii;
#elif defined(__GNUC__) || defined(__clang__)
   __builtin_prefetch(charArray+(ii*wordLength)/8, 0, 3);
#elif defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
   _mm_prefetch(charArray+(ii*wordLength)/8, _MM_HINT_T0);
#else
   (void)ii;
#endif
};

#endif
