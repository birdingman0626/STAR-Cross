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

    uint64 bamLengthMax=P.limitBAMsortRAM; //max length to load
    if (bamLengthMax==0) bamLengthMax=16000000000;
    uint64 grNmax=1000000;//max number of alignments

    std::unique_ptr<char[]> bamRawStorage(new char[bamLengthMax]); // not zero-filled: up to --limitBAMsortRAM
    char *bamRaw=bamRawStorage.get();
    std::unique_ptr<uint64[]> aDStorage(new uint64[grNmax*2]);
    uint64 *aD=aDStorage.get();

    uint64 bamLength=0;
    uint64 bamS=0, bamE=0, bamE1=1; //start/end/next-end position for read group search
    uint32 rightMax=0;
    uint64 grN=0;//number of reads in group
    bool bamFileEnd=false;//when the last alignment of rhe file was reached
    while (true) {
        if (bamE1>bamLength) {//reached end of loaded BAM block, add BAM data
            if (bamLength<bamLengthMax && bamLength>0) {//reached end of BAM file, cannot load more
                bamFileEnd=true;
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
                rightMax=0;
                grN=0;
            };
        };

        int nMult=0;
        uint32 chrE=0;
        uint32 leftE=0;
        uint32 rightE=0;
        uint32 chrS=0;

        if (!bamFileEnd)
        {
            char* bamRecord=bamRaw+bamE;
            const BamFields bamP(bamRecord);//1st mate of the pair

            bamA->data=((uint8_t*) bamRecord)+9*4+((bamP[3]<<24)>>24)+((bamP[4]<<16)>>16)*4+(bamP[5]+1)/2+bamP[5];//add length for: core, name, cigar, seq, qual
            bamA->l_data=((uint8_t*) bamRecord)+bamP[0]+1-bamA->data;

            uint8_t *tagP = bam_aux_get(bamA,"NH");
            if (tagP==0) {
                ostringstream errOut;
                errOut <<"EXITING because of fatal ERROR: SAM tag NH is missing from a read, but it's required for deduplication. \n";
                errOut <<"SOLUTION: re-generate BAM file with NH and AS tags.";
                exitWithError(errOut.str(), std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
            };
            nMult=bam_aux2i(tagP);

            if (nMult==1 || (nMult>1 && P.removeDuplicates.markMulti))
            {
                storeUnaligned<uint32>(bamRecord+4*4, bamP[4] | (0x400<<16));//mark all aligns as duplicate, will unmark. If multimappers, onyl mark if markMult=true
            };

            chrE=bamP[1];
            leftE=bamP[2];
            rightE=bamP[7];

            chrS=loadUnaligned<uint32>(bamRaw+bamS+4*1);
        };

        if ( chrE !=chrS ||  (rightMax>0 && leftE>rightMax) || bamFileEnd ) {//found new group of reads to be processed, start collapsing procedure
            qsort((void*) aD, grN, sizeof(uint64), funCompareNames);
            qsort((void*) aD, grN/2, 2*sizeof(uint64), funCompareCoordFlagCigarSeq);
            //go through the list and select non-duplicates
            int bScore=-999, bP=0;
            for (uint64 pp=0; pp<grN/2; pp++) {
                char* bamRecord1=(char*) aD[pp*2];//1st mate of the pair
                const BamFields bamP1(bamRecord1);
                bamA->data=((uint8_t*) bamRecord1)+9*4+((bamP1[3]<<24)>>24)+((bamP1[4]<<16)>>16)*4+(bamP1[5]+1)/2+bamP1[5];//add length for: core, name, cigar, seq, qual
                bamA->l_data=((uint8_t*) bamRecord1)+bamP1[0]+1-bamA->data;

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
                    //cout << ((char*)(bamPb+9)) <<"\n";
                    bScore=-999;//reset best score
                };
            };


            //reset for the next group
            if (bamFileEnd) break; //exit the main cycle over blocks
            rightMax=0;
            bamS=bamE;
            grN=0;
        };

        if (nMult==1) {//record this alignment in the current group, unique mappers only. Multi-mappers will not be considered for collapsing, and will remain marked as duplicates
            if (grN>=grNmax) {//reallocate
                grNmax=grN*2;
                std::unique_ptr<uint64[]> aD1(new uint64[grNmax]);
                memcpy((char*) aD1.get(), (char*) aD, grN*sizeof(uint64));
                aDStorage=std::move(aD1);
                aD=aDStorage.get();
                cerr << "reallocated array "<<grNmax<<endl;
            };
            aD[grN]=(uint64) bamRaw+bamE;
            ++grN;
            if (rightE>leftE) {//left mate, record coordinate of its right mate
                rightMax=max(rightMax, rightE);
            };
        };

        bamE=bamE1;//shift to the next record
        bamE1=bamE+loadUnaligned<uint32>(bamRaw+bamE)+4;//next alignment

    };

    writeBlock(bamRaw,bamLength);
    if (bgzf_flush(bgzfOut)!=0) outputFailure("flush");
    if (bgzf_close(bgzfOutStorage.release())!=0) outputFailure("close");
};
