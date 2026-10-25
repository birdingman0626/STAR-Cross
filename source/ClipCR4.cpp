#include "ClipCR4.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
using namespace std;

ClipCR4::ClipCR4()
{
    dbN = kLanes;
    readLen = 91;
    match = 1;
    mismatch = -2;
    gapOpen = 2;
    gapExt = 2;

    dbCode.assign(static_cast<size_t>(dbN) * readLen, 4);
    storeClip.resize(dbN);
    alignRes.resize(dbN);
};

static inline int16_t baseCode(char c)
{
    switch (c) {
        case 'A': return 0;
        case 'C': return 1;
        case 'G': return 2;
        case 'T': return 3;
        default:  return 4; // N and any unknown base
    };
};

void ClipCR4::fillOneSeq(uint32 idb, char *seq, uint32 seqL)
{
    if (idb >= static_cast<uint32>(dbN) || (seq == nullptr && seqL != 0))
        throw std::invalid_argument("Invalid clipping database sequence");
    uint32 minLen = min(seqL, readLen);
    for (uint32 ib = 0; ib < readLen; ib++) // pad short sequences with N
        dbCode[static_cast<size_t>(ib) * dbN + idb] = ib < minLen ? baseCode(seq[ib]) : 4;
};

// Semi-global ("overlap") affine alignment of the query against each database
// sequence: leading and trailing gaps are free in both sequences. The result
// is the best score on the last query row (first target position wins ties),
// replaced by the best score in the last target column only when that is
// strictly larger. This is the endpoint rule of Parasail's sg scan and Opal's
// overlap mode, which earlier versions used.
//
// The database sequences have the same length, so each DP cell is computed for
// all of them at once: the innermost loops run over a fixed number of
// sequences and are plain element-wise integer code that compilers vectorize.
// Lane type T is int16 when the scores provably fit, otherwise int32.
template <typename T>
static void overlapAllLanes(const int16_t *dbCode, uint32 readLen, const uint8 *query, uint32 queryLen,
                            int match, int mismatch, int gapOpen, int gapExt, T negInf,
                            std::vector<T> &rowH, std::vector<T> &rowE, ClipAlignResult *res)
{
    constexpr int K = ClipCR4::kLanes;
    rowH.assign((static_cast<size_t>(queryLen) + 1) * K, 0); // column -1: free leading target gap
    rowE.assign((static_cast<size_t>(queryLen) + 1) * K, negInf);
    alignas(64) T diag[K], gapF[K], best[K];
    int32_t bestEnd[K];
    for (int s = 0; s < K; s++) {
        best[s] = negInf;
        bestEnd[s] = -1;
    };
    const T go = static_cast<T>(gapOpen), ge = static_cast<T>(gapExt), mm = static_cast<T>(mismatch);

    for (uint32 col = 0; col < readLen; col++) {
        const int16_t *__restrict target = dbCode + static_cast<size_t>(col) * K;
        for (int s = 0; s < K; s++) {
            diag[s] = 0; // row 0 is free: the query may start anywhere
            gapF[s] = negInf;
        };
        for (uint32 row = 1; row <= queryLen; row++) {
            const T q = static_cast<T>(std::min<uint8>(query[row - 1], 4));
            const T qMatch = static_cast<T>(q == 4 ? 0 : match); // N-vs-N is neutral
            const T *__restrict above = rowH.data() + static_cast<size_t>(row - 1) * K; // this column, row above
            T *__restrict h = rowH.data() + static_cast<size_t>(row) * K;
            T *__restrict e = rowE.data() + static_cast<size_t>(row) * K;
            for (int s = 0; s < K; s++) {
                const T previous = h[s];
                const T eNew = std::max<T>(previous - go, e[s] - ge);
                const T fNew = std::max<T>(above[s] - go, gapF[s] - ge);
                const T sub = target[s] == q ? qMatch : mm;
                const T hNew = std::max<T>(std::max<T>(eNew, fNew), diag[s] + sub);
                e[s] = eNew;
                gapF[s] = fNew;
                diag[s] = previous;
                h[s] = hNew;
            };
        };
        const T *lastRow = rowH.data() + static_cast<size_t>(queryLen) * K;
        for (int s = 0; s < K; s++) {
            if (lastRow[s] > best[s]) {
                best[s] = lastRow[s];
                bestEnd[s] = static_cast<int32_t>(col);
            };
        };
    };

    // rowH now holds the last target column
    for (int s = 0; s < K; s++) {
        T lastColumn = negInf;
        for (uint32 row = 1; row <= queryLen; row++)
            lastColumn = std::max<T>(lastColumn, rowH[static_cast<size_t>(row) * K + s]);
        if (lastColumn > best[s]) {
            best[s] = lastColumn;
            bestEnd[s] = static_cast<int32_t>(readLen) - 1;
        };
        res[s].score = best[s];
        res[s].endLocationTarget = bestEnd[s];
    };
};

void ClipCR4::align(uint8 *query, uint32 queryLen, int dbN1)
{
    if (queryLen == 0 || queryLen > static_cast<uint32>(std::numeric_limits<int>::max() / 4) ||
        query == nullptr || dbN1 < 0 || dbN1 > dbN)
        throw std::invalid_argument("Invalid clipping alignment dimensions");

    // Every lane is computed; lanes at or beyond dbN1 hold stale sequences and
    // their results are not used. Scores never drop below
    // -(gapOpen + gapExt*(queryLen+readLen)), so int16 lanes with a -16384
    // floor are exact while that bound stays above the floor.
    const int64_t lowest = -(static_cast<int64_t>(gapOpen) + static_cast<int64_t>(gapExt) * (queryLen + readLen));
    if (lowest - gapOpen - gapExt > -16384 + 64)
        overlapAllLanes<int16_t>(dbCode.data(), readLen, query, queryLen, match, mismatch, gapOpen, gapExt,
                                 static_cast<int16_t>(-16384), rowH16, rowE16, alignRes.data());
    else
        overlapAllLanes<int32_t>(dbCode.data(), readLen, query, queryLen, match, mismatch, gapOpen, gapExt,
                                 std::numeric_limits<int32_t>::min() / 2, rowH32, rowE32, alignRes.data());
};

uint32 ClipCR4::polyTail3p(char *seq, uint32 seqLen)
{// clip polyA tail
    // hardcoded for CR4 trimming
    if (seqLen < 20)
        return 0; // do not trim reads that are too short

    uint32_t ib1 = seqLen - 1;
    int32_t score = 0, score1 = 0;
    for (uint32_t ib = 1; ib <= seqLen; ib++) {
        if (seq[seqLen - ib] == 0) { // A-tail
            score += 1; // +1 for matches
            if (score * 10 >= (int)ib * 7) { // score>=0.7*clipL
                ib1 = ib;
                score1 = score;
            };
        } else {
            score -= 2; // -2 for mismatches
            if (ib - score > 27)
                break; // score drop is too big
        };
    };
    if (score1 < 20)
        ib1 = 0; // score is too small

    return ib1;
};
