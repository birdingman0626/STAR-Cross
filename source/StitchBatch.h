#ifndef STAR_STITCH_BATCH_H
#define STAR_STITCH_BATCH_H
// Windows collected from one seed batch for device stitching, and the results
// injected back into stitchPieces. Plain records only; no STAR object pointers.
#include "StitchCore.h"
#include <cstdint>
#include <vector>

struct StitchReadDesc {
    unsigned long long seqOffset=0, Lread=0, mismatchLimit=0;
    unsigned long long readLength[stitchcore::maxMates]={0,0,0};
    unsigned windowBegin=0, windowCount=0;
};
struct StitchWindowDesc {
    unsigned long long Chr=0, Str=0, roStr=0, nA=0, waOffset=0;  // waOffset in rows
};
struct StitchBatch {
    std::vector<StitchReadDesc> reads;
    std::vector<char> seq;                 // per read: forward then reverse-complement, Lread each
    std::vector<StitchWindowDesc> windows; // non-empty windows in stitchPieces order
    std::vector<unsigned long long> wa;    // rows of WA_SIZE values, last anchor marked
    void clear() {reads.clear(); seq.clear(); windows.clear(); wa.clear();}
};
enum StitchStatus : int {
    stitchOk=0, stitchStackOverflow=1, stitchSlotOverflow=2, stitchOutputOverflow=4, stitchNotRun=8
};
struct StitchReadResult {
    int status=stitchNotRun;
    int mates[stitchcore::maxMates]={0,0,0};
    unsigned long long resultBase=0;       // first transcript of this read in `transcripts`
    unsigned resultCount=0;
    unsigned windowBegin=0, windowCount=0;  // copied from the batch on the host
};
struct StitchWindowResult {
    unsigned count=0, first=0;             // nWinTr and its first transcript (relative to resultBase)
};
struct StitchResults {
    std::vector<StitchReadResult> reads;
    std::vector<StitchWindowResult> windows;  // parallel to StitchBatch::windows
    std::vector<stitchcore::TrHead> transcripts; // packed: header per transcript
    std::vector<stitchcore::TrRow> rows;         // live exon rows, addressed by TrHead::row
    const StitchBatch* batch=nullptr;            // the collected windows these results belong to
    void clear() {reads.clear(); windows.clear(); transcripts.clear(); rows.clear();}
    void get(size_t i,stitchcore::Tr& out) const {stitchcore::unpackTr(transcripts.at(i),rows.data(),out);}
};
#endif
