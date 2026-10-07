#ifndef CODE_ClipCR4
#define CODE_ClipCR4

#include "IncludeDefine.h"
#include <parasail.h>
#include <memory>

// Alignment result for one database sequence (score + end position)
struct ClipAlignResult
{
    int score;
    int endLocationTarget; // 0-indexed position in target where alignment ends
};

class ClipCR4
{
public:
    int dbN; // number of sequences in the alignment "database"

    vector<uint8*> storeClip;

    // Results for each sequence in database
    vector<ClipAlignResult> alignRes;

    ClipCR4();
    ~ClipCR4() = default;

    // Non-copyable: owns raw heap allocations and C resources
    ClipCR4(const ClipCR4&) = delete;
    ClipCR4& operator=(const ClipCR4&) = delete;

    void fillOneSeq(uint32 idb, char *seq, uint32 seqL);
    void align(uint8 *query, uint32 queryLen, int dbN1);
    uint32 polyTail3p(char *seq, uint32 seqLen);

private:
    uint32 readLen; // sequence length to align against

    std::unique_ptr<parasail_matrix_t, decltype(&parasail_matrix_free)> scoreMatrix;
    int gapOpen;
    int gapExt;

    // Database sequences stored as ASCII characters (A/C/G/T/N)
    vector<char> dbSeqArr;
    vector<char*> dbSeqs;

    // Reused buffer; do not silently truncate user-supplied adapters.
    vector<char> queryCharsBuf;
};

#endif
