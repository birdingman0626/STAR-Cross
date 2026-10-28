#ifndef CODE_ClipCR4
#define CODE_ClipCR4

#include "IncludeDefine.h"
#include <vector>

// Alignment result for one database sequence (score + end position)
struct ClipAlignResult
{
    int score;
    int endLocationTarget; // 0-indexed position in target where alignment ends
};

class ClipCR4
{
public:
    static constexpr int kLanes = 64; // sequences aligned together
    int dbN; // number of sequences in the alignment "database" (kLanes)

    std::vector<uint8*> storeClip;

    // Results for each sequence in database
    std::vector<ClipAlignResult> alignRes;

    ClipCR4();
    ~ClipCR4() = default;

    ClipCR4(const ClipCR4&) = delete;
    ClipCR4& operator=(const ClipCR4&) = delete;

    void fillOneSeq(uint32 idb, char *seq, uint32 seqL);
    void align(uint8 *query, uint32 queryLen, int dbN1);
    static uint32 polyTail3p(char *seq, uint32 seqLen); // stateless: the 3' mate has no ClipCR4 instance

private:
    uint32 readLen; // sequence length to align against

    // Scoring: match 1, mismatch -2, N-vs-N 0; a gap of length k costs
    // gapOpen + (k-1)*gapExt.
    int match;
    int mismatch;
    int gapOpen;
    int gapExt;

    // Database sequences as base codes (A=0,C=1,G=2,T=3,N=4), stored
    // column-major: dbCode[position*dbN + sequence], so one alignment column
    // of every sequence is contiguous.
    std::vector<int16_t> dbCode;

    // Per-row DP state, row-major [row*dbN + sequence]; reused across calls.
    std::vector<int16_t> rowH16, rowE16;
    std::vector<int32_t> rowH32, rowE32;
};

#endif
