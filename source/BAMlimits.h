#ifndef DEF_BAMlimits
#define DEF_BAMlimits

#include <cstdint>
#include <cstddef>
#include <vector>

// BAM stores the reference count, reference lengths and 0-based positions as int32.
constexpr unsigned long long BAM_MAX_REFERENCE_LENGTH = INT32_MAX;

// Index of the first reference too long for BAM, or lengths.size() if all fit.
inline std::size_t firstOversizedBAMreference(const std::vector<unsigned long long> &lengths) {
    for (std::size_t ii=0; ii<lengths.size(); ii++) {
        if (lengths[ii] > BAM_MAX_REFERENCE_LENGTH)
            return ii;
    };
    return lengths.size();
};

// A packed BAM CIGAR operation is len<<4|op with a 28-bit length.
constexpr unsigned int BAM_CIGAR_MAX_OP_LENGTH = (1u<<28) - 1;

// Appends operation 'op' of length 'len' to packed[n...], splitting a length
// above BAM_CIGAR_MAX_OP_LENGTH into consecutive operations of the same type
// (e.g. 268435455N1000N), which SAM/BAM readers treat as one gap. Returns the
// new operation count. An alignment spans at most one reference
// (<= BAM_MAX_REFERENCE_LENGTH), so a split adds at most 8 operations.
inline unsigned int appendBAMcigarOp(unsigned int *packed, unsigned int n, unsigned long long len, unsigned int op) {
    while (len > BAM_CIGAR_MAX_OP_LENGTH) {
        packed[n++] = BAM_CIGAR_MAX_OP_LENGTH<<4 | op;
        len -= BAM_CIGAR_MAX_OP_LENGTH;
    };
    packed[n++] = static_cast<unsigned int>(len)<<4 | op;
    return n;
};

// BAI bin for an alignment covering [beg,end) (zero-based, half-open); same as
// htslib's hts_reg2bin(beg,end,14,5). The scheme is only defined below 2^29;
// past that the value is meaningless and, above ~1 Gb, exceeds 16 bits.
inline int reg2bin(int beg, int end) {
    --end;
    if (beg>>14 == end>>14) return ((1<<15)-1)/7 + (beg>>14);
    if (beg>>17 == end>>17) return ((1<<12)-1)/7 + (beg>>17);
    if (beg>>20 == end>>20) return ((1<<9)-1)/7 + (beg>>20);
    if (beg>>23 == end>>23) return ((1<<6)-1)/7 + (beg>>23);
    if (beg>>26 == end>>26) return ((1<<3)-1)/7 + (beg>>26);
    return 0;
};

// The 16-bit bin field of a BAM record. Keeps the low 16 bits, as htslib does
// when writing (bam1_core_t::bin is uint16_t), so STAR's BAM matches samtools'
// byte for byte. Readers (htslib, CSI indexing) recompute the bin from POS and
// CIGAR, so the value past 2^29 has no effect on them.
inline unsigned int bamBinField(int beg, int end) {
    return static_cast<unsigned int>(reg2bin(beg, end)) & 0xFFFFu;
};

#endif
