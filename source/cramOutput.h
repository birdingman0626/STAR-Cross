#ifndef CODE_cramOutput
#define CODE_cramOutput

#include <string>
#include "Parameters.h"

// Transcode a finished BAM file to referenceless CRAM, then remove the input
// BAM on success. This implements --outSAMtype CRAM by reusing STAR's proven
// BAM output path and converting at finalization, leaving the hot per-record
// output code untouched. Referenceless CRAM (CRAM_OPT_NO_REF) stores sequences
// verbatim, so no external reference FASTA is required.
//
// Returns true on success. On any failure it removes the partial CRAM, leaves
// the original BAM in place, and emits a warning (the run is not aborted).
bool bamToCramReferenceless(const std::string &inBam, const std::string &outCram, Parameters &P);

#endif
