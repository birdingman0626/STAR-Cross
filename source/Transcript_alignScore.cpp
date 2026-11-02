#include <cmath>
#include "Transcript.h"
using namespace std;
intScore Transcript::alignScore(char **Read1, char *G, Parameters &P) {//re-calculates score and number of mismatches
    maxScore=0;
    nMM=0;
    nMatch=0;
    
    if (nExons==0)
        return maxScore;
    
    char* R=Read1[roStr==0 ? 0:2];
    for (uint64 iex=0;iex<nExons;iex++) {
        for (uint64 ii=0;ii<exons[iex][EX_L];ii++) {
            char r1=R[ii+exons[iex][EX_R]];
            char g1=G[ii+exons[iex][EX_G]];
            if (r1>3 || g1>3) {//nothing to do
            } else if (r1==g1) {//match
                ++maxScore;
                ++nMatch;
            } else {//mismatch
                ++nMM;
                --maxScore;
            };
        };
    };
    for (uint64 iex=0;iex<nExons-1;iex++) {//score junctions
        if (sjAnnot[iex]==1) {
            maxScore += P.pGe.sjdbScore;
        } else {
            switch (canonSJ[iex]) {
                case -3: //mate pair, no scoring
                    break;
                case -2: //insertion
                    maxScore += (exons[iex+1][EX_R]-exons[iex][EX_R]-exons[iex][EX_L])*P.score.insBase + P.score.insOpen;
                    break;
                case -1: //deletion
                    maxScore += (exons[iex+1][EX_G]-exons[iex][EX_G]-exons[iex][EX_L])*P.score.delBase + P.score.delOpen;
                    break;
                case 0: //non-canonical
                    maxScore += P.score.gapNoncan+P.score.gap;
                    break;
                case 1: case 2: //GTAG
                    maxScore += P.score.gap;
                    break;
                case 3: case 4: //GCAG
                    maxScore += P.score.gapGCAG+P.score.gap;
                    break;
                case 5: case 6: //ATAC
                    maxScore += P.score.gapATAC+P.score.gap;
                    break;
            };
        };
    };
    if (P.score.genomicLengthLog2scale!=0) {//add gap length score
        maxScore += int(ceil( log2( (double) ( max(1LLU,exons[nExons-1][EX_G]+exons[nExons-1][EX_L] - exons[0][EX_G]) ) ) \
                 * P.score.genomicLengthLog2scale - 0.5));
    };
    return maxScore;
};
