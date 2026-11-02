#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "ReadAlign.h"
#include "SequenceFuns.h"
#include "stitchWindowAligns.h"
#include "sjAlignSplit.h"
#include "PackedArray.h"
#include "GlobalVariables.h"
#include "ThreadControl.h"
#include <time.h>
#include "SeedDiagnostics.h"
#include "SeedHitBatch.h"
#include "StitchCoreBridge.h"
#include "StitchBatch.h"
#include <algorithm>
#include <chrono>
using namespace std;

namespace {
// One hit of a stored piece, on the forward genome strand: start, strand and read start.
struct PieceHit {
    uint64 a1, aStr, aRstart;
};

inline PieceHit pieceHit(const Genome& g, const SeedHitBatch* hits, const uiPC& pc, uint64 iP, uint64 iHit, uint64 Lread, bool revertStrand)
{
    PieceHit h;
    if (hits) {
        const auto& hit=hits->hits[hits->offsets[iP]+iHit];
        h.a1=hit.genome; h.aStr=hit.strand; h.aRstart=hit.readStart;
    } else {
        const uint64 aDir=pc[PC_Dir], aLength=pc[PC_Length];
        h.a1 = g.SA[pc[PC_SAstart]+iHit];
        h.aStr = h.a1 >> g.GstrandBit;
        h.a1 &= g.GstrandMask; //remove strand bit
        h.aRstart=pc[PC_rStart];
        //convert to positive strand
        if (aDir==1 && h.aStr==0) {
            h.aStr=1;
            h.aRstart = Lread - (aLength+h.aRstart);
        } else if (aDir==0 && h.aStr==1) {
            h.aRstart = Lread - (aLength+h.aRstart);
            h.a1 = g.nGenome - (aLength+h.a1);
        } else if (aDir==1 && h.aStr==1) {
            h.aStr=0;
            h.a1 = g.nGenome - (aLength+h.a1);
        };
    }
    if (revertStrand) h.aStr=1-h.aStr; //final strand, according to user input
    return h;
}

inline uint64 pieceHitCount(const SeedHitBatch* hits, const uiPC& pc, uint64 iP)
{// prepared GPU hits may omit hits that cannot reach any window (see gpuSeedHits.cuh)
    return hits ? hits->counts.at(iP) : pc[PC_SAend]-pc[PC_SAstart]+1;
}
} // namespace

void ReadAlign::stitchPieces(char **R, uint64 Lread) {
    #ifdef STAR_CAPTURE_SEEDS
    seedTrace::Timer diagnostic(&seedTrace::Worker::stitchNs);
    #endif

#ifndef COMPILE_FOR_LONG_READS
    if (stitchInject && stitchInject->batch && !stitchcore::verifyEnabled() && takeInjectedTranscripts())
        return;
#endif

    //reset winBin — O(modified) instead of O(N) memset
    winBin[0].reset();
    winBin[1].reset();

    createWindowsFromAnchors(Lread);
    extendWindowFlanks();
    nWall=nW;

    #ifdef OFF_BEFORE_SEEDdistribution
        #warning OFF_BEFORE_SEEDdistribution
        nW=0;
        nTr=0;
        return;
    #endif

    assignAlignsToWindows(Lread);

#ifdef COMPILE_FOR_LONG_READS
    filterWindowsByCoverage();
    std::time(&timeStart); //mapping time initialize
#endif

    #ifdef OFF_BEFORE_STITCH
        #warning OFF_BEFORE_STITCH
        nW=0;
        return;
    #endif
    if (stitchCollect) {//record windows for device stitching; the read is aligned again in the inject pass
        collectStitchWindows(R, Lread);
        nW=0;
        return;
    }
    stitchAllWindows(R, Lread);

    if (trBest->maxScore==0) {//no window was aligned (could happen if for all windows too many reads are multiples)
        mapMarker = MARKER_NO_GOOD_WINDOW;
        nW=0;
        return;
    };
};

#ifndef COMPILE_FOR_LONG_READS
bool ReadAlign::takeInjectedTranscripts()
{// the device stitched this read; its windows were built in the collect pass, in this order
    const StitchReadResult& rr=stitchInject->reads.at(stitchInjectRead);
    if (rr.status!=stitchOk) return false;
    const StitchBatch& batch=*stitchInject->batch;
    const auto& heads=stitchInject->transcripts;
    // Pass 1 sets only maxScore and gLength, the fields trBest selection and multMapSelect's
    // score scan read. Pass 2 fully writes only the transcripts multMapSelect then selects
    // (same rule, same best score); the others are never read again. Batched mode rejects
    // chimeric detection and mate overlap, the other readers of trAll.
    thread_local std::vector<unsigned> windowOf;
    if (windowOf.size()<rr.windowCount) windowOf.resize(rr.windowCount);
    trBest=trInit;
    uint64 iW1=0, trNtotal=0;
    for (unsigned k=0; k<rr.windowCount; k++) {//same steps as stitchAllWindows, minus window building
        const StitchWindowResult& wr=stitchInject->windows.at(rr.windowBegin+k);
        trAll[iW1]=trArrayPointer+trNtotal;
        if (trNtotal+P.align.transcriptsPerWindowNmax >= P.align.transcriptsPerReadNmax) {
            warnTranscriptSpace();
            break;
        };
        for (unsigned j=0; j<wr.count; j++) {
            const stitchcore::TrHead& h=heads.at(rr.resultBase+wr.first+j);
            trAll[iW1][j]->maxScore=h.maxScore;
            trAll[iW1][j]->gLength=h.gLength;
        }
        nWinTr[iW1]=wr.count;
        if (nWinTr[iW1]==0) continue;
        if (trAll[iW1][0]->maxScore > trBest->maxScore || (trAll[iW1][0]->maxScore == trBest->maxScore && trAll[iW1][0]->gLength < trBest->gLength ) ) {
            trBest=trAll[iW1][0];
        };
        windowOf[iW1]=k;
        trNtotal += nWinTr[iW1];
        iW1++;
    }
    if (iW1>0 && trBest!=trInit) {
        const intScore best=trBest->maxScore;
        trA=*trInit;
        trA.maxScore=0;
        for (uint64 w=0; w<iW1; w++) {
            const unsigned k=windowOf[w];
            const StitchWindowDesc& wd=batch.windows.at(rr.windowBegin+k);
            const StitchWindowResult& wr=stitchInject->windows.at(rr.windowBegin+k);
            trA.Chr=wd.Chr;
            trA.Str=wd.Str;
            trA.roStr=wd.roStr;
            for (unsigned j=0; j<wr.count; j++) {
                Transcript& t=*trAll[w][j];
                if ( (t.maxScore + P.alignFilter.multimapScoreRange) >= best ) {
                    t=trA;
                    stitchcore::fromPacked(heads.at(rr.resultBase+wr.first+j), stitchInject->rows.data(), t);
                }
            }
        }
    }
    nW=iW1;
    for (uint64 ii=0; ii<P.readNmates; ii++) maxScoreMate[ii]=rr.mates[ii];
    ++stitchInjectedReads;
    if (trBest->maxScore==0) {
        mapMarker = MARKER_NO_GOOD_WINDOW;
        nW=0;
    };
    return true;
}
#endif

void ReadAlign::warnTranscriptSpace()
{
    OptionalMutexLock logLock(g_threadChunks.mutexLogMain, P.runThreadN>1);//Log.out is shared with other workers
    P.inOut->logMain << "WARNING: not enough space allocated for transcript. Did not process all windows for read "<< readName+1 <<endl;
    P.inOut->logMain <<"   SOLUTION: increase alignTranscriptsPerReadNmax and re-run\n" << flush;
}

void ReadAlign::createWindowsFromAnchors(uint64 Lread)
{// each anchor piece (maps few times) creates or extends windows at its loci
    nW=0; //number of windows
    for (uint64 iP=0; iP<nP; iP++) {
        if (PC[iP][PC_Nrep]>P.win.anchorMultimapNmax) continue;
        const uint64 aLength=PC[iP][PC_Length];
        const uint64 nHits=pieceHitCount(preparedHits, PC[iP], iP);
        for (uint64 iHit=0; iHit<nHits; iHit++) {
            const PieceHit h=pieceHit(mapGen, preparedHits, PC[iP], iP, iHit, Lread, revertStrand);
            if (h.a1>=mapGen.sjGstart) {//this is sj align
                uint64 a1D, aLengthD, a1A, aLengthA, sj1;
                if (sjAlignSplit(h.a1, aLength, mapGen, a1D, aLengthD, a1A, aLengthA, sj1)) {//align crosses the junction: add donor and acceptor pieces
                    if (createExtendWindowsWithAlign(a1D, h.aStr)==EXIT_createExtendWindowsWithAlign_TOO_MANY_WINDOWS) break;
                    if (createExtendWindowsWithAlign(a1A, h.aStr)==EXIT_createExtendWindowsWithAlign_TOO_MANY_WINDOWS) break;
                };
            } else {//this is a normal genomic read
                if (createExtendWindowsWithAlign(h.a1, h.aStr)==EXIT_createExtendWindowsWithAlign_TOO_MANY_WINDOWS) break;
            };
        };
    };
}

void ReadAlign::extendWindowFlanks()
{// extend live windows by winFlankNbins on each side, within the chromosome; reset per-window counters
    for (uint64 iWin=0;iWin<nW;iWin++) {
        if (WC[iWin][WC_gStart]<=WC[iWin][WC_gEnd]) {//otherwise the window is dead
            uint64 wb=WC[iWin][WC_gStart];
            for (uint64 ii=0; ii<P.win.flankNbins && wb>0 && mapGen.chrBin[(wb-1) >> P.win.binChrNbits]==WC[iWin][WC_Chr];ii++) {
                wb--;
                winBin[ WC[iWin][WC_Str] ].set(wb, (uintWinBin) iWin);
            };
            WC[iWin][WC_gStart] = wb;

            wb=WC[iWin][WC_gEnd];
            for (uint64 ii=0; ii<P.win.flankNbins && wb+1<P.win.binN && mapGen.chrBin[(wb+1) >> P.win.binChrNbits]==WC[iWin][WC_Chr];ii++) {
                wb++;
                winBin[ WC[iWin][WC_Str] ].set(wb, (uintWinBin) iWin);
            };
            WC[iWin][WC_gEnd] = wb;
        };
        nWA[iWin]=0; //initialize nWA
        WALrec[iWin]=0; //initialize rec-length
        WlastAnchor[iWin]=-1;
    };
}

void ReadAlign::assignAlignsToWindows(uint64 Lread)
{// every piece's loci are added to the windows that contain them
    for (uint64 iP=0; iP<nP; iP++) {
        const uint64 aNrep=PC[iP][PC_Nrep];
        const uint64 aFrag=PC[iP][PC_iFrag];
        const uint64 aLength=PC[iP][PC_Length];
        const bool aAnchor=(aNrep<=P.win.anchorMultimapNmax); //this align is an anchor or not

        for (uint64 ii=0;ii<nW;ii++) nWAP[ii]=0;

        const uint64 nHits=pieceHitCount(preparedHits, PC[iP], iP);
        for (uint64 iHit=0; iHit<nHits; iHit++) {
            const PieceHit h=pieceHit(mapGen, preparedHits, PC[iP], iP, iHit, Lread, revertStrand);
            if (h.a1>=mapGen.sjGstart) {//this is sj read
                uint64 a1D, aLengthD, a1A, aLengthA, isj1;
                if (sjAlignSplit(h.a1, aLength, mapGen, a1D, aLengthD, a1A, aLengthA, isj1)) {//align crosses the junction
                    assignAlignToWindow(a1D, aLengthD, h.aStr, aNrep, aFrag, h.aRstart, aAnchor, isj1);
                    assignAlignToWindow(a1A, aLengthA, h.aStr, aNrep, aFrag, h.aRstart+aLengthD, aAnchor, isj1);
                };//otherwise the align does not cross the junction and is skipped
            } else {//this is a normal genomic read
                assignAlignToWindow(h.a1, aLength, h.aStr, aNrep, aFrag, h.aRstart, aAnchor, -1);
            };
        };
    };
}

#ifdef COMPILE_FOR_LONG_READS
void ReadAlign::filterWindowsByCoverage()
{// drop windows with low read coverage, merge aligns adjacent in read and genome space
    uint64 swWinCovMax=0;
    for (uint64 iW=0;iW<nW;iW++) {
        swWinCov[iW]=0;
        if (nWA[iW]==0) continue;
        uint64 rLast=0;
        for (uint64 ia=0; ia<nWA[iW]; ia++) {//calculate coverage from all aligns
            uint64 L1=WA[iW][ia][WA_Length];
            uint64 r1=WA[iW][ia][WA_rStart];
            if (r1+L1>rLast+1) {
                if (r1>rLast) {
                    swWinCov[iW] += L1;
                } else {
                    swWinCov[iW] += r1+L1-(rLast+1);
                };
                rLast=r1+L1-1;
            };
        };
        if (swWinCov[iW]>swWinCovMax) swWinCovMax=swWinCov[iW];
    };

    for (uint64 iW=0;iW<nW;iW++) {
        if (swWinCov[iW]<swWinCovMax*P.win.readCoverageRelativeMin || swWinCov[iW]<P.win.readCoverageBasesMin) {//remove windows that are not good enough
            nWA[iW]=0;
            continue;
        };
        uint64 ia1=0;
        for (uint64 ia=1; ia<nWA[iW]; ia++) {
            if ( WA[iW][ia][WA_rStart] == (WA[iW][ia1][WA_rStart]+WA[iW][ia1][WA_Length])
              && WA[iW][ia][WA_gStart] == (WA[iW][ia1][WA_gStart]+WA[iW][ia1][WA_Length])
              && WA[iW][ia][WA_iFrag]  ==  WA[iW][ia1][WA_iFrag]     ) {//merge
                WA[iW][ia1][WA_Length] += WA[iW][ia][WA_Length];
                WA[iW][ia1][WA_Anchor]=max(WA[iW][ia1][WA_Anchor],WA[iW][ia][WA_Anchor]);
                //NOTE: sjA and Nrep fields are not updated - this could cause trouble in some cases
            } else {//do not merge
                ia1++;
                if (ia1!=ia) {//move from ia to ia1
                    for (uint64 ii=0; ii<WA_SIZE; ii++) WA[iW][ia1][ii]=WA[iW][ia][ii];
                };
            };
        };
        nWA[iW]=ia1+1;
    };
}
#endif

void ReadAlign::collectStitchWindows(char **R, uint64 Lread)
{// record the read's non-empty windows for device stitching
    auto& b=*stitchCollect;
    auto& rd=b.reads.back();
    rd.seqOffset=b.seq.size(); rd.Lread=Lread; rd.mismatchLimit=outFilterMismatchNmaxTotal;
    for (uint64 ii=0;ii<MAX_N_MATES;ii++) rd.readLength[ii]=readLength[ii];
    b.seq.insert(b.seq.end(),R[0],R[0]+Lread);
    b.seq.insert(b.seq.end(),R[2],R[2]+Lread);
    rd.windowBegin=(unsigned) b.windows.size();
    for (uint64 iW=0; iW<nW; iW++) {
        if (nWA[iW]==0) continue;
        StitchWindowDesc wd;
        wd.Chr=WC[iW][WC_Chr]; wd.Str=WC[iW][WC_Str]; wd.roStr= revertStrand ? 1-wd.Str : wd.Str;
        wd.nA=nWA[iW]; wd.waOffset=b.wa.size()/WA_SIZE;
        for (uint64 ia=0;ia<nWA[iW];ia++) for (uint64 k=0;k<WA_SIZE;k++) b.wa.push_back(WA[iW][ia][k]);
        if (WlastAnchor[iW]<nWA[iW]) b.wa[(wd.waOffset+WlastAnchor[iW])*WA_SIZE+WA_Anchor]=2; //mark the last anchor
        b.windows.push_back(wd);
    }
    rd.windowCount=(unsigned) (b.windows.size()-rd.windowBegin);
}

void ReadAlign::stitchAllWindows(char **R, uint64 Lread)
{// generate transcripts for each window, choose the best
#ifndef COMPILE_FOR_LONG_READS
    const bool verifyCore=stitchcore::verifyEnabled();
    const StitchReadResult* injected=nullptr;
    if (stitchInject) {
        const auto& rr=stitchInject->reads.at(stitchInjectRead);
        if (rr.status==stitchOk) injected=&rr;
    }
    uint64 injectWindow=0; //index among non-empty windows, as collected
#endif

    trBest =trInit; //initialize next/best
    uint64 iW1=0;//index of non-empty windows
    uint64 trNtotal=0; //total number of recorded transcripts

    for (uint64 iW=0; iW<nW; iW++) {
        if (nWA[iW]==0) continue; //the window does not contain any aligns because it was merged with other windows

        if (WlastAnchor[iW]<nWA[iW]) {
            WA[ iW ][ WlastAnchor[iW] ][ WA_Anchor]=2; //mark the last anchor
        };

        #ifdef STAR_CAPTURE_SEEDS
        if(auto* w=seedTrace::worker()) {++w->windows;w->windowSeeds+=nWA[iW];}
        #endif
        for (uint64 ii=0;ii<nWA[iW];ii++) WAincl[ii]=false; //initialize mask

        trA=*trInit; //that one is initialized
        trA.Chr = WC[iW][WC_Chr];
        trA.Str = WC[iW][WC_Str];
        trA.roStr = revertStrand ? 1-trA.Str : trA.Str; //original strand of the read
        trA.maxScore=0;

        trAll[iW1]=trArrayPointer+trNtotal;
        if (trNtotal+P.align.transcriptsPerWindowNmax >= P.align.transcriptsPerReadNmax) {
            warnTranscriptSpace();
            break;
        };
        *(trAll[iW1][0])=trA;
        nWinTr[iW1]=0; //initialize number of transcripts per window

    #ifdef COMPILE_FOR_LONG_READS
        (void) Lread;
        stitchWindowSeeds(iW, iW1, NULL, R[trA.roStr==0 ? 0:2]);
        if (P.pCh.segmentMin>0) {
            for (uint64 ia=0;ia<nWA[iW];ia++) {//mark all seeds that overlap the best (and only for now) transcript trA
                if (WAincl[ia]) continue;
                for (uint64 iex=0;iex<trA.nExons;iex++) {
                    if ( WA[iW][ia][WA_rStart] < (trA.exons[iex][EX_R]+trA.exons[iex][EX_L]) &&
                        (WA[iW][ia][WA_rStart]+WA[iW][ia][WA_Length]) > trA.exons[iex][EX_R] &&
                         WA[iW][ia][WA_gStart] < (trA.exons[iex][EX_G]+trA.exons[iex][EX_L]) &&
                        (WA[iW][ia][WA_gStart]+WA[iW][ia][WA_Length]) > trA.exons[iex][EX_G] ) {
                        WAincl[ia]=true;
                        break;
                    };
                };
            };
            stitchWindowSeeds(iW, iW1, WAincl, R[trA.roStr==0 ? 0:2]);
        };
    #else
        if (injected && !verifyCore) {//device results: same transcripts, in the same order
            const auto& wr=stitchInject->windows.at(injected->windowBegin+injectWindow);
            thread_local stitchcore::Tr unpacked;
            for (unsigned k=0;k<wr.count;k++) {
                if (k>0) *(trAll[iW1][k])=trA;
                stitchInject->get(injected->resultBase+wr.first+k, unpacked);
                stitchcore::fromCore(unpacked, *trAll[iW1][k]);
            }
            nWinTr[iW1]=wr.count;
        } else {
            intScore mateBefore[MAX_N_MATES];
            if (verifyCore) std::copy(maxScoreMate,maxScoreMate+MAX_N_MATES,mateBefore);
            stitchWindowAligns(0, nWA[iW], 0, WAincl, 0, 0, trA, Lread, WA[iW], R[trA.roStr==0 ? 0:2], mapGen, P, trAll[iW1], nWinTr+iW1,
                              {outFilterMismatchNmaxTotal,readLength,maxScoreMate});
            if (verifyCore && injected) {
                stitchcore::verifyDevice(stitchInject->windows.at(injected->windowBegin+injectWindow),
                                         *stitchInject, injected->resultBase, trAll[iW1], nWinTr[iW1], iReadAll);
            } else if (verifyCore) {
                stitchcore::verifyWindow(P, mapGen, WA[iW], nWA[iW], trA, R[trA.roStr==0 ? 0:2], Lread, readLength,
                                         outFilterMismatchNmaxTotal, mateBefore, maxScoreMate, trAll[iW1], nWinTr[iW1], iReadAll);
            }
        }
        injectWindow++;
    #endif

        if (nWinTr[iW1]==0) continue;

        if (trAll[iW1][0]->maxScore > trBest->maxScore || (trAll[iW1][0]->maxScore == trBest->maxScore && trAll[iW1][0]->gLength < trBest->gLength ) ) {
            trBest=trAll[iW1][0];
        };

        trNtotal += nWinTr[iW1];
        iW1++;
    };

    nW=iW1;//only count windows that had alignments
#ifndef COMPILE_FOR_LONG_READS
    if (injected) {
        if (verifyCore) {
            for (uint64 ii=0; ii<P.readNmates; ii++) stitchcore::verifyMate(injected->mates[ii], maxScoreMate[ii], ii, iReadAll);
        } else {
            for (uint64 ii=0; ii<P.readNmates; ii++) maxScoreMate[ii]=injected->mates[ii];
        }
        ++stitchInjectedReads;
    } else if (stitchInject) {
        ++stitchCpuReads;
    }
#endif
}
