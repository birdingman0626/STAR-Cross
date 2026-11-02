#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "ReadAlign.h"
#include "blocksOverlap.h"
using namespace std;
namespace {
// RNA strand of a transcript from its junction motifs: 0 undefined, 1 same as the read, 2 opposite
uint64 chimericStrand(const Transcript &t)
{
    if (t.intronMotifs[1]==0 && t.intronMotifs[2]==0) {//strand is undefined
        return 0;
    } else if ( (t.Str==0) == (t.intronMotifs[1]>0)) {//strand the same as RNA
        return 1;
    };
    return 2; //strand opposite to RNA
}

// first and last base of a transcript in the original read, without the inter-mate spacer base
void originalReadSpan(const Transcript &t, uint64 Lread, uint64 readLength0, uint64 &roStart, uint64 &roEnd)
{
    roStart=t.Str==0 ? t.exons[0][EX_R] : Lread - t.exons[t.nExons-1][EX_R] - t.exons[t.nExons-1][EX_L];
    roEnd=t.Str==0 ? t.exons[t.nExons-1][EX_R] + t.exons[t.nExons-1][EX_L] - 1 : Lread - t.exons[0][EX_R] - 1;
    if (roStart>readLength0) roStart--;
    if (roEnd>readLength0) roEnd--;
}

// genome base at pos, complemented for the reverse strand (Ns stay Ns)
inline char strandBase(const char *G, uint64 pos, uint64 Str)
{
    char b=G[pos];
    if (Str!=0 && b<4) b=3-b;
    return b;
}
} // namespace

bool ReadAlign::chimericDetectionOld() {

    //////////////////// chimeras
    //stich windows => chimeras
    //stich only the best window with one of the lower score ones for now - do not stich 2 lower score windows
    //stitch only one window on each end of the read

    if (nTr>P.pCh.mainSegmentMultNmax && nTr!=2)
    {//multimapping main segment, nTr==2 is a special case to be checked later
        return false;
    };

    if ( !(P.pCh.segmentMin>0 && trBest->rLength >= P.pCh.segmentMin
            && ( trBest->exons[trBest->nExons-1][EX_R] + trBest->exons[trBest->nExons-1][EX_L] + P.pCh.segmentMin <= Lread
              || trBest->exons[0][EX_R] >= P.pCh.segmentMin )
             && trBest->intronMotifs[0]==0 && (trBest->intronMotifs[1]==0 || trBest->intronMotifs[2]==0) ) ) {
            //there sholud be unmapped space at the start/end, and the main window is not a multimapping window, and non non-canonical junctions, and consistend junction motif
        return false;
    };

    int chimScoreBest=0,chimScoreNext=0;
    Transcript* trChim1=NULL;
    uint64 chimStrBest=0;
    chimOld.tr[0]=*trBest;
    chimOld.str=chimericStrand(*trBest);
    findChimericPartner(chimScoreBest, chimScoreNext, trChim1, chimStrBest);

    if (!(chimScoreBest >= P.pCh.scoreMin && chimScoreBest+P.pCh.scoreDropMax >= (int) (readLength[0]+readLength[1]) ) ) {
        return false;
    };

    if (nTr>P.pCh.mainSegmentMultNmax) {//check main segment for multi-aligns
     //this is nTr==2 - a special case: chimeras are allowed only if the 2nd chimeric segment is the next best alignment
        if ( trChim1!=trMult[0] && trChim1!=trMult[1] ) {
            return false;
        };
    };

    if (chimOld.str==0) chimOld.str=chimStrBest;

    chimOld.N=0;
    if (chimScoreNext + P.pCh.scoreSeparation >= chimScoreBest) {//report only if chimera is unique
        return false;
    };
    if (chimOld.tr[0].roStart > chimOld.tr[1].roStart) swap (chimOld.tr[0],chimOld.tr[1]);

    uint64 e0 = chimOld.tr[0].Str==1 ? 0 : chimOld.tr[0].nExons-1;
    uint64 e1 = chimOld.tr[1].Str==0 ? 0 : chimOld.tr[1].nExons-1;

    chimOld.repeat0=0;chimOld.repeat1=0;chimOld.j0=0;chimOld.j1=0;chimOld.motif=0;

    chimOld.N=2;
    if ( chimOld.tr[0].exons[e0][EX_iFrag] > chimOld.tr[1].exons[e1][EX_iFrag] ) {//strange configuration, rare, similar to the next one
        return false;//reject such chimeras
        //good test example:
        //CTTAGCTAGCAGCGTCTTCCCAGTGCCTGGAGGGCCAGTGAGAATGGCACCCTCTGGGATTTTTGCTCCTAGGTCT
        //TTGAGGTGAAGTTCAAAGATGTGGCTGGCTGTGAGGAGGCCGAGCTAGAGATCATGGAATTTGTGAATTTCTTGAA
    } else if ( chimOld.tr[0].exons[e0][EX_iFrag] < chimOld.tr[1].exons[e1][EX_iFrag] ) {//mates bracket the chimeric junction
        chimOld.N=2;
        chimOld.repeat=0;
        chimOld.motif=-1;
        if (chimOld.tr[0].Str==1) {//negative strand
            chimOld.j0=chimOld.tr[0].exons[e0][EX_G]-1;
        } else {
            chimOld.j0=chimOld.tr[0].exons[e0][EX_G]+chimOld.tr[0].exons[e0][EX_L];
        };
        if (chimOld.tr[1].Str==0) {//positive strand
            chimOld.j1=chimOld.tr[1].exons[e1][EX_G]-1;
        } else {
            chimOld.j1=chimOld.tr[1].exons[e1][EX_G]+chimOld.tr[1].exons[e1][EX_L];
        };
    } else {//chimeric junctions is within one of the mates, check and shift chimeric junction if necessary
        if (!chimericJunctionInMate(e0, e1, chimScoreBest))
            return false;
        chimericRepeats();
    };

    //final check
    if ( chimOld.tr[0].Str!=chimOld.tr[1].Str ||  chimOld.tr[0].Chr!=chimOld.tr[1].Chr
            || (chimOld.tr[0].Str==0 ? chimOld.j1-chimOld.j0+1LLU : chimOld.j0-chimOld.j1+1LLU) > (chimOld.motif>=0 ? P.align.intronMax :  P.align.matesGapMax) ) {
        //chimera has to bw from different chr/strand, or far away

        if (chimOld.motif>=0 &&
           (chimOld.tr[0].exons[e0][EX_L]<P.pCh.junctionOverhangMin+chimOld.repeat0 || chimOld.tr[1].exons[e1][EX_L]<P.pCh.junctionOverhangMin+chimOld.repeat1) ) {
            //filter out linear junctions that are very close to chimeric junction
            return false;
        };
        return true;
    };

    return false; //no good chimeras found
};

void ReadAlign::findChimericPartner(int &chimScoreBest, int &chimScoreNext, Transcript* &trChim1, uint64 &chimStrBest)
{// best second segment for trBest among the other windows' best transcripts and trBest's own window; sets chimOld.tr[1]
    uint64 roStart1, roEnd1;
    originalReadSpan(*trBest, Lread, readLength[0], roStart1, roEnd1);

    for (uint64 iW=0; iW<nW; iW++) {//check all other windows for chimeras
        for (uint64 iWt=0; iWt<nWinTr[iW]; iWt++){//cycl over transcripts in the window
            if (trBest!=trAll[iW][0] && iWt>0) break; //for all windows except that of the best transcript - hceck only iWt=0 (best trnascripts)
            if (trBest==trAll[iW][0] && iWt==0) continue;
            const Transcript &t=*trAll[iW][iWt];
            if (t.intronMotifs[0]>0) continue; //do not stitch a window to itself, or to a window with non-canonical junctions
            const uint64 chimStr1=chimericStrand(t);

            if (chimOld.str!=0 && chimStr1!=0 && chimOld.str!=chimStr1) continue; //chimeric segments have to have consitent strands

            uint64 roStart2, roEnd2;
            originalReadSpan(t, Lread, readLength[0], roStart2, roEnd2);

            uint64 chimOverlap = roStart2>roStart1 ?  (roStart2>roEnd1 ? 0 : roEnd1-roStart2+1) : (roEnd2<roStart1 ? 0 : roEnd2-roStart1+1);
            bool diffMates=(roEnd1 < readLength[0] && roStart2 >= readLength[0]) || (roEnd2 < readLength[0] && roStart1 >= readLength[0]);

            //segment lengths && (different mates || small gap between segments)
            if (roEnd1 > P.pCh.segmentMin + roStart1 + chimOverlap && roEnd2> P.pCh.segmentMin + roStart2 + chimOverlap
                && ( diffMates || ( (roEnd1 + P.pCh.segmentReadGapMax + 1) >= roStart2 && (roEnd2 + P.pCh.segmentReadGapMax + 1) >= roStart1 ) ) ) {

                int chimScore=trBest->maxScore + t.maxScore - (int)chimOverlap; //subtract overlap to avoid double counting

                uint64 overlap1=0;
                if (iWt>0 && chimScoreBest>0)
                {//overlap between chimeric candidate segment and the best chimeric segment so far. Maybe non-zero only if both are in the same window.
                    overlap1=blocksOverlap(chimOld.tr[1],*trAll[iW][iWt]);
                };

                if (chimScore > chimScoreBest) {
                    chimOld.tr[1]=t;
                    trChim1=trAll[iW][iWt];
                    if (overlap1==0)
                    {
                        chimScoreNext=chimScoreBest;
                    };
                    chimScoreBest=chimScore;
                    chimOld.tr[1].roStart = chimOld.tr[1].roStr ==0 ? chimOld.tr[1].rStart : Lread - chimOld.tr[1].rStart - chimOld.tr[1].rLength;
                    chimOld.tr[1].cStart  = chimOld.tr[1].gStart - mapGen.chrStart[chimOld.tr[1].Chr];
                    chimStrBest=chimStr1;
                } else if (chimScore>chimScoreNext && overlap1==0) {//replace the nextscore if it's not the best one and is higher than the previous one
                    chimScoreNext=chimScore;
                };
            };
        };//cycle over window transcripts
    };//cycle over windows
}

bool ReadAlign::chimericJunctionInMate(uint64 e0, uint64 e1, int &chimScoreBest)
{// junction inside one mate: choose its position (canonical motif preferred), shift the segment ends to it.
 // Returns false if the chimera is rejected.
    if (!(chimOld.tr[0].exons[e0][EX_L]>=P.pCh.junctionOverhangMin && chimOld.tr[1].exons[e1][EX_L]>=P.pCh.junctionOverhangMin )) {
        //large enough overhang required
        return false;
    };
    const char *G=mapGen.G;
    const uint64 str0=chimOld.tr[0].Str, str1=chimOld.tr[1].Str;
    uint64 roStart0 = str0==0 ? chimOld.tr[0].exons[e0][EX_R] : Lread - chimOld.tr[0].exons[e0][EX_R] - chimOld.tr[0].exons[e0][EX_L];
    uint64 roStart1 = str1==0 ? chimOld.tr[1].exons[e1][EX_R] : Lread - chimOld.tr[1].exons[e1][EX_R] - chimOld.tr[1].exons[e1][EX_L];

    //position in the genome of read base roStart0+jR (+d) in each segment
    auto pos0=[&](uint64 jR, int64 d) -> uint64 {
        return str0==0 ? chimOld.tr[0].exons[e0][EX_G]+jR+d : chimOld.tr[0].exons[e0][EX_G]+chimOld.tr[0].exons[e0][EX_L]-1-jR-d;
    };
    auto pos1=[&](uint64 jR, int64 d) -> uint64 {
        return str1==0 ? chimOld.tr[1].exons[e1][EX_G]-roStart1+roStart0+jR+d : chimOld.tr[1].exons[e1][EX_G]+chimOld.tr[1].exons[e1][EX_L]-1+roStart1-roStart0-jR-d;
    };

    uint64 jR, jRbest=0;
    int jScore=0,jMotif=0,jScoreBest=-999999,jScoreJ=0;
    uint64 jRmax = roStart1+chimOld.tr[1].exons[e1][EX_L];
    jRmax = jRmax>roStart0 ? jRmax-roStart0-1 : 0;
    for (jR=0; jR<jRmax; jR++) {//scan through the exons to find a canonical junction, and check for mismatches

        if (jR==readLength[0]) jR++; //skip the inter-mate base

        char bR=Read1[0][roStart0+jR];
        char b0=strandBase(G, pos0(jR,0), str0);
        char b1=strandBase(G, pos1(jR,0), str1);

        if ( ( P.pCh.filter.genomicN && (b0>3 || b1>3) ) || bR>3) {//chimera is not called if there are Ns in the genome or in the read
            chimOld.N=0;
            break;
        };

        //donor: the 2 bases after the segment-0 end; acceptor: the 2 bases before the segment-1 start
        char b01=strandBase(G, pos0(jR,1), str0);
        char b02=strandBase(G, pos0(jR,2), str0);
        char b11=strandBase(G, pos1(jR,-1), str1);
        char b12=strandBase(G, pos1(jR,0), str1);

        jMotif=0;
        if (b01==2 && b02==3 && b11==0 && b12==2) {//GTAG
            if (chimOld.str!=2) {
                jMotif=1;
            };
        } else if(b01==1 && b02==3 && b11==0 && b12==1) {//CTAC
            if (chimOld.str!=1) {
                jMotif=2;
            };
        };

        if (bR==b0 && bR!=b1) {
            jScore++;
        } else if (bR!=b0 && bR==b1) {
            jScore--;
        };

        jScoreJ =jMotif==0 ? jScore +  P.pCh.scoreJunctionNonGTAG : jScore ;

        if ( jScoreJ > jScoreBest || (jScoreJ == jScoreBest && jMotif>0) ) {
            chimOld.motif=jMotif;
            jRbest=jR;
            jScoreBest=jScoreJ;
        };
    };//jR cycle
    if ( chimOld.N==0 ) {//the chimera was rejected because of mismatches
        return false;
    };

    if (chimOld.motif==0) {//non-canonical chimera
        chimScoreBest += 1+P.pCh.scoreJunctionNonGTAG; //+1
        if ( !(chimScoreBest >= P.pCh.scoreMin && chimScoreBest+P.pCh.scoreDropMax >= (int) (readLength[0]+readLength[1])) ) {
            return false;
        };
    };

    //shift junction in chimOld.tr
    if (str0==1) {
        chimOld.tr[0].exons[e0][EX_R] +=chimOld.tr[0].exons[e0][EX_L]-jRbest-1;
        chimOld.tr[0].exons[e0][EX_G] +=chimOld.tr[0].exons[e0][EX_L]-jRbest-1;
        chimOld.tr[0].exons[e0][EX_L]=jRbest+1;
        chimOld.j0=chimOld.tr[0].exons[e0][EX_G]-1;
    } else {
        chimOld.tr[0].exons[e0][EX_L]=jRbest+1;
        chimOld.j0=chimOld.tr[0].exons[e0][EX_G]+chimOld.tr[0].exons[e0][EX_L];
    };

    if (str1==0) {
        chimOld.tr[1].exons[e1][EX_R] +=roStart0+jRbest+1-roStart1;
        chimOld.tr[1].exons[e1][EX_G] +=roStart0+jRbest+1-roStart1;
        chimOld.tr[1].exons[e1][EX_L]=roStart1+chimOld.tr[1].exons[e1][EX_L]-roStart0-jRbest-1;
        chimOld.j1=chimOld.tr[1].exons[e1][EX_G]-1;
    } else {
        chimOld.tr[1].exons[e1][EX_L]=roStart1+chimOld.tr[1].exons[e1][EX_L]-roStart0-jRbest-1;
        chimOld.j1=chimOld.tr[1].exons[e1][EX_G]+chimOld.tr[1].exons[e1][EX_L];
    };
    return true;
}

void ReadAlign::chimericRepeats()
{// lengths of identical sequence on both sides of the chimeric junction (up to 100 bases)
    const char *G=mapGen.G;
    const uint64 str0=chimOld.tr[0].Str, str1=chimOld.tr[1].Str;
    uint64 jR;
    for (jR=0;jR<100;jR++) {//forward check
        char b0=strandBase(G, str0==0 ? chimOld.j0+jR : chimOld.j0-jR, str0);
        char b1=strandBase(G, str1==0 ? chimOld.j1+1+jR : chimOld.j1-1-jR, str1);
        if (b0!=b1) break;
    };
    chimOld.repeat1=jR;
    for (jR=0;jR<100;jR++) {//reverse check
        char b0=strandBase(G, str0==0 ? chimOld.j0-1-jR : chimOld.j0+1+jR, str0);
        char b1=strandBase(G, str1==0 ? chimOld.j1-jR : chimOld.j1+jR, str1);
        if (b0!=b1) break;
    };
    chimOld.repeat0=jR;
}
