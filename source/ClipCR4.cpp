#include "ClipCR4.h"

ClipCR4::ClipCR4()
{
    dbN = 64;
    readLen = 91;
    gapOpen = 2;
    gapExt = 2;

    // Create scoring matrix: match=1, mismatch=-2 for ACGT; N-vs-N=0
    scoreMatrix = parasail_matrix_create("ACGTN", 1, -2);
    // N-vs-N should be 0 (neutral) instead of 1 (match)
    parasail_matrix_set_value(scoreMatrix, 4, 4, 0);

    dbSeqArr = new char[dbN * readLen];
    dbSeqs = new char*[dbN];
    for (int id = 0; id < dbN; id++) {
        dbSeqs[id] = dbSeqArr + id * readLen;
    };

    storeClip.resize(dbN);
    alignRes.resize(dbN);
};

ClipCR4::~ClipCR4()
{
    parasail_matrix_free(scoreMatrix);
    delete[] dbSeqArr;
    delete[] dbSeqs;
};

void ClipCR4::fillOneSeq(uint32 idb, char *seq, uint32 seqL)
{
    uint32 minLen = min(seqL, readLen);

    memcpy(dbSeqs[idb], seq, minLen);

    // Normalize unknown bases to 'N'
    for (uint32 ib = 0; ib < minLen; ib++) {
        switch (dbSeqs[idb][ib]) {
            case 'A': case 'C': case 'G': case 'T': case 'N':
                break;
            default:
                dbSeqs[idb][ib] = 'N';
        };
    };

    if (seqL < readLen) // fill the rest of the sequence with Ns
        memset(dbSeqs[idb] + seqL, 'N', readLen - seqL);
};

void ClipCR4::align(uint8 *query, uint32 queryLen, int dbN1)
{
    // Convert numeric query (0=A,1=C,2=G,3=T,4=N) to ASCII characters
    static const char numToChar[] = {'A', 'C', 'G', 'T', 'N'};
    uint32 qLen = min(queryLen, queryBufSize);
    for (uint32 i = 0; i < qLen; i++)
        queryCharsBuf[i] = numToChar[query[i]];

    // Build query profile once; reuse for all database sequences
    parasail_profile_t *profile = parasail_profile_create_16(
        queryCharsBuf, qLen, scoreMatrix);

    for (int idb = 0; idb < dbN1; idb++) {
        // Semi-global alignment (all ends free = overlap mode, like Opal's OPAL_MODE_OV)
        parasail_result_t *res = parasail_sg_striped_profile_16(
            profile, dbSeqs[idb], readLen, gapOpen, gapExt);

        alignRes[idb].score = parasail_result_get_score(res);
        alignRes[idb].endLocationTarget = parasail_result_get_end_ref(res);

        parasail_result_free(res);
    };

    parasail_profile_free(profile);
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
