#ifndef STAR_BARCODE_HINT_H
#define STAR_BARCODE_HINT_H
#include <cstdint>

// Cell-barcode whitelist lookup computed ahead of time (on the device) for one read.
// matchCBtoWL uses it only when key equals the barcode it converts itself, so a stale
// or missing hint falls back to the original binary searches.
struct BarcodeHint {
    uint64_t key=0;
    int64_t exact=-2;   // -2: no hint; -1: not in the whitelist; else the whitelist index
    uint64_t mask=0;    // when exact==-1: bit 3*i+(j-1) set if key^(j<<2i) is in the whitelist
};
#endif
