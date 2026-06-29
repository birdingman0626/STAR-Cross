#ifndef CODE_byteOrder
#define CODE_byteOrder

#include "IncludeDefine.h"

// STAR stores the genome, suffix array and packed arrays as a little-endian byte
// stream (the first bit lives in the least-significant bit of charArray[0]). On
// little-endian hosts such data can be read with a single native integer load,
// which is fast. On big-endian hosts that load would byte-swap the value, so the
// integer has to be assembled from / scattered to the byte stream explicitly.
//
// Only the load/store from memory is byte-order sensitive; once the value is a
// host integer, extracting bytes via shifts is portable. The little-endian fast
// path is the default; only known big-endian compiles take the portable path, so
// x86-64 and ARM64 (incl. MSVC, which never targets big-endian) are unaffected.

// STAR_BIG_ENDIAN may be forced via -DSTAR_BIG_ENDIAN=1 (e.g. to exercise the
// portable path on a little-endian build for testing); otherwise it is detected.
#ifndef STAR_BIG_ENDIAN
    #if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
        #define STAR_BIG_ENDIAN 1
    #else
        #define STAR_BIG_ENDIAN 0
    #endif
#endif

// Portable byte-wise implementations: interpret/produce sizeof(uint) (==8) bytes
// in little-endian order regardless of host byte order. Exposed by name so they
// can be unit-tested directly even on a little-endian machine.
inline uint loadUintLE_bytewise(const char *p) {
    uint v=0;
    for (uint ib=0; ib<sizeof(uint); ib++)
        v |= ((uint)(uchar)p[ib]) << (8*ib);
    return v;
}
inline void storeUintLE_bytewise(char *p, uint v) {
    for (uint ib=0; ib<sizeof(uint); ib++)
        p[ib] = (char)((v >> (8*ib)) & 0xffLLU);
}

// Load sizeof(uint) (==8) bytes as a little-endian unsigned integer.
inline uint loadUintLE(const char *p) {
#if STAR_BIG_ENDIAN
    return loadUintLE_bytewise(p);
#else
    return *((const uint*)p);
#endif
}

// Store an unsigned integer as sizeof(uint) (==8) little-endian bytes.
inline void storeUintLE(char *p, uint v) {
#if STAR_BIG_ENDIAN
    storeUintLE_bytewise(p, v);
#else
    *((uint*)p) = v;
#endif
}

#endif
