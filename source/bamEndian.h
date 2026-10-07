#ifndef STAR_BAM_ENDIAN_H
#define STAR_BAM_ENDIAN_H

#include "byteOrder.h"
#include "unaligned.h"
#include "htslib/htslib/hts_endian.h"
#include "htslib/htslib/bgzf.h"

// Private sorting buffers contain native core/CIGAR words. BAM files require
// little endian; auxiliary fields are already encoded in BAM byte order.
// Convert only the final output buffer, after every native consumer has finished.
inline bool bamNativeRecordToLE(char* record, size_t size) {
    if (size < 36 || loadUnaligned<uint32_t>(record) != size - 4) return false;
    const uint32_t nameSize = loadUnaligned<uint32_t>(record + 12) & 0xff;
    const uint32_t cigarCount = loadUnaligned<uint32_t>(record + 16) & 0xffff;
    if (36ULL + nameSize + 4ULL*cigarCount > size) return false;
    for (size_t i=0; i<9; ++i)
        u32_to_le(loadUnaligned<uint32_t>(record + 4*i), reinterpret_cast<uint8_t*>(record + 4*i));
    char* cigar = record + 36 + nameSize;
    for (size_t i=0; i<cigarCount; ++i)
        u32_to_le(loadUnaligned<uint32_t>(cigar + 4*i), reinterpret_cast<uint8_t*>(cigar + 4*i));
    return true;
}

inline ssize_t bamWriteNativeRecords(BGZF* file, char* records, size_t size) {
#if STAR_BIG_ENDIAN
    for (size_t offset=0; offset<size;) {
        if (size-offset < 4) { errno=EINVAL; return -1; }
        const size_t length = size_t(loadUnaligned<uint32_t>(records+offset)) + 4;
        if (length > size-offset || !bamNativeRecordToLE(records+offset, length)) {
            errno=EINVAL;
            return -1;
        }
        offset += length;
    }
#endif
    return bgzf_write(file, records, size);
}

inline ssize_t bamWriteInt32LE(BGZF* file, uint32_t value) {
    uint8_t bytes[4];
    u32_to_le(value, bytes);
    return bgzf_write(file, bytes, sizeof(bytes));
}
#endif
