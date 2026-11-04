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

#endif
