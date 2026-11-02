#include "stitchWindowAligns.h"
#include "blocksOverlap.h"
#include "ErrorWarning.h"
#include "binarySearch2.h"
#include <cmath>
#include <ctime>
#include "SeedDiagnostics.h"
using namespace std;

namespace {

//extend the 5' and 3' ends of a finished transcript towards the read ends
void extendTranscriptEnds(Transcript &trA, int &Score, uint64 &tR2, uint64 &tG2, uint64 Lread, char* R, Genome &mapGen, \
                          Parameters& P, const WindowAlignmentContext& context) {
    Transcript trAstep1;

    int vOrder[2]; //decide in which order to extend: extend the 5' of the read first

    #if EXTEND_ORDER==1
    if ( trA.roStr==0 ) {//decide in which order to extend: extend the 5' of the read first
        vOrder[0]=0; vOrder[1]=1;
    } else {
        vOrder[0]=1; vOrder[1]=0;
    };
    #elif EXTEND_ORDER==2
        vOrder[0]=0; vOrder[1]=1;
    #else
        #error "EXTEND_ORDER value unrecognized"
    #endif

    for (int iOrd=0;iOrd<2;iOrd++) {

        switch (vOrder[iOrd]) {

        case 0: //extend at start

        if (trA.rStart>0) {// if transcript does not start at base, extend to the read start
            trAstep1.reset();
            uint64 imate=trA.exons[0][EX_iFrag];
            if ( extendAlign(R, mapGen.G, trA.rStart-1, trA.gStart-1, -1, -1, trA.rStart, tR2-trA.rStart+1, \
                             trA.nMM, context.mismatchLimit, P.alignFilter.mismatchOverLengthMax, \
                             P.align.endsType.ext[imate][(int)(trA.Str!=imate)], &trAstep1) ) {//if could extend

                trA.add(&trAstep1);
                Score += trAstep1.maxScore;

                trA.exons[0][EX_R] = trA.rStart = trA.rStart - trAstep1.extendL;
                trA.exons[0][EX_G] = trA.gStart = trA.gStart - trAstep1.extendL;
                trA.exons[0][EX_L] += trAstep1.extendL;

            };
        //TODO penalize the unmapped bases at the start
        };
        break;

        case 1: //extend at end

        if ( tR2<Lread ) {//extend alignment to the read end
            trAstep1.reset();
            uint64 imate=trA.exons[trA.nExons-1][EX_iFrag];
            if ( extendAlign(R, mapGen.G, tR2+1, tG2+1, +1, +1, Lread-tR2-1, tR2-trA.rStart+1, \
                             trA.nMM, context.mismatchLimit,  P.alignFilter.mismatchOverLengthMax, \
                             P.align.endsType.ext[imate][(int)(imate==trA.Str)], &trAstep1) ) {//if could extend

                trA.add(&trAstep1);
                Score += trAstep1.maxScore;

                tR2 += trAstep1.extendL;
                tG2 += trAstep1.extendL;

                trA.exons[trA.nExons-1][EX_L] += trAstep1.extendL;//extend the length of the last exon

            };
        //TODO penalize unmapped bases at the end
        };
    };
    };
};

//false if the read would be soft-clipped past the ends of the chromosome and that is not allowed
bool softClipWithinChromosome(const Transcript &trA, uint64 Lread, Genome &mapGen, Parameters& P) {
    if (!P.align.softClipAtReferenceEnds.yes &&  \
            ( (trA.exons[trA.nExons-1][EX_G] + Lread-trA.exons[trA.nExons-1][EX_R]) > (mapGen.chrStart[trA.Chr]+mapGen.chrLength[trA.Chr]) || \
               trA.exons[0][EX_G]<(mapGen.chrStart[trA.Chr]+trA.exons[0][EX_R]) ) ) {
        return false; //no soft clipping past the ends of the chromosome
    };
    return true;
};

void setTranscriptLengths(Transcript &trA, uint64 tG2) {
    trA.rLength = 0;
    for (uint64 isj=0;isj<trA.nExons;isj++) {
        trA.rLength += trA.exons[isj][EX_L];
    };
    trA.gLength = tG2+1-trA.gStart;
};

//check exons lengths including repeats, do not report a transcript with short exons
bool junctionOverhangsPass(const Transcript &trA, Parameters& P) {
    for (uint64 isj=0;isj<trA.nExons-1;isj++) {//check exons for min length, if they are not annotated and precede a junction
        if ( trA.canonSJ[isj]>=0 ) {//junction
            if (trA.sjAnnot[isj]==1) {//sjdb
                if (  ( trA.exons[isj][EX_L]   < P.align.SJDBoverhangMin && (isj==0            || trA.canonSJ[isj-1]==-3 || (trA.sjAnnot[isj-1]==0 && trA.canonSJ[isj-1]>=0) ) )\
                   || ( trA.exons[isj+1][EX_L] < P.align.SJDBoverhangMin && (isj==trA.nExons-2 || trA.canonSJ[isj+1]==-3 || (trA.sjAnnot[isj+1]==0 && trA.canonSJ[isj+1]>=0) ) ) )return false;
            } else {//non-sjdb
                if (  trA.exons[isj][EX_L] < P.align.SJoverhangMin + trA.shiftSJ[isj][0] \
                   || trA.exons[isj+1][EX_L] < P.align.SJoverhangMin + trA.shiftSJ[isj][1]   ) return false;
            };
        };
    };
    if (trA.nExons>1 && trA.sjAnnot[trA.nExons-2]==1 && trA.exons[trA.nExons-1][EX_L] < P.align.SJDBoverhangMin) return false; //this exon was not checkedin the cycle above
    return true;
};

//set the junction motif strand, then filter strand consistency and intron motifs
bool junctionMotifsPass(Transcript &trA, Parameters& P) {
    uint64 sjN=0;
    trA.intronMotifs[0]=0;trA.intronMotifs[1]=0;trA.intronMotifs[2]=0;
    trA.sjYes=false;
    for (uint64 iex=0;iex<trA.nExons-1;iex++) {
        if (trA.canonSJ[iex]>=0)
        {//junctions - others are indels
            sjN++;
            trA.intronMotifs[trA.sjStr[iex]]++;
            trA.sjYes=true;
        };
    };

    if (trA.intronMotifs[1]>0 && trA.intronMotifs[2]==0)
        trA.sjMotifStrand=1;
    else if (trA.intronMotifs[1]==0 && trA.intronMotifs[2]>0)
        trA.sjMotifStrand=2;
    else
        trA.sjMotifStrand=0;

    if (trA.intronMotifs[1]>0 && trA.intronMotifs[2]>0 && P.outFilterIntronStrands=="RemoveInconsistentStrands")
            return false;

    if (sjN>0 && trA.sjMotifStrand==0 && P.outSAMstrandField.type==1) {//strand not defined for a junction
        return false;
    };

    if (P.outFilterIntronMotifs=="None") {//no filtering

    } else if (P.outFilterIntronMotifs=="RemoveNoncanonical") {
        for (uint64 iex=0;iex<trA.nExons-1;iex++) {
            if (trA.canonSJ[iex]==0) return false;
        };
    } else if (P.outFilterIntronMotifs=="RemoveNoncanonicalUnannotated") {
        for (uint64 iex=0;iex<trA.nExons-1;iex++) {
            if (trA.canonSJ[iex]==0 && trA.sjAnnot[iex]==0) return false;
        };
    } else {
        ostringstream errOut;
        errOut << "EXITING because of FATAL INPUT error: unrecognized value of --outFilterIntronMotifs=" <<P.outFilterIntronMotifs <<"\n";
        errOut << "SOLUTION: re-run STAR with --outFilterIntronMotifs = None -OR- RemoveNoncanonical -OR- RemoveNoncanonicalUnannotated\n";
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    };
    return true;
};

//check mapped length for each mate
bool splicedMateLengthsPass(const Transcript &trA, Parameters& P, const WindowAlignmentContext& context) {
    uint64 nsj=0,exl=0;
    for (uint64 iex=0;iex<trA.nExons;iex++) {//
        exl+=trA.exons[iex][EX_L];
        if (iex==trA.nExons-1 || trA.canonSJ[iex]==-3) {//mate is completed, make the checks
            if (nsj>0 && (exl<P.align.splicedMateMapLmin || exl < (uint64) (P.align.splicedMateMapLminOverLmate*context.readLength[trA.exons[iex][EX_iFrag]])) ) {
                return false; //do not record this transcript
            };
            exl=0;nsj=0;
        } else if (trA.canonSJ[iex]>=0) {
            nsj++;
        };
    };
    return true;
};

bool novelJunctionsPass(const Transcript &trA, Parameters& P) {
    if (P.outFilterBySJoutStage==2) {//junctions have to be present in the filtered set P.sjnovel
        for (uint64 iex=0;iex<trA.nExons-1;iex++) {
            if (trA.canonSJ[iex]>=0 && trA.sjAnnot[iex]==0) {
                uint64 jS=trA.exons[iex][EX_G]+trA.exons[iex][EX_L];
                uint64 jE=trA.exons[iex+1][EX_G]-1;
                if ( binarySearch2(jS,jE,P.sjNovelStart,P.sjNovelEnd,P.sjNovelN) < 0 ) return false;
            };
        };
    };
    return true;
};

bool mateOverlapConsistent(const Transcript &trA, uint64 Lread, Parameters& P) {
    if ( trA.exons[0][EX_iFrag]!=trA.exons[trA.nExons-1][EX_iFrag] ) {//check for correct overlap between mates
        if (trA.exons[trA.nExons-1][EX_G]+trA.exons[trA.nExons-1][EX_L] <= trA.exons[0][EX_G]) return false; //to avoid negative insert size
        uint64 iexM2=trA.nExons;
        for (uint64 iex=0;iex<trA.nExons-1;iex++) {//find the first exon of the second mate
            if (trA.canonSJ[iex]==-3) {//
                iexM2=iex+1;
                break;
            };
        };

        if ( trA.exons[iexM2-1][EX_G] + trA.exons[iexM2-1][EX_L] > trA.exons[iexM2][EX_G] ) {//mates overlap - check consistency of junctions

            if (trA.exons[0][EX_G] > \
                trA.exons[iexM2][EX_G]+trA.exons[0][EX_R]+P.align.endsProtrude.nBasesMax) return false; //LeftMateStart > RightMateStart + allowance
            if (trA.exons[iexM2-1][EX_G]+trA.exons[iexM2-1][EX_L] > \
               trA.exons[trA.nExons-1][EX_G]+Lread-trA.exons[trA.nExons-1][EX_R]+P.align.endsProtrude.nBasesMax) return false; //LeftMateEnd   > RightMateEnd +allowance

            //check for junctions consistency
            uint64 iex1=1, iex2=iexM2+1; //last exons of the junction
            for  (; iex1<iexM2; iex1++) {//find first junction that overlaps 2nd mate
                if (trA.exons[iex1][EX_G] >= trA.exons[iex2-1][EX_G] + trA.exons[iex2-1][EX_L]) break;
            };
            while (iex1<iexM2 && iex2<trA.nExons) {//cycle through all overlapping exons
                if (trA.canonSJ[iex1-1]<0) {//skip non-junctions
                    iex1++;
                    continue;
                };
                if (trA.canonSJ[iex2-1]<0) {//skip non-junctions
                    iex2++;
                    continue;
                };

                if ( ( trA.exons[iex1][EX_G]!=trA.exons[iex2][EX_G] ) || ( (trA.exons[iex1-1][EX_G]+trA.exons[iex1-1][EX_L]) != (trA.exons[iex2-1][EX_G]+trA.exons[iex2-1][EX_L]) ) ) {
                    return false; //inconsistent junctions on overlapping mates
                };
                iex1++;
                iex2++;

            };//cycle through all overlapping exons
        };//mates overlap - check consistency of junctions
    };//check for correct overlap between mates
    return true;
};

//add the genomic length and variation scores, record the final values for the transcript
void finalizeTranscriptScore(Transcript &trA, int &Score, uint64 Lread, char* R, Genome &mapGen, \
                             Parameters& P, const WindowAlignmentContext& context) {
    if (P.score.genomicLengthLog2scale!=0) {//add gap length score
        Score += int(ceil( log2( (double) ( trA.exons[trA.nExons-1][EX_G]+trA.exons[trA.nExons-1][EX_L] - trA.exons[0][EX_G]) ) \
                 * P.score.genomicLengthLog2scale - 0.5));
        Score = max(0,Score);
    };

    //calculate some final values for the transcript

    trA.roStart = (trA.roStr == 0) ? trA.rStart : Lread - trA.rStart - trA.rLength;
    trA.maxScore=Score;

    if (trA.exons[0][EX_iFrag]==trA.exons[trA.nExons-1][EX_iFrag]) {//mark single fragment transcripts
        trA.iFrag=trA.exons[0][EX_iFrag];
        context.maxScoreMate[trA.iFrag] = max (context.maxScoreMate[trA.iFrag] , Score);
    } else {
        trA.iFrag=-1;
    };

    //Variation
    Score+=trA.variationAdjust(mapGen, R);

    trA.maxScore=Score;
};

// transcript has been finalized, compare the score and record
void recordWindowTranscript(Transcript &trA, int Score, Parameters& P, Transcript** wTr, uint64* nWinTr, \
                            const WindowAlignmentContext& context) {
    if (       Score+P.alignFilter.multimapScoreRange >= wTr[0]->maxScore \
            || ( trA.iFrag>=0 && Score+P.alignFilter.multimapScoreRange >= context.maxScoreMate[trA.iFrag] ) \
            || P.pCh.segmentMin>0) {
            //only record the transcripts within the window that are in the Score range
            //OR within the score range of each mate
            //OR all transcript if chimeric detection is activated

        uint64 iTr=0; //transcript insertion/replacement place

        trA.mappedLength=0;
        for (uint64 iex=0;iex<trA.nExons;iex++) {//caclulate total mapped length
            trA.mappedLength += trA.exons[iex][EX_L];
        };

        while (iTr < *nWinTr) {//scan through all recorded transcripts for this window - check for duplicates

            //another way to calculate uOld, uNew: w/o gMap
            uint64 nOverlap=blocksOverlap(trA,*wTr[iTr]);
            uint64 uNew=trA.mappedLength-nOverlap;
            uint64 uOld=wTr[iTr]->mappedLength-nOverlap;

            if (uNew==0 && Score < wTr[iTr]->maxScore) {//new transript is a subset of the old ones
                break;
            } else if (uOld==0) {//old transcript is a subset of the new one, remove old transcript
                Transcript *pTr=wTr[iTr];
                for  (uint64 ii=iTr+1;ii<*nWinTr;ii++) wTr[ii-1]=wTr[ii]; //shift transcripts
                (*nWinTr)--;
                wTr[*nWinTr]=pTr;
            } else if (uOld>0 && (uNew>0 || Score >= wTr[iTr]->maxScore) ) {//check next transcript
                iTr++;
            };

        };

        if (iTr==*nWinTr) {//insert the new transcript
            for (iTr=0;iTr<*nWinTr;iTr++) {//find inseriton location
                if (Score>wTr[iTr]->maxScore || (Score==wTr[iTr]->maxScore && trA.gLength<wTr[iTr]->gLength) ) break;
            };

            Transcript *pTr=wTr[*nWinTr];
            for (int ii=*nWinTr; ii> int(iTr); ii--) {//shift all the transcript pointers down from iTr
                wTr[ii]=wTr[ii-1];
            };
            wTr[iTr]=pTr; //the new transcript pointer is now at *nWinTr+1, move it into the iTr
            *(wTr[iTr])=trA;
            if (*nWinTr<P.align.transcriptsPerWindowNmax) {
                (*nWinTr)++; //increment number of transcripts per window;
            } else {
            };
        };
    };
};

//no more aligns to add: extend, filter, score and record the transcript
void finalizeWindowTranscript(int Score, uint64 tR2, uint64 tG2, Transcript &trA, \
                              uint64 Lread, char* R, Genome &mapGen, \
                              Parameters& P, Transcript** wTr, uint64* nWinTr, const WindowAlignmentContext& context) {
    extendTranscriptEnds(trA, Score, tR2, tG2, Lread, R, mapGen, P, context);

    if (!softClipWithinChromosome(trA, Lread, mapGen, P)) return;

    setTranscriptLengths(trA, tG2);

    if (!junctionOverhangsPass(trA, P)) return;
    if (!junctionMotifsPass(trA, P)) return;
    if (!splicedMateLengthsPass(trA, P, context)) return;
    if (!novelJunctionsPass(trA, P)) return;
    if (!mateOverlapConsistent(trA, Lread, P)) return;

    finalizeTranscriptScore(trA, Score, Lread, R, mapGen, P, context);

    recordWindowTranscript(trA, Score, P, wTr, nWinTr, context);
};

// Skip the expensive Transcript copy when stitchAlignToTranscript would definitely
// reject this alignment. These mirror the early-return checks in stitchAlignToTranscript:
//   line 12: nExons >= MAX_N_EXONS → return -1000010
//   line 53: rBend <= rAend (full read overlap, same fragment) → return -1000001
//   line 54: gBend <= gAend (full genome overlap, same fragment) → return -1000002
// The exclude branch still runs — output is identical to the non-optimized path.
bool alignCannotBeIncluded(uint64 iA, uint64 tR2, uint64 tG2, const Transcript &trA, uiWA* WA) {
    bool skipInclude = false;
    if (trA.nExons>0) {
        if (trA.nExons>=MAX_N_EXONS) {
            skipInclude = true;
        } else if (WA[iA][WA_iFrag] == trA.exons[trA.nExons-1][EX_iFrag]) {
            uint64 rBend = WA[iA][WA_rStart] + WA[iA][WA_Length] - 1;
            uint64 gBend = WA[iA][WA_gStart] + WA[iA][WA_Length] - 1;
            if (rBend <= tR2 || gBend <= tG2)
                skipInclude = true;
        }
    }
    return skipInclude;
};

//this is the first align in the transcript: originate the transcript, return its score
int startTranscriptWithAlign(uint64 iA, uint64 nA, bool WAincl[], uiWA* WA, Transcript &trAi) {
    int dScore=0;
    trAi.exons[0][EX_R]=trAi.rStart=WA[iA][WA_rStart]; //transcript start/end
    trAi.exons[0][EX_G]=trAi.gStart=WA[iA][WA_gStart];
    trAi.exons[0][EX_L]=WA[iA][WA_Length];
    trAi.exons[0][EX_iFrag]=WA[iA][WA_iFrag];
    trAi.exons[0][EX_sjA]=WA[iA][WA_sjA];

    trAi.nExons=1; //recorded first exon

    for (uint64 ii=0;ii<WA[iA][WA_Length];ii++) dScore+=scoreMatch; //sum all the scores

    trAi.nMatch=WA[iA][WA_Length]; //# of matches

    for (uint64 ii=0; ii<nA; ii++) WAincl[ii]=false;
    return dScore;
};

} // namespace

void stitchWindowAligns(uint64 iA, uint64 nA, int Score, bool WAincl[], uint64 tR2, uint64 tG2, Transcript trA, \
                        uint64 Lread, uiWA* WA, char* R, Genome &mapGen, \
                        Parameters& P, Transcript** wTr, uint64* nWinTr, const WindowAlignmentContext& context) {
    //recursively stitch aligns for one gene
    #ifdef STAR_CAPTURE_SEEDS
    if(auto* w=seedTrace::worker()) ++w->recursiveStates;
    #endif
    //*nWinTr - number of transcripts for the current window

    if (iA>=nA && tR2==0) return; //no aligns in the transcript

    if (iA>=nA) {//no more aligns to add, finalize the transcript
        finalizeWindowTranscript(Score, tR2, tG2, trA, Lread, R, mapGen, P, wTr, nWinTr, context);
        return;
    };

    ///////////////////////////////////////////////////////////////////////////////////
    // Do not prune on remaining seed lengths: terminal 5'/3' extension and
    // final score adjustments are not bounded by those lengths. A suffix cache
    // would accelerate the same unproven bound, not make it safe.

    int dScore=0;

    if (!alignCannotBeIncluded(iA, tR2, tG2, trA, WA)) {
        Transcript trAi=trA; //trA copy with this align included, to be used in the 1st recursive call of StitchAlign
        if (trA.nExons>0) {//stitch, a transcript has already been originated

            #ifdef STAR_CAPTURE_SEEDS
            if(auto* w=seedTrace::worker()) ++w->transitions;
            #endif
            dScore=stitchAlignToTranscript(tR2, tG2, WA[iA][WA_rStart], WA[iA][WA_gStart], WA[iA][WA_Length], WA[iA][WA_iFrag],  WA[iA][WA_sjA], P, R, mapGen, &trAi, context.mismatchLimit);
            //TODO check if the new stitching creates too many MM, quit this transcript if so

        } else { //this is the first align in the transcript
            dScore=startTranscriptWithAlign(iA, nA, WAincl, WA, trAi);
        };

        if (dScore>-1000000) {//include this align
            WAincl[iA]=true;

            if ( WA[iA][WA_Nrep]==1 ) trAi.nUnique++; //unique piece
            if ( WA[iA][WA_Anchor]>0 ) trAi.nAnchor++; //anchor piece piece

            stitchWindowAligns(iA+1, nA, Score+dScore, WAincl, WA[iA][WA_rStart]+WA[iA][WA_Length]-1, WA[iA][WA_gStart]+WA[iA][WA_Length]-1, trAi, Lread, WA, R, mapGen, P, wTr, nWinTr, context);
        };
    }

    //also run a transcript w/o including this align
    if (WA[iA][WA_Anchor]!=2 || trA.nAnchor>0) {//only allow exclusion if this is not the last anchor, or other anchors have been used
        WAincl[iA]=false;
        stitchWindowAligns(iA+1, nA, Score, WAincl, tR2, tG2, trA, Lread, WA, R, mapGen, P, wTr, nWinTr, context);
    };
    return;
};

