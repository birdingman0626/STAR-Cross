/*
 * inserts sequences into the SA
 * returns number of SA indexes inserted
 */
#include "insertSeqSA.h"
#include "ErrorWarning.h"
#include "SuffixArrayFuns.h"
#include "SequenceFuns.h"
#include "serviceFuns.cpp"
#include "streamFuns.h"
#include "binarySearch2.h"
#include "funCompareUintAndSuffixes.h"
#include <cmath>
#include <memory>
#include "genomeSAindex.h"
#include "uninitArray.h"
using namespace std;

namespace {

constexpr uint64 GENOME_endFillL=16;

void shiftExistingIndices(PackedArray &SA, uint64 nG, uint64 nG1, uint64 nG2, uint64 N2bit)
{//existing suffixes after the insertion point move by the inserted length
    const uint64 strandMask=~N2bit;
    for (uint64 isa=0;isa<SA.length; isa++)
    {
        uint64 ind1=SA[isa];
        if ( (ind1 & N2bit)>0 )
        {//- strand
            if ( (ind1 & strandMask)>=nG2 )
            {//the first nG bases
                ind1+=nG1; //reverse complementary indices are all shifted by the length of the sequence
                SA.writePacked(isa,ind1);
            };
        } else
        {//+ strand
            if ( ind1>=nG )
            {//the last nG2 bases
                ind1+=nG1; //reverse complementary indices are all shifted by the length of the sequence
                SA.writePacked(isa,ind1);
            };
        };
    };
};

uint64 findInsertionPoints(PackedArray &SA, char *seq1[2], uint64 *indArray, uint64 nG, uint64 nG1, Parameters &P, Genome &mapGen)
{//SA insertion point of every suffix of the new sequence (both strands); returns their number
    #pragma omp parallel num_threads(P.runThreadN)
    #pragma omp for schedule (dynamic,1000)
    for (int64 ii=0; ii<(int64)(2*nG1); ii++) {//find insertion points for each of the sequences

        if (seq1[0][ii]>3)
        {//no index for suffices starting with N
            indArray[ii*2]=(uint64) -1;
        } else
        {
            indArray[ii*2] =  suffixArraySearch1(mapGen, seq1, ii, 10000, nG, ((uint64)ii<nG1 ? true:false), 0, SA.length-1, 0) ;
            indArray[ii*2+1] = ii;
        };
    };

    uint64 nInd=0;//true number of new indices
    for (uint64 ii=0; ii<2*nG1; ii++) {//remove entries that cannot be inserted, this cannot be done in the parallel cycle above
        if (indArray[ii*2]!= (uint64) -1) {
            indArray[nInd*2]=indArray[ii*2];
            indArray[nInd*2+1]=indArray[ii*2+1];
            ++nInd;
        };
    };
    return nInd;
};

void sortInsertions(uint64 *indArray, uint64 nInd, char *seq, uint64 nG1, Genome &mapGen)
{//order by insertion point, then by suffix
    if (mapGen.pGe.gSuffixLengthMax==static_cast<uint64>(-1)) {
        // The default means unlimited suffix length, not a memcmp byte count.
        // The existing numeric comparator stops at the chromosome separator.
        sortUintAndSuffixes(indArray,nInd,CompareUintAndSuffixes{seq});
    } else {
        const uint64_t suffixLengthMax=mapGen.pGe.gSuffixLengthMax/sizeof(uint64_t);
        sortUintAndSuffixes(indArray,nInd,CompareUintAndSuffixesMemcmp{seq,suffixLengthMax,2*nG1+GENOME_endFillL});
    }
};

void mergeInsertions(PackedArray &SA, PackedArray &SA1, const uint64 *indArray, uint64 nInd, uint64 nG, uint64 nG1, uint64 nG2, uint64 N2bit)
{//SA1 = SA with the new suffixes inserted before their insertion points
    auto packedNewIndex=[&](uint64 ind1) {
        return ind1<nG1 ? ind1+nG : (ind1-nG1+nG2) | N2bit;
    };
    uint64 isa1=0, isa2=0;
    for (uint64 isa=0;isa<SA.length;isa++) {
        while (isa==indArray[isa1*2]) {//insert new index before the existing index
            SA1.writePacked(isa2,packedNewIndex(indArray[isa1*2+1]));
            ++isa2; ++isa1;
        };
        SA1.writePacked(isa2,SA[isa]); //TODO make sure that the first sj index is not before the first array index
        ++isa2;
    };
    for (;isa1<nInd;isa1++)
    {//insert the last indices
        SA1.writePacked(isa2,packedNewIndex(indArray[isa1*2+1]));
        ++isa2;
    };
};

} // namespace

uint64 insertSeqSA(PackedArray & SA, PackedArray & SA1, PackedArray & SAi, char * G, char * G1, uint64 nG, uint64 nG1, uint64 nG2, Parameters & P, Genome &mapGen)
{//insert new sequences into the SA

    uint64 GstrandBit1 = (uint64) floor(log(nG+nG1)/log(2))+1;
    if (GstrandBit1<32) GstrandBit1=32; //TODO: use simple access function for SA
    if ( GstrandBit1+1 > SA.wordLength)
    {//sequence is too long - GstrandBit changed
        ostringstream errOut;
        errOut << "EXITING because of FATAL ERROR: cannot insert sequence on the fly because of strand GstrandBit problem\n";
        errOut << "SOLUTION: please contact STAR author at https://groups.google.com/forum/#!forum/rna-star\n";
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_GENOME_FILES, P);
    };

    const uint64 N2bit= 1LLU << (SA.wordLength-1);
    shiftExistingIndices(SA, nG, nG1, nG2, N2bit);

    //new sequence and its reverse complement; ends are filled with spacers to mark boundaries
    char* seq1[2]; // stack array (fix: memory leak)
    auto sequenceStorage=makeUninitArray<char>(4*nG1+3*GENOME_endFillL);
    char* seqq=sequenceStorage.get();

    seq1[0]=seqq+GENOME_endFillL;//TODO: avoid defining an extra array, use reverse search
    seq1[1]=seqq+2*GENOME_endFillL+2*nG1;

    memset(seqq,GENOME_spacingChar,GENOME_endFillL);
    memset(seqq+2*nG1+GENOME_endFillL,GENOME_spacingChar,GENOME_endFillL);
    memset(seqq+4*nG1+2*GENOME_endFillL,GENOME_spacingChar,GENOME_endFillL);

    memcpy(seq1[0], G1, nG1);
    for (uint64 ii=0; ii<nG1; ii++)
    {//reverse complement sequence
        seq1[0][2*nG1-1-ii]=seq1[0][ii]<4 ? 3-seq1[0][ii] : seq1[0][ii];
    };
    complementSeqNumbers(seq1[0], seq1[1], 2*nG1);//complement

    auto insertionStorage=makeUninitArray<uint64>(nG1*2*2+2);
    uint64* indArray=insertionStorage.get();// insertion place and index for both strands
    const uint64 nInd=findInsertionPoints(SA, seq1, indArray, nG, nG1, P, mapGen);

    time_t rawtime;
    time ( &rawtime );
    P.inOut->logMain  << timeMonthDayTime(rawtime) << "   Finished SA search, number of new SA indices = "<<nInd<<endl;

    sortInsertions(indArray, nInd, seq1[0], nG1, mapGen);
    time ( &rawtime );
    P.inOut->logMain  << timeMonthDayTime(rawtime) << "   Finished qsort" <<endl;
    time ( &rawtime );
    P.inOut->logMain  << timeMonthDayTime(rawtime) << "   Finished sorting SA indices"<<endl;

    indArray[2*nInd]=(uint64) -999; //mark the last junction
    indArray[2*nInd+1]=(uint64) -999; //mark the last junction

    SA1.defineBits(SA.wordLength,SA.length+nInd);
    mergeInsertions(SA, SA1, indArray, nInd, nG, nG1, nG2, N2bit);

    time ( &rawtime );
    P.inOut->logMain  << timeMonthDayTime(rawtime) << "   Finished inserting SA indices" <<endl;

    //change parameters, most parameters are already re-defined in sjdbPrepare.cpp
    SA.defineBits(mapGen.GstrandBit+1,SA.length+nInd);//same as SA2
    SA.pointArray(SA1.charArray);
    mapGen.nSA=SA.length;
    mapGen.nSAbyte=SA.lengthByte;

    //generate SAi
    SAi.deallocateArray(); // Replace the old owned prefix table, not a second allocation.
    genomeSAindex(G,SA,P,SAi,mapGen);

    time ( &rawtime );
    P.inOut->logMain  << timeMonthDayTime(rawtime) << "   Finished SAi" <<endl;

    return nInd;
};
