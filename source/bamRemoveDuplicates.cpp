#include <memory>
#include <unordered_map>
#include <vector>
#include "bamRemoveDuplicates.h"
#include <iostream>
#include "htslib/htslib/sam.h"
#include "IncludeDefine.h"
#include SAMTOOLS_BGZF_H
#include "ErrorWarning.h"
#include "unaligned.h"
#include "uninitArray.h"
using namespace std;
#define compareReturn(a,b) if(a>b) {return 1;} else if (a<b) {return -1;}

uint64 g_bamRemoveDuplicatesMate2basesN;

namespace {
// BAM records sit at arbitrary byte offsets in the loaded block, so their 32-bit
// fields are read with memcpy-based loads instead of uint32* dereferences (UB).
class BamFields {
    const char* base;
public:
    explicit BamFields(const char* record) : base(record) {}
    uint32 operator[](size_t index) const { return loadUnaligned<uint32>(base + 4*index); }
    const char* bytes() const { return base; }
};
// aD stores record addresses; each sort slot holds one address.
BamFields recordAt(const void* slot) { return BamFields(*(const char* const*) slot); }
}

int funCompareNames(const void *a, const void *b) {//compare read names
    const BamFields pa=recordAt(a);
    const BamFields pb=recordAt(b);

    uint32 la=(pa[3]<<24)>>24;
    uint32 lb=(pb[3]<<24)>>24;

    compareReturn(la,lb) else {
        const char* ca=pa.bytes()+9*4;
        const char* cb=pb.bytes()+9*4;
        for (uint32 ii=0;ii<la;ii++) {
            compareReturn(ca[ii],cb[ii]);
        };
        uint32 fa=pa[4]>>16;
        uint32 fb=pb[4]>>16;

        compareReturn((fa&0x80), (fb&0x80));
        return 0;
    };
};

uint32 funStartExtendS(const BamFields p) {//calculates align start extending right S operation
    const BamFields cig(p.bytes()+9*4+((p[3]<<24)>>24));
    if ( ((cig[0]<<28)>>28) == 4 ) {//first (right) operation is S
        return p[2]-(cig[0]>>4);
    } else {
        return p[2];
    };
};

uint32 funCigarExtendS(const BamFields p, uint32* cigarOut) {
    const BamFields cig(p.bytes()+9*4+((p[3]<<24)>>24));
    uint32 n=(p[4]<<16)>>16, n1=n;

    if (((cig[0]<<28)>>28) == 4) {
        --n1;
        memcpy((char*) cigarOut, cig.bytes()+4, n1*sizeof(uint32));//copy CIGAR starting from the 2nd operation
        cigarOut[0]+=(cig[0]>>4)<<4;
    } else {
        memcpy((char*) cigarOut, cig.bytes(), n*sizeof(uint32));//copy full CIGAR
    };
    if (((cig[n-1]<<28)>>28) == 4) {//remove last S opeartion add length to previous M
        --n1;
        cigarOut[n1-1]+=(cig[n-1]>>4)<<4;
    };
    return n1;
};

int funCompareCigarsExtendS(const BamFields pa, const BamFields pb){
    uint32 ca[100], cb[100];
    uint32 na=funCigarExtendS(pa,ca);
    uint32 nb=funCigarExtendS(pb,cb);
    compareReturn(na,nb);
    for (uint32 ii=0; ii<na; ii++) {
        compareReturn(ca[ii],cb[ii]);
    };
    return 0;
};

int funCompareCoordFlagCigarSeq(const void *a, const void *b) {
    const BamFields pa1=recordAt(a);
    const BamFields pa2=recordAt((const char*)a+8);

    const BamFields pb1=recordAt(b);
    const BamFields pb2=recordAt((const char*)b+8);

    compareReturn(funStartExtendS(pa1),funStartExtendS(pb1));//position match
    compareReturn(funStartExtendS(pa2),funStartExtendS(pb2));//2nd mate position match
    compareReturn(pa1[4]>>16,pb1[4]>>16);//FLAG match
    compareReturn(pa2[4]>>16,pb2[4]>>16);//FLAG match - 2nd mate

    int ret1=funCompareCigarsExtendS(pa1,pb1);
    if (ret1!=0) return ret1;
    ret1=funCompareCigarsExtendS(pa2,pb2);
    if (ret1!=0) return ret1;

    //compare sequences
    const uint8_t* sa=((const uint8_t*) pa2.bytes())+9*4+((pa2[3]<<24)>>24)+((pa2[4]<<16)>>16)*4;
    const uint8_t* sb=((const uint8_t*) pb2.bytes())+9*4+((pb2[3]<<24)>>24)+((pb2[4]<<16)>>16)*4;
    if (((pa2[4]>>16) & 0x10) == 0) {//not reverse complemented
        uint64 ii=1;
        for (; ii<g_bamRemoveDuplicatesMate2basesN; ii+=2) {
            compareReturn(sa[ii/2],sb[ii/2]);
        };
        if (g_bamRemoveDuplicatesMate2basesN%2>0) {
            compareReturn((sa[ii/2]>>4),(sb[ii/2]>>4));
        };
    } else {
        uint32 ii=pa2[5]-g_bamRemoveDuplicatesMate2basesN;
        if (ii%2>0) {
            compareReturn((sa[ii/2]&15),(sb[ii/2]&15));
            ++ii;
        };
        for (; ii<pa2[5]; ii+=2) {
            compareReturn(sa[ii/2],sb[ii/2]);
        };
    };

    return 0;
};

namespace {
// position of the group search in the loaded BAM block
struct DedupScan {
    uint64 bamLength=0;
    uint64 bamS=0, bamE=0, bamE1=1; //start/end/next-end position for read group search
    uint32 rightMax=0;
    uint64 grN=0;//number of reads in group
    bool bamFileEnd=false;//when the last alignment of rhe file was reached
};

// one alignment as seen by the group search
struct ScannedAlign {
    int nMult=0;
    uint32 chrE=0;
    uint32 leftE=0;
    uint32 rightE=0;
    uint32 chrS=0;
};

// reached end of loaded BAM block: write out the processed part and load more BAM data
template <class WriteBlock>
void loadBamBlock(DedupScan &scan, BGZF *bamIn, char *bamRaw, uint64 bamLengthMax, Parameters &P, const WriteBlock &writeBlock) {
    uint64 &bamLength=scan.bamLength, &bamS=scan.bamS, &bamE=scan.bamE, &bamE1=scan.bamE1;
    if (bamLength<bamLengthMax && bamLength>0) {//reached end of BAM file, cannot load more
        scan.bamFileEnd=true;
    } else {
        if (bamS==0 && bamLength>0) {//TODO
            ostringstream errOut;
            errOut <<"EXITING because of fatal ERROR: not enough memory for marking duplicates \n";
            errOut <<"SOLUTION: re-run STAR with at least --limitBAMsortRAM " <<bamLengthMax*2;
            exitWithError(errOut.str(), std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
        };

        //write out processed block
        writeBlock(bamRaw,bamS);

        bamLength-=bamS;
        memmove(bamRaw, bamRaw+bamS,bamLength); //move the non-processed part of the block to the beginning of bamRaw
        bamLength+=bgzf_read(bamIn, bamRaw+bamLength, bamLengthMax-bamLength);//marks the end of the BAM block that has been read
        //restart search for the group
        bamS=0;
        bamE=0;
        bamE1=bamE+loadUnaligned<uint32>(bamRaw+bamE)+4;//next alignment
        scan.rightMax=0;
        scan.grN=0;
    };
}

// point bamA's aux data at a record inside the loaded block
void viewAuxData(bam1_t *bamA, char *bamRecord, const BamFields &bamP) {
    bamA->data=((uint8_t*) bamRecord)+9*4+((bamP[3]<<24)>>24)+((bamP[4]<<16)>>16)*4+(bamP[5]+1)/2+bamP[5];//add length for: core, name, cigar, seq, qual
    bamA->l_data=((uint8_t*) bamRecord)+bamP[0]+1-bamA->data;
}

// read NH and coordinates of the next alignment, and mark it as duplicate (un-marked later if it is the best copy)
ScannedAlign scanAlignment(char *bamRaw, const DedupScan &scan, bam1_t *bamA, Parameters &P) {
    ScannedAlign a;
    char* bamRecord=bamRaw+scan.bamE;
    const BamFields bamP(bamRecord);//1st mate of the pair

    viewAuxData(bamA, bamRecord, bamP);

    uint8_t *tagP = bam_aux_get(bamA,"NH");
    if (tagP==0) {
        ostringstream errOut;
        errOut <<"EXITING because of fatal ERROR: SAM tag NH is missing from a read, but it's required for deduplication. \n";
        errOut <<"SOLUTION: re-generate BAM file with NH and AS tags.";
        exitWithError(errOut.str(), std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
    };
    a.nMult=bam_aux2i(tagP);

    if (a.nMult==1 || (a.nMult>1 && P.removeDuplicates.markMulti))
    {
        storeUnaligned<uint32>(bamRecord+4*4, bamP[4] | (0x400<<16));//mark all aligns as duplicate, will unmark. If multimappers, onyl mark if markMult=true
    };

    a.chrE=bamP[1];
    a.leftE=bamP[2];
    a.rightE=bamP[7];

    a.chrS=loadUnaligned<uint32>(bamRaw+scan.bamS+4*1);
    return a;
}

// collapse one group: sort its pairs, and un-mark the best-scoring pair among identical pairs
void unmarkBestPairs(uint64 *aD, uint64 grN, bam1_t *bamA, Parameters &P) {
    qsort((void*) aD, grN, sizeof(uint64), funCompareNames);
    qsort((void*) aD, grN/2, 2*sizeof(uint64), funCompareCoordFlagCigarSeq);
    //go through the list and select non-duplicates
    int bScore=-999, bP=0;
    for (uint64 pp=0; pp<grN/2; pp++) {
        char* bamRecord1=(char*) aD[pp*2];//1st mate of the pair
        const BamFields bamP1(bamRecord1);
        viewAuxData(bamA, bamRecord1, bamP1);

        uint8_t *tagP = bam_aux_get(bamA,"AS");
        if (tagP==0) {
            ostringstream errOut;
            errOut <<"EXITING because of fatal ERROR: SAM tag AS is missing from a read, but it's required for deduplication. \n";
            errOut <<"SOLUTION: re-generate BAM file with NH and AS tags.";
            exitWithError(errOut.str(), std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
        };
        int score1=bam_aux2i(tagP);
        if (score1>bScore) {
            bScore=score1;
            bP=pp;
        };

        if ( pp==(grN/2-1) || funCompareCoordFlagCigarSeq((void*) (aD+pp*2),(void*) (aD+pp*2+2))!=0 ) {//next pair is not equal to the current one
            //un-mark duplicates
            for (const uint64 mate : {aD[bP*2+1], aD[bP*2]}) {//2nd, then 1st mate of the pair
                char* flagField=(char*) mate+4*4;
                storeUnaligned<uint32>(flagField, loadUnaligned<uint32>(flagField) ^ (0x400<<16));
            };
            bScore=-999;//reset best score
        };
    };
}

// record this unique alignment in the current group, growing the group array when needed
void addToGroup(std::unique_ptr<uint64[]> &aDStorage, uint64 *&aD, uint64 &grNmax, DedupScan &scan, char *bamRaw, const ScannedAlign &a) {
    uint64 &grN=scan.grN;
    if (grN>=grNmax) {//reallocate
        grNmax=grN*2;
        auto aD1=makeUninitArray<uint64>(grNmax);
        memcpy((char*) aD1.get(), (char*) aD, grN*sizeof(uint64));
        aDStorage=std::move(aD1);
        aD=aDStorage.get();
        cerr << "reallocated array "<<grNmax<<endl;
    };
    aD[grN]=(uint64) bamRaw+scan.bamE;
    ++grN;
    if (a.rightE>a.leftE) {//left mate, record coordinate of its right mate
        scan.rightMax=max(scan.rightMax, a.rightE);
    };
}
}

void bamRemoveDuplicates(const string bamFileName, const string bamFileNameOut, Parameters &P) {
    g_bamRemoveDuplicatesMate2basesN=P.removeDuplicates.mate2basesN;

    // bamA only views records inside bamRaw: detach before destruction so htslib
    // never frees memory it does not own.
    const auto releaseView = [](bam1_t *b) { b->data=nullptr; b->m_data=0; bam_destroy1(b); };
    std::unique_ptr<bam1_t, decltype(releaseView)> bamRecord(bam_init1(), releaseView);
    bam1_t *bamA=bamRecord.get();

    std::unique_ptr<BGZF, int(*)(BGZF*)> bamInStorage(bgzf_open(bamFileName.c_str(),"r"), bgzf_close);
    BGZF *bamIn=bamInStorage.get();
    if (bamIn==NULL) {
        exitWithError("EXITING because of fatal ERROR: could not open --inputBAMfile " + bamFileName +
                      "SOLUTION: check that file   " + bamFileName + "   exists and has proper read permissions."
                        , std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
    };
    std::unique_ptr<bam_hdr_t, void(*)(bam_hdr_t*)> bamHeaderStorage(bam_hdr_read(bamIn), bam_hdr_destroy);
    if (!bamHeaderStorage) {
        exitWithError("EXITING because of fatal ERROR: could not read the BAM header of --inputBAMfile " + bamFileName +
                      "\nSOLUTION: check that the file is a valid BAM file.\n", std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    };

    const auto outputFailure = [&](const string &what) {
        exitWithError("EXITING because of fatal OUTPUT error: could not " + what + " " + bamFileNameOut +
                      "\nSOLUTION: check free disk space and write permissions.\n", std::cerr, P.inOut->logMain, EXIT_CODE_FILE_WRITE, P);
    };
    BGZF *bgzfOut=bgzf_open(bamFileNameOut.c_str(),("w"+to_string((long long) P.output.bamCompression)).c_str());
    if (bgzfOut==NULL) outputFailure("create");
    std::unique_ptr<BGZF, int(*)(BGZF*)> bgzfOutStorage(bgzfOut, bgzf_close); // released after a checked close
    if (bam_hdr_write(bgzfOut, bamHeaderStorage.get())<0) outputFailure("write the header of");
    const auto writeBlock = [&](const char *data, uint64 length) {
        if (length>0 && bgzf_write(bgzfOut, data, length)!=(ssize_t)length) outputFailure("write");
    };

    uint64 bamLengthMax=P.limit.BAMsortRAM; //max length to load
    if (bamLengthMax==0) bamLengthMax=16000000000;
    uint64 grNmax=1000000;//max number of alignments

    auto bamRawStorage=makeUninitArray<char>(bamLengthMax); // not zero-filled: up to --limitBAMsortRAM
    char *bamRaw=bamRawStorage.get();
    auto aDStorage=makeUninitArray<uint64>(grNmax*2);
    uint64 *aD=aDStorage.get();

    DedupScan scan;
    while (true) {
        if (scan.bamE1>scan.bamLength) {//reached end of loaded BAM block, add BAM data
            loadBamBlock(scan, bamIn, bamRaw, bamLengthMax, P, writeBlock);
        };

        ScannedAlign a;
        if (!scan.bamFileEnd)
        {
            a=scanAlignment(bamRaw, scan, bamA, P);
        };

        if ( a.chrE !=a.chrS ||  (scan.rightMax>0 && a.leftE>scan.rightMax) || scan.bamFileEnd ) {//found new group of reads to be processed, start collapsing procedure
            unmarkBestPairs(aD, scan.grN, bamA, P);

            //reset for the next group
            if (scan.bamFileEnd) break; //exit the main cycle over blocks
            scan.rightMax=0;
            scan.bamS=scan.bamE;
            scan.grN=0;
        };

        if (a.nMult==1) {//record this alignment in the current group, unique mappers only. Multi-mappers will not be considered for collapsing, and will remain marked as duplicates
            addToGroup(aDStorage, aD, grNmax, scan, bamRaw, a);
        };

        scan.bamE=scan.bamE1;//shift to the next record
        scan.bamE1=scan.bamE+loadUnaligned<uint32>(bamRaw+scan.bamE)+4;//next alignment

    };

    writeBlock(bamRaw,scan.bamLength);
    if (bgzf_flush(bgzfOut)!=0) outputFailure("flush");
    if (bgzf_close(bgzfOutStorage.release())!=0) outputFailure("close");
};
