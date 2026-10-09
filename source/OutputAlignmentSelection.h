#ifndef STAR_OUTPUT_ALIGNMENT_SELECTION_H
#define STAR_OUTPUT_ALIGNMENT_SELECTION_H
#include "IncludeDefine.h"

struct OutputAlignmentSelection {
    bool enabled, onlyAddedReferences, allAddedReferences;
    uint firstAddedReference;
};

// Allocation-free, stable compaction of borrowed candidates. The alignment
// type only needs Chr/primaryFlag; no parser, index owner or output stream.
template<class Alignment>
uint selectOutputAlignments(OutputAlignmentSelection policy, Alignment** candidates, uint count) {
    if (!policy.enabled) return count;
    if (policy.onlyAddedReferences) {
        for (uint i=0;i<count;++i)
            if (candidates[i]->Chr<policy.firstAddedReference) return 0;
    } else if (policy.allAddedReferences) {
        uint retained=0;
        for (uint i=0;i<count;++i) {
            if (candidates[i]->Chr>=policy.firstAddedReference) {
                candidates[retained]=candidates[i];
                candidates[retained++]->primaryFlag=false;
            }
        }
        if (retained) candidates[0]->primaryFlag=true;
        return retained;
    }
    return count;
}
#endif
