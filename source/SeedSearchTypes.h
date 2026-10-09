#ifndef STAR_SEED_SEARCH_TYPES_H
#define STAR_SEED_SEARCH_TYPES_H
#include <cstdint>
// Native capture ABI: preserve field widths/order when adding executors.
struct SeedQuery {
    uint64_t offset, readBytes, start, length, lower, upper, initialLength, forward;
};
struct SeedMatch { uint64_t length, lower, upper, multiplicity; };
#endif
