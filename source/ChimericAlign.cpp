#include "ChimericAlign.h"

ChimericAlign::ChimericAlign(ChimericSegment &seg1in, ChimericSegment &seg2in, int chimScoreIn, Genome &genomeIn, ReadAlign *RAin)
                              : seg1(seg1in), seg2(seg2in),chimScore(chimScoreIn), RA(RAin), P(seg1in.P), pCh(P.pCh), mapGen(genomeIn) {
    stitchingDone=false;

    al1=&seg1.align;
    al2=&seg2.align;

    if (al1->roStart > al2->roStart)
        swap (al1,al2);

    // Find the exon pair that overlaps in read space but maps to different chromosomes.
    // This identifies where the chimeric junction lives within multi-exon layouts.
    auto chrIdx = [this](uint gPos) -> uint {
        uint lo = 0, hi = mapGen.nChrReal;
        while (lo + 1 < hi) {
            uint mid = (lo + hi) / 2;
            if (mapGen.chrStart[mid] <= gPos) lo = mid; else hi = mid;
        }
        return lo;
    };

    int bestEx1 = -1, bestEx2 = -1;
    uint bestOverlap = 0;
    for (uint i1 = 0; i1 < al1->nExons; i1++) {
        uint rs1 = al1->exons[i1][EX_R];
        uint re1 = rs1 + al1->exons[i1][EX_L];
        uint gs1 = al1->exons[i1][EX_G];
        for (uint i2 = 0; i2 < al2->nExons; i2++) {
            uint rs2 = al2->exons[i2][EX_R];
            uint re2 = rs2 + al2->exons[i2][EX_L];
            uint gs2 = al2->exons[i2][EX_G];
            uint s = max(rs1, rs2);
            uint e = min(re1, re2);
            if (s >= e) continue; // no read-space overlap
            if (gs1 - rs1 == gs2 - rs2) continue; // same genomic mapping, not a junction
            if (chrIdx(gs1) == chrIdx(gs2)) continue; // same chromosome — splice, not chimeric
            uint overlap = e - s;
            if (overlap > bestOverlap) {
                bestOverlap = overlap;
                bestEx1 = i1;
                bestEx2 = i2;
            }
        }
    }

    if (bestEx1 >= 0) {
        ex1 = bestEx1;
        ex2 = bestEx2;
        // Ensure iFrag ordering for chimericCheck: iFrag(ex1) <= iFrag(ex2)
        if (al1->exons[ex1][EX_iFrag] > al2->exons[ex2][EX_iFrag]) {
            swap(al1, al2);
            swap(ex1, ex2);
        }
    } else {
        // Fallback: positional selection for non-overlapping segment pairs
        ex1 = al1->Str==1 ? 0 : al1->nExons-1;
        ex2 = al2->Str==0 ? 0 : al2->nExons-1;
    }
};

bool ChimericAlign::chimericCheck() {
    bool chimGood=true;

    chimGood = chimGood && al1->exons[ex1][EX_iFrag] <= al2->exons[ex2][EX_iFrag];//otherwise - strange configuration, both segments contain two mates
        //if ( trChim[0].exons[e0][EX_iFrag] > trChim[1].exons[e1][EX_iFrag] ) {//strange configuration, rare, similar to the next one
        //    chimN=0;//reject such chimeras
            //good test example:
            //CTTAGCTAGCAGCGTCTTCCCAGTGCCTGGAGGGCCAGTGAGAATGGCACCCTCTGGGATTTTTGCTCCTAGGTCT
            //TTGAGGTGAAGTTCAAAGATGTGGCTGGCTGTGAGGAGGCCGAGCTAGAGATCATGGAATTTGTGAATTTCTTGAA
        //} else

    //junction overhangs too short for chimerically spliced mates
    chimGood = chimGood && (al1->exons[ex1][EX_iFrag] < al2->exons[ex2][EX_iFrag] || (al1->exons[ex1][EX_L] >= pCh.junctionOverhangMin &&  al2->exons[ex2][EX_L] >= pCh.junctionOverhangMin) );

    return chimGood;
};
