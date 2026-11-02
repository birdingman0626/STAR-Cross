#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "extendAlign.h"
#include "binarySearch2.h"
using namespace std;

// stitchAlignToTranscript appends align B (rBstart/gBstart, length L, fragment iFragB) to transcript trA,
// whose last exon ends at rAend/gAend. It returns the score of B and the stitched gap, or a negative
// rejection code: -1000001..-1000010. StitchCore.h holds the device port of this function.

namespace {

// Gap between A and B on one fragment, as scored by the gap-type step.
struct GapStitch {
    int Score=0;
    uint64 nMatch=0, nMM=0;
    uint64 Del=0, Ins=0, nIns=0, nDel=0;
    int jR=0; //junction location in R-space: the last base of the donor
    int jCan=999; //canonical junction type
};

// sjAB is an annotated junction joining A and B directly. Returns 0 or a rejection code; Score receives B's score.
int stitchAnnotatedJunction(int &Score, uint64 rBstart, uint64 gBstart, uint64 L, uint64 sjAB, Parameters& P, Genome &mapGen, Transcript *trA)
{
    if (mapGen.sjdbMotif[sjAB]==0 && (L<=mapGen.sjdbShiftRight[sjAB] || trA->exons[trA->nExons-1][EX_L]<=mapGen.sjdbShiftLeft[sjAB]) ) {
        return -1000006; //too large repeats around non-canonical junction
    };
    trA->exons[trA->nExons][EX_L] = L; //new exon length
    trA->exons[trA->nExons][EX_R] = rBstart; //new exon r-start
    trA->exons[trA->nExons][EX_G] = gBstart; //new exon g-start
    trA->canonSJ[trA->nExons-1]=mapGen.sjdbMotif[sjAB]; //mark sj-db
    trA->shiftSJ[trA->nExons-1][0]=mapGen.sjdbShiftLeft[sjAB];
    trA->shiftSJ[trA->nExons-1][1]=mapGen.sjdbShiftRight[sjAB];
    trA->sjAnnot[trA->nExons-1]=1;
    trA->sjStr[trA->nExons-1]=mapGen.sjdbStrand[sjAB];
    trA->nExons++;
    trA->nMatch+=L;
    for (uint64 ii=rBstart;ii<rBstart+L;ii++) Score+=scoreMatch; //add QS for mapped portions
    Score+=P.pGe.sjdbScore;
    return 0;
}

// equal read and genome gaps: fill the space base by base, no indel
void stitchEqualGap(GapStitch &s, const char *R, const char *G, uint64 rAend, uint64 gAend, int rGap)
{
    for (int ii=1;ii<=rGap;ii++) {
        if (G[gAend+ii]<4 && R[rAend+ii]<4) {//only score genome bases that are not Ns
            if ( R[rAend+ii]==G[gAend+ii] ) {
                s.Score+=scoreMatch;
                s.nMatch++;
            } else {
                s.Score-=scoreMatch;
                s.nMM++;
            };
        };
    };
}

// motif of the intron that follows donor base jR1: 1..6 canonical/semi-canonical, 0 non-canonical; jPen1 its penalty
inline int intronMotif(const char *G, uint64 gAend, uint64 gBstart1, int jR1, const Parameters &P, int &jPen1)
{
    jPen1=0;
    if ( G[gAend+jR1+1]==2 && G[gAend+jR1+2]==3 && G[gBstart1+jR1-1]==0 && G[gBstart1+jR1]==2 ) {//GTAG
        return 1;
    } else if ( G[gAend+jR1+1]==1 && G[gAend+jR1+2]==3 && G[gBstart1+jR1-1]==0 && G[gBstart1+jR1]==1 ) {//CTAC
        return 2;
    } else if ( G[gAend+jR1+1]==2 && G[gAend+jR1+2]==1 && G[gBstart1+jR1-1]==0 && G[gBstart1+jR1]==2 ) {//GCAG
        jPen1=P.score.gapGCAG;
        return 3;
    } else if ( G[gAend+jR1+1]==1 && G[gAend+jR1+2]==3 && G[gBstart1+jR1-1]==2 && G[gBstart1+jR1]==1 ) {//CTGC
        jPen1=P.score.gapGCAG;
        return 4;
    } else if ( G[gAend+jR1+1]==0 && G[gAend+jR1+2]==3 && G[gBstart1+jR1-1]==0 && G[gBstart1+jR1]==1 ) {//ATAC
        jPen1=P.score.gapATAC;
        return 5;
    } else if ( G[gAend+jR1+1]==2 && G[gAend+jR1+2]==3 && G[gBstart1+jR1-1]==0 && G[gBstart1+jR1]==3 ) {//GTAT
        jPen1=P.score.gapATAC;
        return 6;
    };
    jPen1=P.score.gapNoncan;
    return 0;
}

// genome gap larger than read gap: a deletion or a junction. Finds the junction position jR,
// its repeat shifts, annotation and score. Returns 0 or a rejection code.
int stitchDeletion(GapStitch &s, const char *R, const char *G, uint64 rAend, uint64 gAend, uint64 rBend, uint64 gBstart1,
                   uint64 L, int rGap, int gGap, Parameters &P, Genome &mapGen, Transcript *trA)
{
    s.nDel=1;
    s.Del=gGap-rGap; //gGap>0 here

    if (s.Del>P.align.intronMax && P.align.intronMax>0) {
        return -1000003; //large gaps not allowed
    };

    int Score1=0;
    int jR1=1; //junction location in R-space
    do { // 1. move left, until the score for MM is less than canonical advantage
        jR1--;
        if ( R[rAend+jR1]!=G[gBstart1+jR1] && G[gBstart1+jR1]<4 && R[rAend+jR1]==G[gAend+jR1]) Score1 -= scoreMatch;
    }  while ( Score1+P.score.stitchSJshift >= 0 && int(trA->exons[trA->nExons-1][EX_L]) + jR1 > 1);//also check that we are still within the exon

    int maxScore2=-999999;
    Score1=0;
    int jPen=0;
    do { // 2. scan to the right to find the best junction locus
        if  ( R[rAend+jR1]==G[gAend+jR1] && R[rAend+jR1]!=G[gBstart1+jR1] )  Score1+=scoreMatch;
        if  ( R[rAend+jR1]!=G[gAend+jR1] && R[rAend+jR1]==G[gBstart1+jR1] )  Score1-=scoreMatch;

        int jCan1=-1; //this marks Deletion
        int jPen1=0;
        int Score2=Score1;

        if (s.Del>=P.align.intronMin) {//only check intron motif for large gaps= non-Dels
            jCan1=intronMotif(G, gAend, gBstart1, jR1, P, jPen1);
            Score2 += jPen1;
        };

        if (maxScore2 < Score2 ) {//check if the score is the highest
            maxScore2=Score2;
            s.jR=jR1; //this is the last base of donor
            s.jCan=jCan1;
            jPen=jPen1;
        };
        jR1++;
    } while ( jR1 < int(rBend) - int(rAend) );

    //repeat length: go back and forth around jR to find repeat length
    int &jR=s.jR;
    uint64 jjL=0,jjR=0;
    while ( gAend+jR>=jjL && G[gAend-jjL+jR]==G[gBstart1-jjL+jR] && G[gAend-jjL+jR]<4 && jjL<=MAX_SJ_REPEAT_SEARCH) {//go back
        jjL++;
    };

    while ( gAend+jjR+jR+1<mapGen.nGenome && G[gAend+jjR+jR+1]==G[gBstart1+jjR+jR+1] && G[gAend+jjR+jR+1]<4 && jjR<=MAX_SJ_REPEAT_SEARCH) {//go forward
        jjR++;
    };

    if (s.jCan<=0) {//flush deletions and non-canonical junction to the left
        jR-=jjL;
        if (int(trA->exons[trA->nExons-1][EX_L])+jR<1) return -1000005;
        jjR+=jjL;
        jjL=0;
    };

    for (int ii=min(1,jR+1);ii<=max(rGap,jR);ii++) {//score donor and acceptor
        uint64 g1=(ii<=jR) ? (gAend+ii):(gBstart1+ii);
        if (G[g1]<4 && R[rAend+ii]<4) {//only penalize non-N bases
            if ( R[rAend+ii]==G[g1] ) {
                if (ii>=1 && ii <=rGap) {//only add +score and matches within the gap
                    s.Score+=scoreMatch;
                    s.nMatch++;
                };
            } else {//add -score and MM for all bases
                s.Score-=scoreMatch;
                s.nMM++;
                if (ii<1 || ii>rGap) {//subtract previuosly presumed matches
                    s.Score-=scoreMatch;
                    s.nMatch--;
                };
            };
        };
    };

    //score the gap
    if (mapGen.sjdbN>0) {//check if the junction is annotated
        uint64 jS=gAend+jR+1, jE=gBstart1+jR;//intron start/end
        int sjdbInd=binarySearch2(jS,jE,mapGen.sjdbStart,mapGen.sjdbEnd,mapGen.sjdbN);
        if (sjdbInd<0) {
            if (s.Del>=P.align.intronMin) {
                s.Score += P.score.gap + jPen; //genome gap penalty + non-canonical penalty
            } else {//deletion
                s.Score += s.Del*P.score.delBase + P.score.delOpen;
                s.jCan=-1;
                trA->sjAnnot[trA->nExons-1]=0;
            };
        } else {//annotated
            s.jCan=mapGen.sjdbMotif[sjdbInd];
            if (mapGen.sjdbMotif[sjdbInd]==0) {//shift to match annotations
                if (L<=mapGen.sjdbShiftLeft[sjdbInd] || trA->exons[trA->nExons-1][EX_L]<=mapGen.sjdbShiftLeft[sjdbInd]) {
                    //this is not needed, the check below rAend+jR >= rBend is enough
                    //it changes the results slightly, so will keep for now. Will remove in STAR3
                    return -1000006;
                };
                jR += (int) mapGen.sjdbShiftLeft[sjdbInd];
                if ( rAend+jR >= rBend ) {//jR is shifted past rBend
                    return -1000006;
                };
                jjL=mapGen.sjdbShiftLeft[sjdbInd];
                jjR=mapGen.sjdbShiftRight[sjdbInd];
            };
            trA->sjAnnot[trA->nExons-1]=1;
            trA->sjStr[trA->nExons-1]=mapGen.sjdbStrand[sjdbInd];
            s.Score += P.pGe.sjdbScore;
        };
    } else {//no annotation
        if (s.Del>=P.align.intronMin) {//junction, not short deletion
            s.Score += P.score.gap + jPen;
        } else {
            s.Score += s.Del*P.score.delBase + P.score.delOpen;
            s.jCan=-1;
            trA->sjAnnot[trA->nExons-1]=0;
        };
    };

    trA->shiftSJ[trA->nExons-1][0]=jjL;
    trA->shiftSJ[trA->nExons-1][1]=jjR;
    trA->canonSJ[trA->nExons-1]=s.jCan;

    if (trA->sjAnnot[trA->nExons-1]==0) {//strand for unannotated junctions
        if (s.jCan>0) {
             trA->sjStr[trA->nExons-1]=2-s.jCan%2; //1=+,2=-
        } else {
             trA->sjStr[trA->nExons-1]=0;
        };
    };
    return 0;
}

// read gap larger than genome gap: an insertion. Returns 0 or a rejection code.
int stitchInsertion(GapStitch &s, const char *R, const char *G, uint64 rAend, uint64 gAend, uint64 rBend, int rGap, int gGap, Parameters &P)
{
    s.Ins=rGap-gGap;
    s.nIns=1;
    int &jR=s.jR;
    if (gGap==0) {//simple insertion, no need to stitch
        jR=0;
    } else if (gGap<0) {//overlapping seeds: reduce the score
        jR=0;
        for (int ii=0; ii<-gGap; ii++) {
            s.Score -= scoreMatch;
        };
    } else {//stitch: define the exon boundary jR
        const uint64 Ins=s.Ins;
        int Score1=0; int maxScore1=0;
        for (int jR1=1;jR1<=gGap;jR1++) {//scan to the right to find the best score
            if (G[gAend+jR1]<4) {//only penalize good genome bases
                Score1+=( R[rAend+jR1]==G[gAend+jR1] ) ? scoreMatch:-scoreMatch;
                Score1+=( R[rAend+Ins+jR1]==G[gAend+jR1] ) ? -scoreMatch:+scoreMatch;
            };

            if (Score1>maxScore1 || (Score1==maxScore1 && P.align.insertionFlush.flushRight)) {//equal sign (>=) flushes insertions to the right
                maxScore1=Score1;
                jR=jR1;
            };
        };
        for (int ii=1;ii<=gGap;ii++) {//score donor and acceptor
            uint64 r1=rAend+ii+(ii<=jR ? 0:Ins);
            if (G[gAend+ii]<4 && R[r1]<4) {
                if ( R[r1]==G[gAend+ii] ) {
                    s.Score+=scoreMatch;
                    s.nMatch++;
                } else {//add -score and MM for all bases
                    s.Score-=scoreMatch;
                    s.nMM++;
                };
            };
        };
    };

    if (P.align.insertionFlush.flushRight) {
        for (; jR<(int)rBend-(int)rAend-(int)s.Ins; jR++ ){//flush the indel to the right as much as possible
            if (R[rAend+jR+1]!=G[gAend+jR+1] || G[gAend+jR+1]==4) {
                break;
            };
        };
        if (jR==(int)rBend-(int)rAend-(int)s.Ins) {//nothing left of the B-piece
            return -1000009;
        };
    };
    s.Score += s.Ins*P.score.insBase + P.score.insOpen;
    s.jCan=-2; //marks insertion though it's not used below
    return 0;
}

// A and B on the same fragment: remove the overlap, score the gap by its type, then accept it into trA.
// Returns 0 or a rejection code; Score receives B's score.
int stitchSameFragment(int &Score, uint64 rAend, uint64 gAend, uint64 rBstart, uint64 gBstart, uint64 L, uint64 iFragB,
                       Parameters& P, const char* R, Genome &mapGen, Transcript *trA, const uint64 outFilterMismatchNmaxTotal)
{
    const char *G=mapGen.G;
    uint64 gBend=gBstart+L-1;
    uint64 rBend=rBstart+L-1;

    //check if r-overlapping fully and exit
    if (rBend<=rAend) return -1000001;
    if (gBend<=gAend && trA->exons[trA->nExons-1][EX_iFrag]==iFragB) return -1000002;

    //shift the B 5' if overlaps A 3'
    if (rBstart<=rAend) {
        gBstart+=rAend-rBstart+1;
        rBstart=rAend+1;
        L=rBend-rBstart+1;
    };

    GapStitch s;
    for (uint64 ii=rBstart;ii<=rBend;ii++) s.Score+=scoreMatch; //add QS for mapped portions

    int gGap=gBstart-gAend-1; //could be < 0 for insertions
    int rGap=rBstart-rAend-1;//>0 always since we removed overlap

    s.nMatch=L;
    uint64 gBstart1=gBstart-rGap-1;//the last base of the intron if all read gap belongs to acceptor, i.e. jR=0

    // check all the different combinations of gGap and rGap
    int status=0;
    if ( gGap==0 && rGap==0 ) {//just joined the pieces, w/o stiching or gaps
    } else if ( gGap>0 && rGap>0 && rGap==gGap ) {//no gaps, just try to fill space
        stitchEqualGap(s, R, G, rAend, gAend, rGap);
    } else if ( gGap>rGap ) {//genomic gap (Deletion)
        status=stitchDeletion(s, R, G, rAend, gAend, rBend, gBstart1, L, rGap, gGap, P, mapGen, trA);
    } else if ( rGap>gGap ) {//insertion: if also gGap>0, need to stitch
        status=stitchInsertion(s, R, G, rAend, gAend, rBend, rGap, gGap, P);
    };
    Score=s.Score;
    if (status!=0) return status;

#ifdef COMPILE_FOR_LONG_READS
    if ( (trA->nMM + s.nMM)<=outFilterMismatchNmaxTotal )
#else
    if ( (trA->nMM + s.nMM)<=outFilterMismatchNmaxTotal
                 && ( s.jCan<0 || (s.jCan<7 && s.nMM<= (uint64) P.align.SJstitchMismatchNmax[(s.jCan+1)/2]) ) )
#endif
    {//stitching worked only if there no mis-matches for non-GT/AG junctions
        trA->nMM += s.nMM;
        trA->nMatch += s.nMatch;

        if (s.Del>=P.align.intronMin) {
            trA->nGap += s.nDel;
            trA->lGap += s.Del;
        } else {
            trA->nDel += s.nDel;
            trA->lDel += s.Del;
        };

        //modify exons
        const int jR=s.jR;
        if (s.Del==0 && s.Ins==0) {//no gap => no new exon, extend the boundary of the previous exon
            trA->exons[trA->nExons-1][EX_L] += rBend-rAend;
        } else if (s.Del>0) { //deletion: can only have Del>0 or Ins>0
            trA->exons[trA->nExons-1][EX_L] += jR; //correct the previous exon boundary
            trA->exons[trA->nExons][EX_L] = rBend-rAend-jR; //new exon length
            trA->exons[trA->nExons][EX_R] = rAend+jR+1; //new exon r-start
            trA->exons[trA->nExons][EX_G] = gBstart1+jR+1; //new exon g-start
            trA->nExons++;
        } else if (s.Ins>0) {
            trA->nIns += s.nIns;
            trA->lIns += s.Ins;
            trA->exons[trA->nExons-1][EX_L] += jR; //correct the previous exon boundary
            trA->exons[trA->nExons][EX_L] = rBend-rAend-jR-s.Ins; //new exon length
            trA->exons[trA->nExons][EX_R] = rAend+jR+s.Ins+1; //new exon r-start
            trA->exons[trA->nExons][EX_G] = gAend+1+jR; //new exon g-start
            trA->canonSJ[trA->nExons-1]=-2; //mark insertion
            trA->sjAnnot[trA->nExons-1]=0;
            trA->nExons++;
        };
        return 0;
    };
    return -1000007; //stitching was not accepted
}

// A and B on different fragments (mates): extend A to the right and B to the left, then join them.
// Returns 0 or a rejection code; Score receives the added score.
int stitchMates(int &Score, uint64 rAend, uint64 gAend, uint64 rBstart, uint64 gBstart, uint64 L, uint64 iFragB,
                Parameters& P, char* R, Genome &mapGen, Transcript *trA, const uint64 outFilterMismatchNmaxTotal)
{
    char *G=mapGen.G;
    if (P.align.matesGapMax>0 && gBstart > trA->exons[trA->nExons-1][EX_G] + trA->exons[trA->nExons-1][EX_L] + P.align.matesGapMax) {
        return -1000004; //gap between mates too large
    };
    //extend the fragments inside
    //note, that this always works, i.e. Score>0

    for (uint64 ii=rBstart;ii<rBstart+L;ii++) Score+=scoreMatch; //add QS for mapped portions

    Transcript trExtend;

    //TODO: compare extensions to the left and right, pick the best one to be performed first
    //otherwise if a large nMM is reached in the 1st extension, it will prevent the 2nd extension
    //use the following example:
    //>1
    //TTCTGTGTCTCCCCCTCCCCCACTGGCTACATGGAGACAGGGGGGGGGGGCCGGGCGGTTCCCGGGCAGAAAAAAA
    //>1
    //AATATTTGGAACACTTATGTGAAAAATGATTTGTTTTTCTGAAATTTACGTTTCTCTCTGAGTCCTGTAACTGTCC

    trExtend.reset();
    if ( extendAlign(R, G, rAend+1, gAend+1, 1, 1, DEF_readSeqLengthMax, trA->nMatch, trA->nMM, outFilterMismatchNmaxTotal, P.alignFilter.mismatchOverLengthMax,
                     P.align.endsType.ext[trA->exons[trA->nExons-1][EX_iFrag]][1], &trExtend) ) {
        trA->add(&trExtend);
        Score += trExtend.maxScore;
        trA->exons[trA->nExons-1][EX_L] += trExtend.extendL;
    };

    trA->exons[trA->nExons][EX_R] = rBstart;
    trA->exons[trA->nExons][EX_G] = gBstart;
    trA->exons[trA->nExons][EX_L] = L;
    trA->nMatch += L;

    trExtend.reset();
    //if end extension needs to be forced, use large length. Otherwise, only extend until the beginning of the transcript
    uint64 extlen=P.align.endsType.ext[iFragB][1] ? DEF_readSeqLengthMax : gBstart-trA->exons[0][EX_G]+trA->exons[0][EX_R];
    if ( extendAlign(R, G, rBstart-1, gBstart-1, -1, -1, extlen, trA->nMatch, trA->nMM, outFilterMismatchNmaxTotal, P.alignFilter.mismatchOverLengthMax,
                     P.align.endsType.ext[iFragB][1], &trExtend) ) {
        trA->add(&trExtend);
        Score += trExtend.maxScore;
        trA->exons[trA->nExons][EX_R] -= trExtend.extendL;
        trA->exons[trA->nExons][EX_G] -= trExtend.extendL;
        trA->exons[trA->nExons][EX_L] += trExtend.extendL;
    };

    trA->canonSJ[trA->nExons-1]=-3; //mark different fragments junction
    trA->sjAnnot[trA->nExons-1]=0;

    trA->nExons++;
    return 0;
}

} // namespace

intScore stitchAlignToTranscript(uint64 rAend, uint64 gAend, uint64 rBstart, uint64 gBstart, uint64 L, uint64 iFragB, uint64 sjAB, Parameters& P, char* R, Genome &mapGen, Transcript *trA, const uint64 outFilterMismatchNmaxTotal) {
    //stitch together A and B, extend in the gap, returns max score

    if (trA->nExons>=MAX_N_EXONS)
        return -1000010;

    int Score=0;

    if (sjAB!=((uint64) -1) && trA->exons[trA->nExons-1][EX_sjA]==sjAB
            && trA->exons[trA->nExons-1][EX_iFrag]==iFragB && rBstart==rAend+1 && gAend+1<gBstart ) {//simple stitching if junction belongs to a database
        int status=stitchAnnotatedJunction(Score, rBstart, gBstart, L, sjAB, P, mapGen, trA);
        if (status!=0) return status;
    } else {//general stitching
        trA->sjAnnot[trA->nExons-1]=0;
        trA->sjStr[trA->nExons-1]=0;

        int status;
        if (trA->exons[trA->nExons-1][EX_iFrag]==iFragB) {//stitch aligns on the same fragment
            status=stitchSameFragment(Score, rAend, gAend, rBstart, gBstart, L, iFragB, P, R, mapGen, trA, outFilterMismatchNmaxTotal);
        } else if (gBstart+trA->exons[0][EX_R]+P.align.endsProtrude.nBasesMax >= trA->exons[0][EX_G] || trA->exons[0][EX_G] < trA->exons[0][EX_R]){//stitch aligns from different fragments
            //CHECK: this second condition does not make sense
            status=stitchMates(Score, rAend, gAend, rBstart, gBstart, L, iFragB, P, R, mapGen, trA, outFilterMismatchNmaxTotal);
        } else {//no stitching possible
            status=-1000008;
        };
        if (status!=0) return status;
    };

    trA->exons[trA->nExons-1][EX_iFrag]=iFragB; //the new exon belongs to fragment iFragB
    trA->exons[trA->nExons-1][EX_sjA]=sjAB;

    return Score;
};
