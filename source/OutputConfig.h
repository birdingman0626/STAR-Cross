#ifndef STAR_OUTPUT_CONFIG_H
#define STAR_OUTPUT_CONFIG_H
#include "IncludeDefine.h"
// Serialization policy; streams, headers and output counters are runtime state.
struct OutputConfig {
    uint64 maxAlignments;
    int uniqueMapq, bamCompression;
};
#endif
