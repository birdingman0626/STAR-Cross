#include "signalFromBAM.h"
#include "ErrorWarning.h"
#include <iostream>
#include <memory>
#include <regex>
#include <vector>
using namespace std;
namespace {
using BgzfHandle=std::unique_ptr<BGZF, int(*)(BGZF*)>;
using BamHeader=std::unique_ptr<bam_hdr_t, void(*)(bam_hdr_t*)>;
using SignalFiles=std::vector<std::unique_ptr<ofstream>>;
// Open the input BAM and read its header, or exit cleanly (a missing file used to crash).
std::pair<BgzfHandle, BamHeader> openInputBAM(const string &bamFileName, Parameters &P) {
    BgzfHandle in(bgzf_open(bamFileName.c_str(),"r"), bgzf_close);
    if (!in)
        exitWithError("EXITING because of fatal ERROR: could not open --inputBAMfile " + bamFileName +
                      "\nSOLUTION: check that the file exists and has proper read permissions.\n",
                      std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    BamHeader header(bam_hdr_read(in.get()), bam_hdr_destroy);
    if (!header)
        exitWithError("EXITING because of fatal ERROR: could not read the BAM header of " + bamFileName +
                      "\nSOLUTION: check that the file is a valid BAM file.\n",
                      std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    return {std::move(in), std::move(header)};
}

// A read error or failed close means part of the input was not seen: fatal, not a short signal file.
void inputBAMfailure(const string &what, const string &bamFileName, Parameters &P) {
    exitWithError("EXITING because of fatal INPUT FILE error: could not " + what + " --inputBAMfile " + bamFileName +
                  "\nSOLUTION: check that the file is a complete, valid BAM file.\n",
                  std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
}

// bam_read1: >=0 record size, -1 end of file, < -1 truncated or corrupt record
bool readRecord(BGZF *bamIn, bam1_t *bamA, const string &bamFileName, Parameters &P) {
    const int bamBytes1=bam_read1(bamIn, bamA);
    if (bamBytes1 < -1) inputBAMfailure("read a record of", bamFileName, P);
    return bamBytes1 >= 0;
}

void closeInputBAM(BgzfHandle &in, const string &bamFileName, Parameters &P) {
    if (bgzf_close(in.release())!=0) inputBAMfailure("close", bamFileName, P);
}

bool referenceExcluded(const Parameters &P, const char *targetName) {//reference does not match required references
    return P.outWigReferencesPrefix!="-" && (P.outWigReferencesPrefix.compare(0,P.outWigReferencesPrefix.size(),targetName,P.outWigReferencesPrefix.size())!=0);
}

// count unique and (fractional) multi-mapping reads in the BAM file, for normalization
void countNormalizationReads(const string &bamFileName, Parameters &P, bam1_t *bamA, double &nUniq, double &nMult) {
    auto countInput=openInputBAM(bamFileName, P);
    BGZF *bamIn=countInput.first.get();
    bam_hdr_t *bamHeader=countInput.second.get();
    while ( readRecord(bamIn, bamA, bamFileName, P) ) {//until the end of file
        if (bamA->core.tid<0) continue; //unmapped read
        if ( referenceExcluded(P, bamHeader->target_name[bamA->core.tid]) ) continue; //reference does not match required references

        uint8_t* aNHp=bam_aux_get(bamA,"NH");
        if (aNHp!=NULL) {
            uint32_t aNH=bam_aux2i(aNHp);
            if (aNH==1) {//unique mappers
                ++nUniq;
            } else if (aNH>1) {
                nMult+=1.0/aNH;
            };
        };
    };
    closeInputBAM(countInput.first, bamFileName, P);
}

void openSignalFiles(const string &sigFileName, Parameters &P, int sigN, std::vector<string> &sigOutFileName, SignalFiles &sigOutAll) {
    sigOutFileName[0]=sigFileName+".Unique.str1.out";
    sigOutFileName[1]=sigFileName+".UniqueMultiple.str1.out";
    if (P.outWigFlags.strand) {
        sigOutFileName[2]=sigFileName+".Unique.str2.out";
        sigOutFileName[3]=sigFileName+".UniqueMultiple.str2.out";
    };

    for (int ii=0; ii<sigN; ii++) {
        sigOutFileName[ii]+= (P.outWigFlags.format==0 ? ".bg" : ".wig");
        sigOutAll[ii].reset(new ofstream ( sigOutFileName[ii].c_str() ));
        if (!*sigOutAll[ii])
            exitWithError("EXITING because of fatal OUTPUT error: could not create signal file " + sigOutFileName[ii] +
                          "\nSOLUTION: check the output path and write permissions.\n",
                          std::cerr, P.inOut->logMain, EXIT_CODE_FILE_OPEN, P);
    };
}

void setNormFactors(Parameters &P, int sigN, double nUniq, double nMult, std::vector<double> &normFactor, SignalFiles &sigOutAll) {
    if (P.outWigFlags.norm==0) {//raw counts
        normFactor[0]=1;
        normFactor[1]=1;
    } else if (P.outWigFlags.norm==1) {//normlaized
        normFactor[0]=1.0e6 / nUniq;
        normFactor[1]=1.0e6 / (nUniq+nMult);
        for (int is=0;is<sigN;is++) {//formatting double output
            *sigOutAll[is]<<setiosflags(ios::fixed) << setprecision(5);
        };
    };
    if (P.outWigFlags.strand) {
        normFactor[2]=normFactor[0];
        normFactor[3]=normFactor[1];
    };
}

// output the signal of one chromosome to all signal files
void writeChromosomeSignal(Parameters &P, int sigN, const char *chrName, const std::vector<double> &sigAll, uint32_t chrLen,
                           const std::vector<double> &normFactor, SignalFiles &sigOutAll) {
    for (int is=0;is<sigN;is++) {
        if (P.outWigFlags.format==1) {
            *sigOutAll[is] <<"variableStep chrom="<<chrName <<"\n";
        };
        double prevSig=0;
        for (uint32_t ig=0;ig<chrLen;ig++) {
            double newSig=sigAll[sigN*ig+is];
            if (P.outWigFlags.format==0) {//bedGraph
                if (newSig!=prevSig) {
                    if (prevSig!=0) {//finish previous record
                        *sigOutAll[is] <<ig<<"\t"<<prevSig*normFactor[is] <<"\n"; //1-based end
                    };
                    if (newSig!=0) {
                        *sigOutAll[is] << chrName <<"\t"<< ig <<"\t"; //0-based beginning
                    };
                    prevSig=newSig;
                };
            } else if (P.outWigFlags.format==1){//wiggle
                if (newSig!=0) {
                    *sigOutAll[is] <<ig+1<<"\t"<<newSig*normFactor[is] <<"\n";
                };
            };
        };
    };
}

#define BAM_CIGAR_OperationShift 4
#define BAM_CIGAR_LengthBits 28
#define BAM_CIGAR_M 0
#define BAM_CIGAR_I 1
#define BAM_CIGAR_D 2
#define BAM_CIGAR_N 3
#define BAM_CIGAR_S 4
#define BAM_CIGAR_H 5
#define BAM_CIGAR_P 6
#define BAM_CIGAR_EQ 7
#define BAM_CIGAR_X 8

// add one alignment to the signal of its chromosome
void addAlignmentSignal(const bam1_t *bamA, Parameters &P, int sigN, std::vector<double> &sigAll, uint32_t chrLen) {
    //by default, alignments marked as duplicate are not processed
    if ( (bamA->core.flag & 0x400) > 0 ) return;

    //NH attribute
    uint8_t* aNHp=bam_aux_get(bamA,"NH");
    uint32_t aNH;
    if (aNHp==NULL) {
        aNH=1; //no NH tag: assume NH=1
    } else {
        aNH=bam_aux2i(bam_aux_get(bamA,"NH")); //write a safer function allowing for lacking NH tag
    };
    if (aNH==0) return; //do not process lines without NH=0
    uint32_t aG=bamA->core.pos;
    uint32_t iStrand=0;
    if (P.outWigFlags.strand) {//strand for stranded data from SAM flag
        iStrand= ( (bamA->core.flag & 0x10) > 0 ) == ( (bamA->core.flag & 0x80) == 0 );//0/1 for +/-
    };
    if (P.outWigFlags.type==1) {//5' of the1st read signal only, RAMPAGE/CAGE
        if ( (bamA->core.flag & 0x80)>0) return; //skip if this the second mate
        if (iStrand==0) {
            if (aNH==1) {//unique mappers
                sigAll[aG*sigN+0+2*iStrand]++;
            };
            sigAll[aG*sigN+1+2*iStrand]+=1.0/aNH;//U+M, normalized by the number of multi-mapping loci
            return; //record only the first position
        };
    };

    uint32_t* cigar=(uint32_t*) (bamA->data+bamA->core.l_qname);

    for (uint32_t ic=0; ic<bamA->core.n_cigar; ic++) {
        uint32_t cigOp=(cigar[ic]<<BAM_CIGAR_LengthBits)>>BAM_CIGAR_LengthBits;
        uint32_t cigL=cigar[ic]>>BAM_CIGAR_OperationShift;
        switch (cigOp) {
            case(BAM_CIGAR_D):
            case(BAM_CIGAR_N):
                aG+=cigL;
                break;
            case(BAM_CIGAR_M):
                if (P.outWigFlags.type==0 || (P.outWigFlags.type==2 && (bamA->core.flag & 0x80)>0 )) {//full signal, or second mate onyl signal
                    for (uint32_t ig=0;ig<cigL;ig++) {
                        if (aG>=chrLen) {
                            exitWithError("BUG: alignment extends past chromosome in signalFromBAM.cpp", std::cerr, P.inOut->logMain, EXIT_CODE_BUG, P);
                        };
                        if (aNH==1) {//unique mappers
                            sigAll[aG*sigN+0+2*iStrand]++;
                        };
                        sigAll[aG*sigN+1+2*iStrand]+=1.0/aNH;//U+M, normalized by the number of multi-mapping loci
                        aG++;
                    };
                } else {
                    aG+=cigL;
                };
        };
    };
    if (P.outWigFlags.type==1) {//full signal
        --aG;
        if (aNH==1) {//unique mappers
            sigAll[aG*sigN+0+2*iStrand]++;
        };
        sigAll[aG*sigN+1+2*iStrand]+=1.0/aNH;//U+M, normalized by the number of multi-mapping loci
    };
}

// flush/close all signal files; a failed write is fatal, not a truncated file
void closeSignalFiles(Parameters &P, int sigN, const std::vector<string> &sigOutFileName, SignalFiles &sigOutAll) {
    for (int is=0; is<sigN; is++) {
        sigOutAll[is]->flush();
        sigOutAll[is]->close();
        if (sigOutAll[is]->fail())
            exitWithError("EXITING because of fatal OUTPUT error: could not write signal file " + sigOutFileName[is] +
                          "\nSOLUTION: check free disk space and write permissions.\n",
                          std::cerr, P.inOut->logMain, EXIT_CODE_FILE_WRITE, P);
    };
}
}
void signalFromBAM(const string bamFileName, const string sigFileName, Parameters &P) {

    std::unique_ptr<bam1_t, void(*)(bam1_t*)> bamRecord(bam_init1(), bam_destroy1);
    bam1_t *bamA=bamRecord.get();

    double nMult=0, nUniq=0;

    if (P.outWigFlags.norm==1) {//count reads in the BAM file
        countNormalizationReads(bamFileName, P, bamA, nUniq, nMult);
    };

    auto signalInput=openInputBAM(bamFileName, P);
    BGZF *bamIn=signalInput.first.get();
    bam_hdr_t *bamHeader=signalInput.second.get();

    int sigN=P.outWigFlags.strand ? 4 : 2;

    std::vector<double> normFactor(sigN);

    SignalFiles sigOutAll(sigN);

    std::vector<string> sigOutFileName(sigN);
    openSignalFiles(sigFileName, P, sigN, sigOutFileName, sigOutAll);

    setNormFactors(P, sigN, nUniq, nMult, normFactor, sigOutAll);

    int iChr=-999;
    std::vector<double> sigAll; // per-chromosome signal, zeroed for each new chromosome
    uint32_t chrLen=0;
    while ( true ) {//until the end of file
        const bool gotRecord=readRecord(bamIn, bamA, bamFileName, P);
        if (bamA->core.tid!=iChr || !gotRecord) {
            //output to file
            if (iChr!=-999) {//iChr=-999 marks chromosomes that are not output, including unmapped reads
                writeChromosomeSignal(P, sigN, bamHeader->target_name[iChr], sigAll, chrLen, normFactor, sigOutAll);
            };
            if (!gotRecord) {//no more reads
                break;
            };

            iChr=bamA->core.tid;
            if ( iChr==-1 || referenceExcluded(P, bamHeader->target_name[bamA->core.tid]) ) {
                iChr=-999;
                continue; //reference does not match required references
            };

            chrLen=bamHeader->target_len[iChr]+1;//one extra base at the end which sohuld always be 0
            sigAll.assign((size_t)sigN*chrLen, 0.0);
        };

        addAlignmentSignal(bamA, P, sigN, sigAll, chrLen);
    };
    closeInputBAM(signalInput.first, bamFileName, P);
    closeSignalFiles(P, sigN, sigOutFileName, sigOutAll);
};
