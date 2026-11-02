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
void ReadAlign::stitchPieces(char **R, uint64 Lread) {
    #ifdef STAR_CAPTURE_SEEDS
    seedTrace::Timer diagnostic(&seedTrace::Worker::stitchNs);
    #endif

#ifndef COMPILE_FOR_LONG_READS
    if (stitchInject && stitchInject->batch && !stitchcore::verifyEnabled()) {
        const StitchReadResult& rr=stitchInject->reads.at(stitchInjectRead);
        if (rr.status==stitchOk) {//the device stitched this read; its windows were built in the collect pass, in this order
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
            for (unsigned k=0; k<rr.windowCount; k++) {//same steps as the window loop below, minus window building
                const StitchWindowResult& wr=stitchInject->windows.at(rr.windowBegin+k);
                trAll[iW1]=trArrayPointer+trNtotal;
                if (trNtotal+P.alignTranscriptsPerWindowNmax >= P.alignTranscriptsPerReadNmax) {
                    OptionalMutexLock logLock(g_threadChunks.mutexLogMain, P.runThreadN>1);
                    P.inOut->logMain << "WARNING: not enough space allocated for transcript. Did not process all windows for read "<< readName+1 <<endl;
                    P.inOut->logMain <<"   SOLUTION: increase alignTranscriptsPerReadNmax and re-run\n" << flush;
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
            return;
        }
    }
#endif

    //reset winBin — O(modified) instead of O(N) memset
    winBin[0].reset();
    winBin[1].reset();


//     for (uint64 iWin=0;iWin<nWall;iWin++) {//zero out winBin
//         if (WC[iWin][WC_gStart]<=WC[iWin][WC_gEnd]) {//otherwise the window is dead
//             memset(&(winBin[WC[iWin][WC_Str]][WC[iWin][WC_gStart]]),255,sizeof(winBin[0][0])*(WC[iWin][WC_gEnd]-WC[iWin][WC_gStart]+1));
//         };
// //         for (uint64 ii=C[iWin][WC_gStart]; ii<WC[iWin][WC_gEnd]; ii++) {
// //             winBin[WC[WC_Str]
// //         };
//     };

//     //debug
//     for (uint64 ii=0;ii<P.winBinN;ii++){
//         if (winBin[0][ii]!=uintWinBinMax || winBin[1][ii]!=uintWinBinMax) {
//             cerr<< "BUG in stitchPieces: ii="<<ii<<"   "<< winBin[0][ii] <<"   "<<winBin[1][ii] <<"   iRead="<<iRead<<"   nW="<<nW<<endl;
//             for (uint64 iWin=0;iWin<nW;iWin++) {
//                 cerr <<WC[iWin][WC_gStart]<<"   " <<WC[iWin][WC_gEnd] <<"   "<<WC[iWin][WC_Str] <<endl;
//             };
//             exit(1);
//         };
//     };


    nW=0; //number of windows
    for (uint64 iP=0; iP<nP; iP++) {//scan through all anchor pieces, create alignment windows
        // np is number of pieces (stored seed alignments)


//          if (PC[iP][PC_Nrep]<=P.winAnchorMultimapNmax || PC[iP][PC_Length]>=readLength[PC[iP][PC_iFrag]] ) {//proceed if piece is an anchor, i.e. maps few times or is long enough
       if (PC[iP][PC_Nrep]<=P.winAnchorMultimapNmax ) {//proceed if piece is an anchor, i.e. maps few times

            uint64 aDir   = PC[iP][PC_Dir];
            uint64 aLength= PC[iP][PC_Length];

            // prepared GPU hits may omit hits that cannot reach any window (see gpuSeedHits.cuh)
            const uint64 nHits = preparedHits ? preparedHits->counts.at(iP) : PC[iP][PC_SAend]-PC[iP][PC_SAstart]+1;
            for (uint64 iHit=0; iHit<nHits; iHit++) {//scan through all alignments of this piece
                // going through ordered positions in the suffix array from PC_SAstart to PC_SAend
                uint64 a1,aStr;
                if(preparedHits) {
                    const auto& hit=preparedHits->hits[preparedHits->offsets[iP]+iHit];
                    a1=hit.genome;aStr=hit.strand;
                } else {
                a1 = mapGen.SA[PC[iP][PC_SAstart]+iHit];
                aStr = a1 >> mapGen.GstrandBit;
                a1 &= mapGen.GstrandMask; //remove strand bit

                //convert to positive strand
                if (aDir==1 && aStr==0) {
                    aStr=1;
                } else if (aDir==0 && aStr==1) {
                    a1 = mapGen.nGenome - (aLength+a1);
                } else if (aDir==1 && aStr==1) {
                    aStr=0;
                    a1 = mapGen.nGenome - (aLength+a1);
                };

                }
                //final strand
                if (revertStrand) { //modified strand according to user input CHECK!!!!
                    aStr=1-aStr;
                };

                if (a1>=mapGen.sjGstart) {//this is sj align
                    uint64 a1D, aLengthD, a1A, aLengthA, sj1;
                    if (sjAlignSplit(a1, aLength, mapGen, a1D, aLengthD, a1A, aLengthA, sj1)) {//align crosses the junction

                        int addStatus=createExtendWindowsWithAlign(a1D, aStr);//add donor piece
                        if (addStatus==EXIT_createExtendWindowsWithAlign_TOO_MANY_WINDOWS) {//too many windows
                            break;
                        };
                        addStatus=createExtendWindowsWithAlign(a1A, aStr);//add acceptor piece
                        if (addStatus==EXIT_createExtendWindowsWithAlign_TOO_MANY_WINDOWS) {//too many windows
                            break;
                        };
                    };
                } else {//this is a normal genomic read
                    int addStatus=createExtendWindowsWithAlign(a1, aStr);
                    if (addStatus==EXIT_createExtendWindowsWithAlign_TOO_MANY_WINDOWS) {//too many windows
                        break;
                    };
                };
            }; //for iHit: scan through all alignments of this piece
        };//if (PC[iP][PC_Nrep]<=P.winAnchorMultimapNmax) //proceed if anchor
    };//for (uint64 iP=0; iP<nP; iP++) //scan through all anchor pieces, create alignment windows


    for (uint64 iWin=0;iWin<nW;iWin++) {//extend windows with flanks
        if (WC[iWin][WC_gStart]<=WC[iWin][WC_gEnd]) {//otherwise the window is dead

            uint64 wb=WC[iWin][WC_gStart];
            for (uint64 ii=0; ii<P.winFlankNbins && wb>0 && mapGen.chrBin[(wb-1) >> P.winBinChrNbits]==WC[iWin][WC_Chr];ii++) {
                wb--;
                winBin[ WC[iWin][WC_Str] ].set(wb, (uintWinBin) iWin);
            };
            WC[iWin][WC_gStart] = wb;

            wb=WC[iWin][WC_gEnd];
            for (uint64 ii=0; ii<P.winFlankNbins && wb+1<P.winBinN && mapGen.chrBin[(wb+1) >> P.winBinChrNbits]==WC[iWin][WC_Chr];ii++) {
                wb++;
                winBin[ WC[iWin][WC_Str] ].set(wb, (uintWinBin) iWin);
            };
            WC[iWin][WC_gEnd] = wb;


        };
        nWA[iWin]=0; //initialize nWA
        WALrec[iWin]=0; //initialize rec-length
        WlastAnchor[iWin]=-1;
    };

    nWall=nW;

    #ifdef OFF_BEFORE_SEEDdistribution
        #warning OFF_BEFORE_SEEDdistribution
        nW=0;
        nTr=0;
        return;
    #endif

    for (uint64 iP=0; iP<nP; iP++) {//scan through all pieces/aligns, add them to alignment windows, create alignment coordinates
        uint64 aNrep=PC[iP][PC_Nrep];
        uint64 aFrag=PC[iP][PC_iFrag];
        uint64 aLength=PC[iP][PC_Length];
        uint64 aDir=PC[iP][PC_Dir];

        bool aAnchor=(aNrep<=P.winAnchorMultimapNmax); //this align is an anchor or not

        for (uint64 ii=0;ii<nW;ii++) {//initialize nWAP
            nWAP[ii]=0;
        };

        const uint64 nHits = preparedHits ? preparedHits->counts.at(iP) : PC[iP][PC_SAend]-PC[iP][PC_SAstart]+1;
        for (uint64 iHit=0; iHit<nHits; iHit++) {//scan through all alignments

            uint64 a1,aStr,aRstart;
            if(preparedHits) {
                const auto& hit=preparedHits->hits[preparedHits->offsets[iP]+iHit];
                a1=hit.genome;aStr=hit.strand;aRstart=hit.readStart;
            } else {
            a1 = mapGen.SA[PC[iP][PC_SAstart]+iHit];
            aStr = a1 >> mapGen.GstrandBit;
            a1 &= mapGen.GstrandMask; //remove strand bit
            aRstart=PC[iP][PC_rStart];

            //convert to positive strand
            if (aDir==1 && aStr==0) {
                aStr=1;
                aRstart = Lread - (aLength+aRstart);
            } else if (aDir==0 && aStr==1) {
                aRstart = Lread - (aLength+aRstart);
                a1 = mapGen.nGenome - (aLength+a1);
            } else if (aDir==1 && aStr==1) {
                aStr=0;
                a1 = mapGen.nGenome - (aLength+a1);
            };

            }
            //final strand
            if (revertStrand) { //modified strand according to user input CHECK!!!!
                aStr=1-aStr;
            };


            if (a1>=mapGen.sjGstart) {//this is sj read
                uint64 a1D, aLengthD, a1A, aLengthA, isj1;
                if (sjAlignSplit(a1, aLength, mapGen, a1D, aLengthD, a1A, aLengthA, isj1)) {//align crosses the junction

                        assignAlignToWindow(a1D, aLengthD, aStr, aNrep, aFrag, aRstart, aAnchor, isj1);
                        assignAlignToWindow(a1A, aLengthA, aStr, aNrep, aFrag, aRstart+aLengthD, aAnchor, isj1);

                  } else {//align does not cross the junction
                        continue; //do not check this align, continue to the next one
                  };

              } else {//this is a normal genomic read
                    assignAlignToWindow(a1, aLength, aStr, aNrep, aFrag, aRstart, aAnchor, -1);
              };
        };

//         for (uint64 ii=0;ii<nW;ii++) {//check of some pieces created too many aligns in some windows, and remove those from WA (ie shift nWA indices
//             if (nWAP[ii]>P.seedNoneLociPerWindow) nWA[ii] -= nWAP[ii];
//         };
    };

    //TODO remove windows that have too many alignments
    //aligns are still sorted by original read coordinates, change direction for negative strand
    // DOES NOT HELP!!!
//     for ( uint64 iW=0;iW<nW;iW++ ) {
//         if (WA[iW][0][WA_rStart]>WA[iW][nWA[iW]-1][WA_rStart]) {//swap
//             for (uint64 iA=0;iA<nWA[iW]/2;iA++) {
//                 for (uint64 ii=0;ii<WA_SIZE;ii++) {
//                     uint64 dummy=WA[iW][iA][ii];
//                     WA[iW][iA][ii]=WA[iW][nWA[iW]-1-iA][ii];
//                     WA[iW][nWA[iW]-1-iA][ii]=dummy;
//                 };
//             };
//         };
//     };

#ifdef COMPILE_FOR_LONG_READS
uint64 swWinCovMax=0;
for (uint64 iW=0;iW<nW;iW++) {//check each window
    swWinCov[iW]=0;
    if (nWA[iW]>0) {
        //select good windows by coverage
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
        };//for (uint64 ia=0; ia<nWA[iW]; ia++)

        if (swWinCov[iW]>swWinCovMax) swWinCovMax=swWinCov[iW];
    };//if (nWA[iW]>0)
};//for (uint64 iW=0;iW<nW;iW++)

for (uint64 iW=0;iW<nW;iW++) {
    if (swWinCov[iW]<swWinCovMax*P.winReadCoverageRelativeMin || swWinCov[iW]<P.winReadCoverageBasesMin) {//remove windows that are not good enough
        nWA[iW]=0;
    } else {//merge pieces that are adjacent in R- and G-spaces	
        uint64 ia1=0;
        for (uint64 ia=1; ia<nWA[iW]; ia++) {
            if ( WA[iW][ia][WA_rStart] == (WA[iW][ia1][WA_rStart]+WA[iW][ia1][WA_Length]) \
              && WA[iW][ia][WA_gStart] == (WA[iW][ia1][WA_gStart]+WA[iW][ia1][WA_Length]) \
              && WA[iW][ia][WA_iFrag]  ==  WA[iW][ia1][WA_iFrag]     ) {//merge

                WA[iW][ia1][WA_Length] += WA[iW][ia][WA_Length];
                WA[iW][ia1][WA_Anchor]=max(WA[iW][ia1][WA_Anchor],WA[iW][ia][WA_Anchor]);
                //NOTE: I am not updating sjA and Nrep fields - this could cause trouble in some cases

            } else {//do not merge
                ia1++;
                if (ia1!=ia) {//move from ia to ia1
                    for (uint64 ii=0; ii<WA_SIZE; ii++) {
                        WA[iW][ia1][ii]=WA[iW][ia][ii];
                    };
                };
            };
        };
        nWA[iW]=ia1+1;
    };
};

//mapping time initialize
std::time(&timeStart);
#endif

    #ifdef OFF_BEFORE_STITCH
        #warning OFF_BEFORE_STITCH
        nW=0;
        return;
    #endif
    if (stitchCollect) {//record windows for device stitching; the read is aligned again in the inject pass
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
        nW=0;
        return;
    }
    const bool verifyCore=stitchcore::verifyEnabled();
    const StitchReadResult* injected=nullptr;
    if (stitchInject) {
        const auto& rr=stitchInject->reads.at(stitchInjectRead);
        if (rr.status==stitchOk) injected=&rr;
    }
    uint64 injectWindow=0; //index among non-empty windows, as collected

    //generate transcript for each window, choose the best
    trBest =trInit; //initialize next/best
    uint64 iW1=0;//index of non-empty windows
    uint64 trNtotal=0; //total number of recorded transcripts

    for (uint64 iW=0; iW<nW; iW++) {//transcripts for all windows

        if (nWA[iW]==0) continue; //the window does not contain any aligns because it was merged with other windows

//         {//debug
//             if ( WA[iW][0][WA_iFrag]==WA[iW][nWA[iW]-1][WA_iFrag] ) continue;
//         };
//
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
        if (trNtotal+P.alignTranscriptsPerWindowNmax >= P.alignTranscriptsPerReadNmax) {
            OptionalMutexLock logLock(g_threadChunks.mutexLogMain, P.runThreadN>1);//Log.out is shared with other workers
            P.inOut->logMain << "WARNING: not enough space allocated for transcript. Did not process all windows for read "<< readName+1 <<endl;
            P.inOut->logMain <<"   SOLUTION: increase alignTranscriptsPerReadNmax and re-run\n" << flush;
            break;
        };
        *(trAll[iW1][0])=trA;
        nWinTr[iW1]=0; //initialize number of transcripts per window


    #ifdef COMPILE_FOR_LONG_READS
        stitchWindowSeeds(iW, iW1, NULL, R[trA.roStr==0 ? 0:2]);
        if (P.pCh.segmentMin>0) {
            for (uint64 ia=0;ia<nWA[iW];ia++)
            {//mark all seeds that overlap the best (and only for now) transcript trA
                if (WAincl[ia]) continue;
                for (uint64 iex=0;iex<trA.nExons;iex++)
                {
                    if ( WA[iW][ia][WA_rStart] < (trA.exons[iex][EX_R]+trA.exons[iex][EX_L]) && \
                        (WA[iW][ia][WA_rStart]+WA[iW][ia][WA_Length]) > trA.exons[iex][EX_R] && \
                         WA[iW][ia][WA_gStart] < (trA.exons[iex][EX_G]+trA.exons[iex][EX_L]) && \
                        (WA[iW][ia][WA_gStart]+WA[iW][ia][WA_Length]) > trA.exons[iex][EX_G] )
                    {
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

        if (nWinTr[iW1]==0) {
            continue;
        };

        if (trAll[iW1][0]->maxScore > trBest->maxScore || (trAll[iW1][0]->maxScore == trBest->maxScore && trAll[iW1][0]->gLength < trBest->gLength ) ) {
            trBest=trAll[iW1][0];
        };

        trNtotal += nWinTr[iW1];
        iW1++;
    };

    nW=iW1;//only count windows that had alignments
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

//     {//debug
//         std::time(&timeFinish);
//         double timeDiff=difftime(timeFinish,timeStart);
//         cout << "     "<< timeDiff << "     "<<trBest->maxScore*100/Lread<<"   "<<iRead<<endl;;
//     };

    if (trBest->maxScore==0) {//no window was aligned (could happen if for all windows too many reads are multiples)
        mapMarker = MARKER_NO_GOOD_WINDOW;
        nW=0;
        return;
    };

};//end of function
