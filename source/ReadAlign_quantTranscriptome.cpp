#include "Transcriptome.h"
#include "ReadAlign.h"
#include "Transcript.h"
#include "serviceFuns.cpp"
#include "transcriptomePrimary.h"
#include <random>
using namespace std;
uint64 ReadAlign::quantTranscriptome (Transcriptome *Tr, uint64 nAlignG, Transcript **alignG, Transcript *alignT) {
    uint64 nAlignT=0;
    for (uint64 iag=0; iag<nAlignG; iag++) {//transform all alignments

        Transcript *align1=alignG[iag];

        if (!P.quant.trSAM.indel && (align1->nDel>0 || align1->nIns>0) ) {
            //prevent indels if requested
            continue;
        };
        if (!P.quant.trSAM.singleEnd && (P.readNmates==2 && align1->exons[0][EX_iFrag]==align1->exons[align1->nExons-1][EX_iFrag]) ) {//not readNends: this is alignment
        //prevent single end alignments
            continue;
        };

        // Keep the optional copy alive until quantAlign consumes align1 below.
        Transcript align2;
        if (!P.quant.trSAM.softClip) {
            //soft clipping not allowed, extend them if possible
            uint64 nMM1=0;
            char* R=Read1[align1->roStr==0 ? 0:2];
            align2=*align1; //copy this transcript to avoid changing the original one
            
            for (uint32 iab=0; iab<align2.nExons; iab++) {
                uint64 left1=0,right1=0;//how many bases to move left or right
                if (iab==0) {
                    left1=align2.exons[iab][EX_R];
                } else if (align2.canonSJ[iab-1]==-3) {
                    left1=align2.exons[iab][EX_R]-readLength[align2.exons[iab-1][EX_iFrag]]-1;
                };
                if (iab==align2.nExons-1) {//last block of left mates
                    right1=Lread-align2.exons[iab][EX_R]-align2.exons[iab][EX_L];

                } else if (align2.canonSJ[iab]==-3) {//last block of the right mate (i.e. whole read)
                    right1=readLength[align2.exons[iab][EX_iFrag]]-align2.exons[iab][EX_R]-align2.exons[iab][EX_L];
                };

                for (uint64 b=1; b<=left1 ; b++) {//extend to the left
                    char r1=R[align2.exons[iab][EX_R]-b];
                    char g1=mapGen.G[align2.exons[iab][EX_G]-b];
                    if ( r1!=g1 && r1<4 && g1<4) ++nMM1;
                };
                for (uint64 b=0; b<right1 ; b++) {//extend to the left
                    char r1=R[align2.exons[iab][EX_R]+align2.exons[iab][EX_L]+b];
                    char g1=mapGen.G[align2.exons[iab][EX_G]+align2.exons[iab][EX_L]+b];
                    if ( r1!=g1 && r1<4 && g1<4) ++nMM1;
                };
                align2.exons[iab][EX_R] -= left1;
                align2.exons[iab][EX_G] -= left1;
                align2.exons[iab][EX_L] += left1+right1;
            };

            if ( (align2.nMM + nMM1) > min(outFilterMismatchNmaxTotal, (uint64) (P.alignFilter.mismatchOverLengthMax*(Lread-1)) ) ) {
                //extension of soft clips yielded too many mismatches, no output
                continue;
            };
            
            align1 = &align2;
        };

        nAlignT += Tr->quantAlign(*align1,alignT+nAlignT);
    };

    if (P.quant.trSAM.bamYes) {//output Aligned.toTranscriptome.bam
        // Draw in both modes so the thread's random stream, used by later
        // choices such as --outMultimapperOrder Random, stays the same.
        uint64 iPrimary = int(rngUniformReal0to1(rngMultOrder)*nAlignT);
        if (P.quant.trSAM.primaryByReadIndex)
            iPrimary = transcriptomePrimaryByReadIndex(P.runRNGseed, iReadAll, nAlignT);
        alignT[iPrimary].primaryFlag=true;

        for (uint64 iatr=0;iatr<nAlignT;iatr++) {//write all transcripts
            alignBAM(alignT[iatr], nAlignT, iatr, 0, (uint64) -1, (uint64) -1, 0, -1, NULL, P.outSAMattrOrderQuant, outBAMoneAlign, outBAMoneAlignNbytes);
            for (uint64 imate=0; imate<P.readNmates; imate++) {//output each mate //not readNends: this is alignment
                outBAMquant->unsortedOneAlign(outBAMoneAlign[imate], outBAMoneAlignNbytes[imate], (imate>0 || iatr>0) ? 0 : (outBAMoneAlignNbytes[0]+outBAMoneAlignNbytes[1])*2*nAlignT);
            };
        };
    };

    //not used anymore per Colin Dewey's request
    //     if (nAlignT==0 && P.outSAMunmapped=="Within") {//read could be mapped to genome, but not transcriptome - output as unmapped
    //         uint64 unmapType=5;
    //         bool mateMapped[2]={false,false};
    //         alignBAM(*alignG[0], 0, 0, mapGen.chrStart[alignG[0]->Chr], (uint64) -1, (uint64) -1, 0,  unmapType, mateMapped, P.outSAMattrOrder);
    //             for (uint64 imate=0; imate<P.readNmates; imate++) {//output each mate //not readNends: this is alignment
    //                 outBAMquant->unsortedOneAlign(outBAMoneAlign[imate], outBAMoneAlignNbytes[imate], imate>0 ? 0 : outBAMoneAlignNbytes[0]+outBAMoneAlignNbytes[1]);
    //             };
    //
    //     };

    return nAlignT;
};
