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

#endif
