#ifndef STAR_HUGE_PAGES_H
#define STAR_HUGE_PAGES_H
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#if defined(__linux__)
#include <sys/mman.h>
#endif

// Asks Linux to back a large allocation with transparent huge pages. Suffix-array
// and genome lookups land on random pages, so with 4 KiB pages nearly every one
// also misses the TLB. Call before the memory is first written: pages already
// touched stay small until khugepaged collapses them.
//
// A hint only: contents and output are unchanged, failure is ignored, and it does
// nothing on other systems, when THP is "never", or when STAR_NO_HUGEPAGES=1.
// Only the 2 MiB-aligned interior of the range is advised.
inline void adviseHugePages(void* start, std::size_t bytes) {
#if defined(__linux__) && defined(MADV_HUGEPAGE)
    constexpr std::uintptr_t huge = std::uintptr_t(1) << 21;
    if (bytes < huge) return;
    if (const char* off = std::getenv("STAR_NO_HUGEPAGES"); off && std::strcmp(off, "1") == 0) return;
    const auto begin = reinterpret_cast<std::uintptr_t>(start);
    const std::uintptr_t first = (begin + huge - 1) & ~(huge - 1);
    const std::uintptr_t last = (begin + bytes) & ~(huge - 1);
    if (last > first) (void)madvise(reinterpret_cast<void*>(first), last - first, MADV_HUGEPAGE);
#else
    (void)start; (void)bytes;
#endif
}
#endif
