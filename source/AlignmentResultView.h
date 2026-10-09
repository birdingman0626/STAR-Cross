#ifndef STAR_ALIGNMENT_RESULT_VIEW_H
#define STAR_ALIGNMENT_RESULT_VIEW_H
#include <cstdint>
class Transcript;

// Synchronous serialization boundary. These borrow the mapper's scratch arena;
// consume before the next read or any remapping. Never enqueue this view.
struct AlignmentResultView {
    uint64_t count;
    Transcript** candidates;
    Transcript* best;
};
#endif
