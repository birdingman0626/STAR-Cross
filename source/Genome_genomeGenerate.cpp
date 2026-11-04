#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <memory>
#include <new>
#include <vector>

#include "Genome.h"

#include "IncludeDefine.h"
#include "Parameters.h"
#include "SuffixArrayFuns.h"
#include "PackedArray.h"
#include "TimeFunctions.h"
#include "ErrorWarning.h"
#include "GTF.h"
#include "SjdbClass.h"
#include "sjdbLoadFromFiles.h"
#include "sjdbPrepare.h"
#include "genomeParametersWrite.h"
#include "sjdbInsertJunctions.h"
#include "genomeScanFastaFiles.h"
#include "genomeSAindex.h"
#include "funCompareSuffixes.h"

#include "serviceFuns.cpp"
#include "streamFuns.h"
#include "SequenceFuns.h"
#include "uninitArray.h"
using namespace std;

static uint64 systemAvailableMemoryBytes()
{
#ifdef __linux__
    ifstream memInfo("/proc/meminfo");
    string field, unit;
    uint64 value=0;

    while (memInfo >> field >> value >> unit) {
        if (field=="MemAvailable:") {
            return value*1024LLU;
        };
    };
#endif
    return 0;
};


inline uint64 funG2strLocus (uint64 SAstr, uint64 const N, char const GstrandBit, uint64 const GstrandMask) {
    bool strandG = (SAstr>>GstrandBit) == 0;
    SAstr &= GstrandMask;
    if ( !strandG ) SAstr += N;
    return SAstr;
};

inline uint64 funSAsortPrefix(char* G, uint64 ii, uint64 prefixLength) {
    uint64 prefix=0;
    for (uint64 jj=0; jj<prefixLength; jj++) {
        uint64 g=(uint64) G[(int64) ii - (int64) jj];
        if (g>5) g=5;
        prefix=prefix*6+g;
    };
    return prefix;
};

inline uint64 funSAsortPrefixAtOffset(char* G, uint64 ii, uint64 offset, uint64 prefixLength) {
    uint64 prefix=0;
    for (uint64 jj=0; jj<prefixLength; jj++) {
        uint64 g=(uint64) G[(int64) ii - (int64) offset - (int64) jj];
        if (g>5) g=5;
        prefix=prefix*6+g;
    };
    return prefix;
};

inline bool funSAsortPrefixFirstWordHas5(uint64 prefix, uint64 prefixLength) {
    uint64 wordBases=(uint64) sizeof(uint64);
    if (prefixLength>wordBases) {
        for (uint64 jj=0; jj<prefixLength-wordBases; jj++) {
            prefix /= 6;
        };
        prefixLength=wordBases;
    };
    for (uint64 jj=0; jj<prefixLength; jj++) {
        if (prefix % 6 == 5) {
            return true;
        };
        prefix /= 6;
    };
    return false;
};

namespace {

void logStage(Parameters &P, const char *message)
{//time-stamped progress line to Log.out and stdout
    time_t rawTime;
    time(&rawTime);
    P.inOut->logMain     << timeMonthDayTime(rawTime) << message <<flush;
    *P.inOut->logStdOut  << timeMonthDayTime(rawTime) << message <<flush;
};

void exitSAerror(Parameters &P, const string &problem)
{
    ostringstream errOut;
    errOut << "EXITING because of FATAL problem while generating the suffix array\n";
    errOut << problem;
    errOut << "SOLUTION: try to re-run suffix array generation, if it still does not work, report this problem to the author\n"<<flush;
    exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
};

// Chunks [iChunk, return value) form one batch of at most batchSize indices (always at least one chunk).
uint64 nextSAchunkBatch(const vector<uint64> &chunkCount, uint64 chunkN, uint64 iChunk, uint64 batchSize, uint64 &batchN)
{
    batchN=0;
    while (iChunk<chunkN && (batchN==0 || batchN+chunkCount[iChunk]<=batchSize)) {
        batchN += chunkCount[iChunk];
        iChunk++;
    };
    return iChunk;
};

} // namespace

// How the SA indices are partitioned by sort prefix into chunks, and where the sorted chunks live.
struct Genome::SAsortPlan {
    uint64 indPrefLen=6, indPrefN=1;
    vector<uint64> prefCount;               //SA indices per sort prefix
    vector<uint64> chunkPrefStart;          //first prefix of each chunk; [chunkN]=indPrefN+1
    vector<uint64> chunkCount, chunkStart;  //indices per chunk and their running offsets
    uint64 chunkN=0, chunkSize=0, batchSize=0;
    bool batchFill=false;                   //batched fill trades one genome scan per chunk for two scans per batch
    bool profile=false;                     //STAR_PROFILE_SA_SORT
    bool inMemory=false;                    //sorted chunks retained in RAM rather than SA_* files
    unique_ptr<uint64[]> inMemoryChunks;
    uint64 genomeScanN=0;
    uint64 suffixLengthWords=0;             //compared suffix length cap in 8-byte words
};

struct Genome::SAsortProfile {
    double fillCountSeconds=0.0, fillScatterSeconds=0.0, sortSeconds=0.0, finalizeSeconds=0.0;
    uint64 skipFirstWordRangeN=0, skipFirstWordIndexN=0;
    uint64 subBinPrefixN=0, subBinRangeN=0, subBinIndexN=0;
};

void Genome::genomeGenerate() {

    checkGenerateParameters();

    logStage(P," ... starting to generate Genome files\n");

    //define some parameters from input parameters
    genomeChrBinNbases=1LLU << pGe.gChrBinNbits;

    nGenome = genomeScanFastaFiles(P,NULL,false,*this);//first scan the fasta file to find all the sizes
    sequenceStorage=genomeSequenceAllocate(nGenome, nG1alloc, G, G1);
    genomeScanFastaFiles(P,G,true,*this);    //load the genome sequence

    uint64 nGenomeTrue=0;
    for (auto &cl : chrLength)
    	nGenomeTrue += cl; //nGenomeTrue = sum of chr lengths

    P.inOut->logMain <<"Genome sequence total length = " << nGenomeTrue << "\n";
    P.inOut->logMain <<"Genome size with padding = "<< nGenome <<"\n";

    SjdbClass sjdbLoci; //will be filled in transcriptGeneSJ below
    GTF mainGTF(*this, P, pGe.gDir, sjdbLoci); //this loads exonLoci and gene/transcript metadata only, sjdbLoci is not filled

    Genome::transformGenome(&mainGTF);

    mainGTF.superTranscript(); //this may change the genome into (Super)Transcriptome

    chrBinFill();//chrBin is first used in the transcriptGeneSJ below
    mainGTF.transcriptGeneSJ(pGe.gDir);

    sjdbLoadFromFiles(P,sjdbLoci);//this will not be transformed. TODO prevent this parameter combination

    if (pGe.gSAindexNbases > log2(nGenomeTrue)/2-1) {
        ostringstream warnOut;
        warnOut << "--genomeSAindexNbases " << pGe.gSAindexNbases << " is too large for the genome size=" << nGenomeTrue;
        warnOut << ", which may cause seg-fault at the mapping step. Re-run genome generation with recommended --genomeSAindexNbases " << int(log2(nGenomeTrue)/2-1);
        warningMessage(warnOut.str(),P.inOut->logMain,std::cerr,P);
    };

    //output genome metadata
    writeChrInfo(pGe.gDir);

    defineSuffixArray(sjdbLoci.chr.size());

    logStage(P," ... starting to sort Suffix Array. This may take a long time...\n");
    sortSuffixArray();
    logStage(P," ... finished generating suffix array\n");

    genomeSAindex(G, SA, P, SAi, *this);

    sjdbN=0;
    if (P.sjdbInsert.yes) {//insert junctions
        P.sjdbInsert.outDir=pGe.gDir;
        P.twoPass.pass2=false;

        Genome genome1(*this, Genome::Snapshot::Borrowed); // insertion-time view; *this owns storage
        sjdbInsertJunctions(P, *this, genome1, sjdbLoci);
    };

    writeGenomeIndex();

    SApass1.deallocateArray();

    logStage(P," ..... finished successfully\n");
};

void Genome::checkGenerateParameters()
{
	createDirectory(pGe.gDir, P.runDirPerm, "--genomeDir", P);

	{//move Log.out file into genome directory
		string logfn=pGe.gDir+"Log.out";
		if ( rename( P.outLogFileName.c_str(), logfn.c_str() ) ) {
			warningMessage("Could not move Log.out file from " + P.outLogFileName + " into " + logfn + ". Will keep " + P.outLogFileName +"\n", \
						   std::cerr, P.inOut->logMain, P);
		} else {
			P.outLogFileName=logfn;
		};
	};
    if (sjdbOverhang<=0 && (pGe.sjdbFileChrStartEnd.at(0)!="-" || pGe.sjdbGTFfile!="-")) {
        ostringstream errOut;
        errOut << "EXITING because of FATAL INPUT PARAMETER ERROR: for generating genome with annotations (--sjdbFileChrStartEnd or --sjdbGTFfile options)\n";
        errOut << "you need to specify >0 --sjdbOverhang\n";
        errOut << "SOLUTION: re-run genome generation specifying non-zero --sjdbOverhang, which ideally should be equal to OneMateLength-1, or could be chosen generically as ~100\n";
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    };

    if (pGe.sjdbFileChrStartEnd.at(0)=="-" && pGe.sjdbGTFfile=="-") {
        if (P.parArray.at(P.pGe.sjdbOverhang_par)->inputLevel>0 && sjdbOverhang>0) {
            ostringstream errOut;
            errOut << "EXITING because of FATAL INPUT PARAMETER ERROR: when generating genome without annotations (--sjdbFileChrStartEnd or --sjdbGTFfile options)\n";
            errOut << "do not specify >0 --sjdbOverhang\n";
            errOut << "SOLUTION: re-run genome generation without --sjdbOverhang option\n";
            exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
        };
        sjdbOverhang=0;
    };
};

void Genome::defineSuffixArray(uint64 sjdbLociN)
{//fill the (-) strand, count SA indices and size the packed SA arrays
    for (uint64 ii=0;ii<nGenome;ii++) {//- strand
        G[2*nGenome-1-ii]=G[ii]<4 ? 3-G[ii] : G[ii];
    };
    nSA=0;
    for (uint64 ii=0;ii<2*nGenome;ii+=pGe.gSAsparseD) {
        if (G[ii]<4) {
            nSA++;
        };
    };

    // GstrandBit
    GstrandBit = (char) (uint64) floor(log(nGenome+P.limit.sjdbInsertNsj*sjdbLength)/log(2))+1; //GstrandBit uses P.limitSjdbInsertNsj even if no insertion requested, in case it will be requested at the mapping stage
    if (GstrandBit<32) GstrandBit=32; //TODO: should not this be 31? Need to test for small genomes. TODO: use simple access function for SA
    P.inOut->logMain <<"Estimated genome size with padding and SJs: total=genome+SJ="<<nGenome+P.limit.sjdbInsertNsj*sjdbLength<<" = "<<nGenome<<" + "<<P.limit.sjdbInsertNsj*sjdbLength<<"\n";
    P.inOut->logMain << "GstrandBit=" << int(GstrandBit) <<"\n";
    GstrandMask = ~(1LLU<<GstrandBit);
    SA.defineBits(GstrandBit+1,nSA);

    if (P.sjdbInsert.yes) {//reserve space for junction insertion
        SApass1.defineBits( GstrandBit+1, nSA+2*sjdbLength*min(sjdbLociN,P.limit.sjdbInsertNsj) );//TODO: this allocation is wasteful, get a better estimate of the number of junctions
    } else {//same as SA
        SApass1.defineBits(GstrandBit+1,nSA);
    };

    P.inOut->logMain  << "Number of SA indices: "<< nSA << "\n"<<flush;
};

void Genome::sortSuffixArray()
{
    for (uint64 ii=0;ii<nGenome;ii++) {//re-fill the array backwards for sorting
        swap(G[2*nGenome-1-ii],G[ii]);
    };
    SAsortPlan plan;
    plan.suffixLengthWords=pGe.gSuffixLengthMax/sizeof(uint64);
    planSAchunks(plan);

    logStage(P," ... sorting Suffix Array chunks...\n");

    chooseSAchunkStorage(plan);

    SAsortProfile prof;
    if (plan.batchFill) {
        uint64 iChunkBatch=0;
        while (iChunkBatch<plan.chunkN) {
            uint64 saBatchN;
            uint64 iChunkBatchNext=nextSAchunkBatch(plan.chunkCount, plan.chunkN, iChunkBatch, plan.batchSize, saBatchN);
            sortSAchunkBatch(plan, prof, iChunkBatch, iChunkBatchNext, saBatchN);
            iChunkBatch=iChunkBatchNext;
        };
    } else {
        sortSAchunksSeparately(plan, prof);
    };

    if (plan.profile) {
        ostringstream saProfileOut;
        saProfileOut << fixed << setprecision(3);
        saProfileOut << "SA sort profile phases: fill_count_seconds=" << prof.fillCountSeconds
                     << "; fill_scatter_seconds=" << prof.fillScatterSeconds
                     << "; sort_seconds=" << prof.sortSeconds
                     << "; finalize_seconds=" << prof.finalizeSeconds;
        P.inOut->logMain << saProfileOut.str() << "\n" <<flush;
        P.inOut->logMain << "SA sort profile comparator: skip_first_word_ranges=" << prof.skipFirstWordRangeN
                          << "; skip_first_word_indices=" << prof.skipFirstWordIndexN << "\n" <<flush;
        P.inOut->logMain << "SA sort profile sub-bins: prefixes=" << prof.subBinPrefixN
                          << "; ranges=" << prof.subBinRangeN
                          << "; indices=" << prof.subBinIndexN << "\n" <<flush;
    };

    logStage(P, plan.inMemory ? " ... packing SA from RAM chunks...\n" : " ... loading chunks from disk, packing SA...\n");
    packSuffixArray(plan);

    for (uint64 ii=0;ii<nGenome;ii++) {//return to normal order for future use
        swap(G[2*nGenome-1-ii],G[ii]);
    };
};

void Genome::planSAchunks(SAsortPlan &plan)
{//count indices per sort prefix and group prefixes into chunks
    // Count sort prefixes over STAR's 0..5 genome alphabet.
    const uint64 saAvailableBytesForPrefix=P.limit.genomeGenerateRAM>nG1alloc ? P.limit.genomeGenerateRAM-nG1alloc : 0;
    if (P.runThreadN>=16 && nGenome>=50000000) {
        uint64 indPrefN8=1;
        for (uint64 ii=0;ii<8;ii++) indPrefN8 *= 6;
        const uint64 indPrefTempBytes8=(uint64) P.runThreadN*indPrefN8*2*sizeof(uint64) + (indPrefN8+1)*sizeof(uint64);
        if (indPrefTempBytes8 < saAvailableBytesForPrefix/4) {
            plan.indPrefLen=8;
        };
    };
    for (uint64 ii=0;ii<plan.indPrefLen;ii++) plan.indPrefN *= 6;
    const uint64 indPrefN=plan.indPrefN;
    vector<uint64> &indPrefCount=plan.prefCount;
    indPrefCount.assign(indPrefN,0);
    plan.profile=getenv("STAR_PROFILE_SA_SORT")!=NULL;
    nSA=0;
    for (uint64 ii=0;ii<2*nGenome;ii+=pGe.gSAsparseD) {
        if (G[ii]<4) {
            uint64 p1=funSAsortPrefix(G,ii,plan.indPrefLen);
            indPrefCount[p1]++;
            nSA++;
        };
    };

    uint64 saChunkSize=(P.limit.genomeGenerateRAM-nG1alloc)/8/P.runThreadN; //number of SA indexes per chunk
    saChunkSize=saChunkSize*6/10; //allow extra space for sorting
    if (P.runThreadN>1) saChunkSize=min(saChunkSize,nSA/(P.runThreadN-1));
    uint64 saChunkN = nSA / saChunkSize + 1;//estimate
    plan.chunkPrefStart.assign(saChunkN*2,0); //start and stop, *2 just in case
    plan.chunkCount.assign(saChunkN*2,0);
    saChunkN=0;//start counting chunks
    uint64 chunkSize1=indPrefCount[0];
    for (uint64 ii=1; ii<indPrefN; ii++) {
        chunkSize1 += indPrefCount[ii];
        if (chunkSize1 > saChunkSize) {
            saChunkN++;
            plan.chunkPrefStart[saChunkN]=ii;
            plan.chunkCount[saChunkN-1]=chunkSize1-indPrefCount[ii];
            chunkSize1=indPrefCount[ii];
        };
    };
    saChunkN++;
    plan.chunkPrefStart[saChunkN]=indPrefN+1;
    plan.chunkCount[saChunkN-1]=chunkSize1;

    plan.chunkStart.assign(saChunkN+1,0);
    for (uint64 iChunk=0; iChunk<saChunkN; iChunk++) {
        plan.chunkStart[iChunk+1]=plan.chunkStart[iChunk]+plan.chunkCount[iChunk];
    };
    plan.chunkN=saChunkN;
    plan.chunkSize=saChunkSize;

    P.inOut->logMain  << "Number of chunks: " << saChunkN <<";   chunks size limit: " << saChunkSize*8 <<" bytes\n" <<flush;
};

void Genome::chooseSAchunkStorage(SAsortPlan &plan)
{//pick batched vs per-chunk fill and RAM vs disk chunk storage
    const uint64 saChunkN=plan.chunkN;
    plan.genomeScanN=(2*nGenome+pGe.gSAsparseD-1)/pGe.gSAsparseD;
    const uint64 saAvailableBytes=P.limit.genomeGenerateRAM>nG1alloc ? P.limit.genomeGenerateRAM-nG1alloc : 0;
    const uint64 saAllChunkScatterBytes=nSA*sizeof(uint64) + (uint64) P.runThreadN*plan.indPrefN*2*sizeof(uint64) + (plan.indPrefN+1)*sizeof(uint64) + (saChunkN+1)*sizeof(uint64);
    plan.batchSize=max(plan.chunkSize*(uint64) P.runThreadN,plan.chunkSize);
    plan.batchFill=P.runThreadN>=16 && saChunkN>1;
    uint64 saChunkBatchN=0;
    if (plan.batchFill) {
        for (uint64 iChunkBatch=0, saBatchN; iChunkBatch<saChunkN; saChunkBatchN++) {
            iChunkBatch=nextSAchunkBatch(plan.chunkCount, saChunkN, iChunkBatch, plan.batchSize, saBatchN);
        };
    };

    const uint64 saRetainedChunkBytes=nSA*sizeof(uint64);
    const uint64 saRamPackPeakBytes=saRetainedChunkBytes+SApass1.lengthByte;
    const uint64 saRamPeakBytes=max(saAllChunkScatterBytes,saRamPackPeakBytes);
    const uint64 saRamHeadroomBytes=max(saRamPeakBytes/10,(uint64) 2000000000LLU);
    const uint64 saRamRequiredBytes=saRamPeakBytes+saRamHeadroomBytes;
    const uint64 systemAvailableBytes=systemAvailableMemoryBytes();
    const bool saRamLimitOK=saAvailableBytes>=saRamRequiredBytes;
    const bool saRamSystemOK=systemAvailableBytes>0 && systemAvailableBytes>=saRamRequiredBytes;
    plan.inMemory=saRamLimitOK && saRamSystemOK;
    P.inOut->logMain  << "SA chunk all-scatter available bytes: " << saAvailableBytes << "; required temporary bytes: " << saAllChunkScatterBytes << "\n" <<flush;
    P.inOut->logMain  << "SA chunk estimated batched-fill batches: " << saChunkBatchN << "\n" <<flush;
    P.inOut->logMain  << "SA chunk fill strategy: " << (plan.batchFill ? "batched" : "per-chunk") << "\n" <<flush;
    P.inOut->logMain  << "SA chunk sort prefix length: " << plan.indPrefLen << "\n" <<flush;
    P.inOut->logMain  << "SA chunk sort granularity: " << (plan.batchFill ? "prefix-bin" : "chunk") << "\n" <<flush;
    P.inOut->logMain  << "SA chunk retained bytes: " << saRetainedChunkBytes << "; RAM peak bytes: " << saRamPeakBytes << "; RAM headroom bytes: " << saRamHeadroomBytes << "\n" <<flush;
    P.inOut->logMain  << "SA chunk system available bytes: " << systemAvailableBytes << "; limit available bytes: " << saAvailableBytes << "\n" <<flush;
    if (plan.profile)
        logSAsortPlan(plan);

    if (plan.inMemory) {
        plan.inMemoryChunks=makeUninitArrayNoThrow<uint64>(nSA);
        if (!plan.inMemoryChunks) {
            plan.inMemory=false;
            P.inOut->logMain << "SA chunk storage fallback: disk-backed after RAM allocation failure\n" <<flush;
        };
    };
    P.inOut->logMain  << "SA chunk storage strategy: " << (plan.inMemory ? "RAM-retained" : "disk-backed") << "\n" <<flush;
};

void Genome::logSAsortPlan(const SAsortPlan &plan)
{//STAR_PROFILE_SA_SORT: prefix-bin and chunk size distribution
    uint64 indPrefEmptyN=0, indPrefSingletonN=0, indPrefNonEmptyN=0, indPrefSortableN=0, indPrefMaxCount=0, indPrefMaxIndex=0;
    for (uint64 ii=0; ii<plan.indPrefN; ii++) {
        if (plan.prefCount[ii]==0) {
            indPrefEmptyN++;
        } else {
            indPrefNonEmptyN++;
            if (plan.prefCount[ii]==1) {
                indPrefSingletonN++;
            } else {
                indPrefSortableN++;
            };
            if (plan.prefCount[ii]>indPrefMaxCount) {
                indPrefMaxCount=plan.prefCount[ii];
                indPrefMaxIndex=ii;
            };
        };
    };

    uint64 saChunkMinCount=plan.chunkCount[0];
    uint64 saChunkMaxCount=plan.chunkCount[0];
    uint64 saChunkMaxIndex=0;
    for (uint64 iChunk=1; iChunk<plan.chunkN; iChunk++) {
        if (plan.chunkCount[iChunk]<saChunkMinCount) {
            saChunkMinCount=plan.chunkCount[iChunk];
        };
        if (plan.chunkCount[iChunk]>saChunkMaxCount) {
            saChunkMaxCount=plan.chunkCount[iChunk];
            saChunkMaxIndex=iChunk;
        };
    };
    P.inOut->logMain << "SA sort profile: enabled\n" <<flush;
    P.inOut->logMain << "SA sort profile prefix bins: total=" << plan.indPrefN
                      << "; empty=" << indPrefEmptyN
                      << "; nonempty=" << indPrefNonEmptyN
                      << "; singleton=" << indPrefSingletonN
                      << "; sortable=" << indPrefSortableN
                      << "; max_bin=" << indPrefMaxCount
                      << "; max_prefix=" << indPrefMaxIndex << "\n" <<flush;
    P.inOut->logMain << "SA sort profile chunks: count=" << plan.chunkN
                      << "; min_indices=" << saChunkMinCount
                      << "; max_indices=" << saChunkMaxCount
                      << "; max_chunk=" << saChunkMaxIndex
                      << "; mean_indices=" << (plan.chunkN>0 ? nSA/plan.chunkN : 0) << "\n" <<flush;
};

void Genome::sortSAchunkBatch(SAsortPlan &plan, SAsortProfile &prof, uint64 iChunkBatch, uint64 iChunkBatchNext, uint64 saBatchN)
{//fill a batch of chunks with two genome scans, then sort and write each chunk
    const uint64 indPrefLen=plan.indPrefLen;
    const uint64 genomeScanN=plan.genomeScanN;
    const bool saSortProfile=plan.profile;
    const bool saChunksInMemoryActive=plan.inMemory;
    const uint64 batchChunkN=iChunkBatchNext-iChunkBatch;
    std::unique_ptr<uint64[]> saBatchStorage;//owned only when chunks are not retained in RAM
    if (!saChunksInMemoryActive)
        saBatchStorage=makeUninitArray<uint64>(saBatchN);
    uint64* saBatch=saChunksInMemoryActive ? plan.inMemoryChunks.get()+plan.chunkStart[iChunkBatch] : saBatchStorage.get();
    vector<uint64> saBatchStart(batchChunkN+1);
    saBatchStart[0]=0;
    for (uint64 iChunk=0;iChunk<batchChunkN;iChunk++) {
        saBatchStart[iChunk+1]=saBatchStart[iChunk]+plan.chunkCount[iChunkBatch+iChunk];
    };

    const uint64 batchPrefStart=plan.chunkPrefStart[iChunkBatch];
    const uint64 batchPrefEnd=min(plan.chunkPrefStart[iChunkBatchNext],plan.indPrefN);
    const uint64 batchPrefN=batchPrefEnd-batchPrefStart;

    vector<uint64> saPrefStart(batchPrefN+1);
    saPrefStart[0]=0;
    for (uint64 iPref=0;iPref<batchPrefN;iPref++) {
        saPrefStart[iPref+1]=saPrefStart[iPref]+plan.prefCount[batchPrefStart+iPref];
    };
    if (saPrefStart[batchPrefN]!=saBatchN) {
        exitSAerror(P, "The number of indices collected for suffix array batch " + to_string(iChunkBatch)
                     + " = " + to_string(saPrefStart[batchPrefN]) + " is not equal to expected " + to_string(saBatchN) + "\n");
    };

    // pass 1: per-thread prefix counts
    vector<uint64> saThreadPrefCount((uint64) P.runThreadN*batchPrefN,0);
    double saProfileTime0=omp_get_wtime();
    #pragma omp parallel num_threads(P.runThreadN)
    {
        int tid=omp_get_thread_num();
        uint64* threadPrefCount=saThreadPrefCount.data()+(uint64) tid*batchPrefN;
        #pragma omp for schedule(static)
        for (int64 iScan=0;iScan<(int64)genomeScanN;iScan++) {//signed index: MSVC OpenMP 2.0
            uint64 ii=(uint64)iScan*pGe.gSAsparseD;
            if (G[ii]<4) {
                uint64 p1=funSAsortPrefix(G,ii,indPrefLen);
                if (p1>=batchPrefStart && p1<batchPrefEnd) {
                    threadPrefCount[p1-batchPrefStart]++;
                };
            };
        };
    };
    if (saSortProfile) {
        prof.fillCountSeconds += omp_get_wtime()-saProfileTime0;
    };

    vector<uint64> saThreadPrefStart((uint64) P.runThreadN*batchPrefN);
    for (uint64 iPref=0;iPref<batchPrefN;iPref++) {
        uint64 prefCount=0;
        for (int iThread=0;iThread<P.runThreadN;iThread++) {
            saThreadPrefStart[(uint64) iThread*batchPrefN+iPref]=saPrefStart[iPref]+prefCount;
            prefCount += saThreadPrefCount[(uint64) iThread*batchPrefN+iPref];
        };
        if (prefCount!=plan.prefCount[batchPrefStart+iPref]) {
            exitSAerror(P, "The number of indices collected for suffix array prefix " + to_string(batchPrefStart+iPref) + " = " + to_string(prefCount)
                         + " is not equal to expected " + to_string(plan.prefCount[batchPrefStart+iPref]) + "\n");
        };
    };

    // pass 2: scatter indices into their prefix bins
    saProfileTime0=omp_get_wtime();
    #pragma omp parallel num_threads(P.runThreadN)
    {
        int tid=omp_get_thread_num();
        uint64* threadPrefStart=saThreadPrefStart.data()+(uint64) tid*batchPrefN;
        #pragma omp for schedule(static)
        for (int64 iScan=0;iScan<(int64)genomeScanN;iScan++) {//signed index: MSVC OpenMP 2.0
            uint64 ii=(uint64)iScan*pGe.gSAsparseD;
            if (G[ii]<4) {
                uint64 p1=funSAsortPrefix(G,ii,indPrefLen);
                if (p1>=batchPrefStart && p1<batchPrefEnd) {
                    uint64 iPref=p1-batchPrefStart;
                    saBatch[threadPrefStart[iPref]]=ii;
                    threadPrefStart[iPref]++;
                };
            };
        };
    };
    if (saSortProfile) {
        prof.fillScatterSeconds += omp_get_wtime()-saProfileTime0;
    };

    // sort every multi-index prefix bin (large bins are split into sub-bins first)
    saProfileTime0=omp_get_wtime();
    vector<uint64*> saSortRange;
    vector<uint64> saSortRangeN;
    vector<char> saSortRangeSkipFirstWord;
    collectSAsortRanges(plan, prof, saBatch, saPrefStart, batchPrefStart, saSortRange, saSortRangeN, saSortRangeSkipFirstWord);

    uint64 skipFirstWordRangeN=0, skipFirstWordIndexN=0;
    #pragma omp parallel for num_threads(P.runThreadN) schedule(dynamic,16) reduction(+:skipFirstWordRangeN,skipFirstWordIndexN)
    for (int iRange=0; iRange < (int) saSortRangeN.size(); iRange++) {
        uint64* saRange=saSortRange[iRange];
        uint64 saRangeN=saSortRangeN[iRange];
        if (saSortRangeSkipFirstWord[iRange]) {
            std::sort(saRange,saRange+saRangeN,CompareSuffixes{G,plan.suffixLengthWords,1});
            if (saSortProfile) {
                skipFirstWordRangeN++;
                skipFirstWordIndexN += saRangeN;
            };
        } else {
            std::sort(saRange,saRange+saRangeN,CompareSuffixes{G,plan.suffixLengthWords,0});
        };
    };
    prof.skipFirstWordRangeN += skipFirstWordRangeN;
    prof.skipFirstWordIndexN += skipFirstWordIndexN;
    if (saSortProfile) {
        prof.sortSeconds += omp_get_wtime()-saProfileTime0;
    };

    saProfileTime0=omp_get_wtime();
    #pragma omp parallel for num_threads(P.runThreadN) schedule(dynamic,1)
    for (int iChunk=0; iChunk < (int) batchChunkN; iChunk++) {
        uint64 iChunkAbs=iChunkBatch+(uint64) iChunk;
        finishSAchunk(plan, saBatch+saBatchStart[iChunk], iChunkAbs);
    };
    if (saSortProfile) {
        prof.finalizeSeconds += omp_get_wtime()-saProfileTime0;
    };
};

void Genome::collectSAsortRanges(const SAsortPlan &plan, SAsortProfile &prof, uint64 *saBatch, const vector<uint64> &saPrefStart, uint64 batchPrefStart,
                                 vector<uint64*> &saSortRange, vector<uint64> &saSortRangeN, vector<char> &saSortRangeSkipFirstWord)
{//split the batch into independent sort ranges, one per prefix bin or per 3-base sub-bin of a large bin
    const uint64 indPrefLen=plan.indPrefLen;
    const uint64 batchPrefN=saPrefStart.size()-1;
    const bool saCanSkipFirstWord=indPrefLen>=(uint64) sizeof(uint64) && plan.suffixLengthWords>1;
    const uint64 saSubBinExtraLen=3;
    const uint64 saSubBinN=216; // 6^3
    const uint64 saSubBinMinN=1000000;
    saSortRange.reserve(batchPrefN);
    saSortRangeN.reserve(batchPrefN);
    saSortRangeSkipFirstWord.reserve(batchPrefN);
    for (uint64 iPref=0; iPref < batchPrefN; iPref++) {
        uint64* saPref=saBatch+saPrefStart[iPref];
        uint64 saPrefN=saPrefStart[iPref+1]-saPrefStart[iPref];
        if (saPrefN<=1)
            continue;

        bool skipFirstWord=saCanSkipFirstWord && !funSAsortPrefixFirstWordHas5(batchPrefStart+iPref,indPrefLen);
        bool subBinned=false;
        if (skipFirstWord && saPrefN>=saSubBinMinN) {
            uint64 saSubBinCount[saSubBinN];
            memset(saSubBinCount,0,sizeof(saSubBinCount));
            bool subBinOK=true;
            for (uint64 ii=0; ii<saPrefN; ii++) {
                if (saPref[ii]<indPrefLen+saSubBinExtraLen-1) {
                    subBinOK=false;
                    break;
                };
                saSubBinCount[funSAsortPrefixAtOffset(G,saPref[ii],indPrefLen,saSubBinExtraLen)]++;
            };

            uint64 saSubBinNonEmptyN=0;
            uint64 saSubBinStart[saSubBinN+1];
            saSubBinStart[0]=0;
            if (subBinOK) {
                for (uint64 ii=0; ii<saSubBinN; ii++) {
                    if (saSubBinCount[ii]>0) {
                        saSubBinNonEmptyN++;
                    };
                    saSubBinStart[ii+1]=saSubBinStart[ii]+saSubBinCount[ii];
                };
            };

            std::unique_ptr<uint64[]> saPrefTmp;
            if (saSubBinNonEmptyN>1) {
                saPrefTmp=makeUninitArrayNoThrow<uint64>(saPrefN);
            };
            if (saPrefTmp) {
                uint64 saSubBinCursor[saSubBinN];
                memcpy(saSubBinCursor,saSubBinStart,sizeof(saSubBinCursor));
                for (uint64 ii=0; ii<saPrefN; ii++) {
                    uint64 iSub=funSAsortPrefixAtOffset(G,saPref[ii],indPrefLen,saSubBinExtraLen);
                    saPrefTmp[saSubBinCursor[iSub]]=saPref[ii];
                    saSubBinCursor[iSub]++;
                };
                memcpy(saPref,saPrefTmp.get(),saPrefN*sizeof(saPref[0]));
                saPrefTmp.reset();

                uint64 saSubBinSortRangeN=0;
                for (uint64 iSub=0; iSub<saSubBinN; iSub++) {
                    uint64 saSubN=saSubBinStart[iSub+1]-saSubBinStart[iSub];
                    if (saSubN>1) {
                        saSortRange.push_back(saPref+saSubBinStart[iSub]);
                        saSortRangeN.push_back(saSubN);
                        saSortRangeSkipFirstWord.push_back(1);
                        saSubBinSortRangeN++;
                    };
                };
                if (plan.profile) {
                    prof.subBinPrefixN++;
                    prof.subBinRangeN += saSubBinSortRangeN;
                    prof.subBinIndexN += saPrefN;
                };
                subBinned=true;
            };
        };
        if (!subBinned) {
            saSortRange.push_back(saPref);
            saSortRangeN.push_back(saPrefN);
            saSortRangeSkipFirstWord.push_back(skipFirstWord ? 1 : 0);
        };
    };
};

void Genome::sortSAchunksSeparately(SAsortPlan &plan, SAsortProfile &prof)
{//each thread scans the genome for its own chunk, sorts it and stores it
    const uint64 indPrefLen=plan.indPrefLen;
    const bool saSortProfile=plan.profile;
    const bool saChunksInMemoryActive=plan.inMemory;
    double fillScatterSeconds=0.0, sortSeconds=0.0, finalizeSeconds=0.0;
    #pragma omp parallel for num_threads(P.runThreadN) schedule(dynamic,1) reduction(+:fillScatterSeconds,sortSeconds,finalizeSeconds)
    for (int iChunk=0; iChunk < (int) plan.chunkN; iChunk++) {//start the chunk cycle: sort each chunk and write to a file
        const uint64 chunkCount=plan.chunkCount[iChunk];
        std::unique_ptr<uint64[]> saChunkStorage;//local array for each chunk unless chunks are retained in RAM
        if (!saChunksInMemoryActive)
            saChunkStorage=makeUninitArray<uint64>(chunkCount);
        uint64* saChunk=saChunksInMemoryActive ? plan.inMemoryChunks.get()+plan.chunkStart[iChunk] : saChunkStorage.get();
        double saProfileTime0=omp_get_wtime();
        for (uint64 ii=0,jj=0;ii<2*nGenome;ii+=pGe.gSAsparseD) {//fill the chunk with SA indices
            if (G[ii]<4) {
                uint64 p1=funSAsortPrefix(G,ii,indPrefLen);
                if (p1>=plan.chunkPrefStart[iChunk] && p1<plan.chunkPrefStart[iChunk+1]) {
                    saChunk[jj]=ii;
                    jj++;
                    if (jj==chunkCount) break;
                };
            };
        };
        if (saSortProfile) {
            fillScatterSeconds += omp_get_wtime()-saProfileTime0;
        };

        saProfileTime0=omp_get_wtime();
        if (chunkCount>1) {
            std::sort(saChunk,saChunk+chunkCount,CompareSuffixes{G,plan.suffixLengthWords,0});
        };
        if (saSortProfile) {
            sortSeconds += omp_get_wtime()-saProfileTime0;
        };

        saProfileTime0=omp_get_wtime();
        finishSAchunk(plan, saChunk, (uint64) iChunk);
        saChunkStorage.reset();
        if (saSortProfile) {
            finalizeSeconds += omp_get_wtime()-saProfileTime0;
        };
    };
    prof.fillScatterSeconds += fillScatterSeconds;
    prof.sortSeconds += sortSeconds;
    prof.finalizeSeconds += finalizeSeconds;
};

void Genome::finishSAchunk(const SAsortPlan &plan, uint64 *saChunk, uint64 iChunk)
{//convert a sorted chunk from reversed-genome to forward coordinates; write it out unless retained in RAM
    const uint64 chunkCount=plan.chunkCount[iChunk];
    for (uint64 ii=0;ii<chunkCount;ii++) {
        saChunk[ii]=2*nGenome-1-saChunk[ii];
    };
    if (!plan.inMemory) {
        string chunkFileName=pGe.gDir+"/SA_"+to_string(iChunk);
        ofstream saChunkFile = ofstrOpenOwned(chunkFileName,ERROR_OUT, P);
        fstreamWriteBig(saChunkFile, (char*) saChunk, sizeof(saChunk[0])*chunkCount,chunkFileName,ERROR_OUT,P);
        ofstrClose(saChunkFile, chunkFileName, ERROR_OUT, P);
    };
};

void Genome::packSuffixArray(SAsortPlan &plan)
{//read the sorted chunks in order and pack them into the full SA
    SApass1.allocateArray();
    SA.pointArray(SApass1.charArray + SApass1.lengthByte-SA.lengthByte); //SA is shifted to have space for junction insertion
    const uint64 N2bit= 1LLU << GstrandBit;
    uint64 packedInd=0;

    if (plan.inMemory) {//pack directly from RAM-retained chunks (upstream PR #2687)
        const uint64 *saChunks=plan.inMemoryChunks.get();
        for (uint64 ii=0; ii<nSA; ii++) {
            SA.writePacked( ii, (saChunks[ii]<nGenome) ? saChunks[ii] : ( (saChunks[ii]-nGenome) | N2bit ) );
        };
        packedInd=nSA;
        plan.inMemoryChunks.reset();
    } else {//disk-backed fallback: load chunk files one by one and convert to packed
        constexpr uint64 saChunkBlockSize=10000000;
        auto saIn=makeUninitArray<uint64>(saChunkBlockSize);

        for (uint64 iChunk=0;iChunk<plan.chunkN;iChunk++) {//load files one by one and convert to packed
            string chunkFileName=pGe.gDir+"/SA_"+to_string(iChunk);
            ifstream saChunkFile(chunkFileName.c_str(), std::ios::binary);//binary mode: SA chunks are raw binary, must not be text-translated on Windows
            while (! saChunkFile.eof()) {//read blocks from each file
                uint64 chunkBytesN=fstreamReadBig(saChunkFile,(char*) saIn.get(),saChunkBlockSize*sizeof(saIn[0]));
                for (uint64 ii=0;ii<chunkBytesN/sizeof(saIn[0]);ii++) {
                    SA.writePacked( packedInd+ii, (saIn[ii]<nGenome) ? saIn[ii] : ( (saIn[ii]-nGenome) | N2bit ) );
                };
                packedInd += chunkBytesN/sizeof(saIn[0]);
            };
            saChunkFile.close();
            remove(chunkFileName.c_str());//remove the chunk file
        };
    };

    if (packedInd != nSA ) {
        exitSAerror(P, "The number of indices read from chunks = "+to_string(packedInd)+" is not equal to expected nSA="+to_string(nSA)+"\n");
    };
};

void Genome::writeGenomeIndex()
{//genomeParameters.txt, Genome, SA and SAindex
    pGe.gFileSizes.clear();
    pGe.gFileSizes.push_back(nGenome);
    pGe.gFileSizes.push_back(SA.lengthByte);

    genomeParametersWrite(pGe.gDir+("/genomeParameters.txt"), P, ERROR_OUT, *this);

    logStage(P," ... writing Genome to disk ...\n");
    writeGenomeSequence(pGe.gDir);

    P.inOut->logMain  << "SA size in bytes: "<<SA.lengthByte << "\n"<<flush;
    logStage(P," ... writing Suffix Array to disk ...\n");

    ofstream SAout = ofstrOpenOwned(pGe.gDir+"/SA",ERROR_OUT, P);
    fstreamWriteBig(SAout,(char*) SA.charArray, (streamsize) SA.lengthByte,pGe.gDir+"/SA",ERROR_OUT,P);
    ofstrClose(SAout, pGe.gDir+"/SA", ERROR_OUT, P);

    logStage(P," ... writing SAindex to disk\n");

    ofstream SAiOut = ofstrOpenOwned(pGe.gDir+"/SAindex",ERROR_OUT, P);
    fstreamWriteBig(SAiOut, (char*) &pGe.gSAindexNbases, sizeof(pGe.gSAindexNbases),pGe.gDir+"/SAindex",ERROR_OUT,P);
    fstreamWriteBig(SAiOut, (char*) genomeSAindexStart, sizeof(genomeSAindexStart[0])*(pGe.gSAindexNbases+1),pGe.gDir+"/SAindex",ERROR_OUT,P);
    fstreamWriteBig(SAiOut,  SAi.charArray, SAi.lengthByte,pGe.gDir+"/SAindex",ERROR_OUT,P);
    ofstrClose(SAiOut, pGe.gDir+"/SAindex", ERROR_OUT, P);
};

void Genome::writeChrInfo(const string dirOut) 
{//write chr information
    ofstream chrN = ofstrOpenOwned(dirOut+"/chrName.txt",ERROR_OUT, P);
    ofstream chrS = ofstrOpenOwned(dirOut+"/chrStart.txt",ERROR_OUT, P);
    ofstream chrL = ofstrOpenOwned(dirOut+"/chrLength.txt",ERROR_OUT, P);
    ofstream chrNL = ofstrOpenOwned(dirOut+"/chrNameLength.txt",ERROR_OUT, P);

    for (uint64 ii=0;ii<nChrReal;ii++) {//output names, starts, lengths
        chrN<<chrName[ii]<<"\n";
        chrS<<chrStart[ii]<<"\n";
        chrL<<chrLength.at(ii)<<"\n";
        chrNL<<chrName[ii]<<"\t"<<chrLength.at(ii)<<"\n";
    };
    chrS<<chrStart[nChrReal]<<"\n";//size of the genome
    ofstrClose(chrN, dirOut+"/chrName.txt", ERROR_OUT, P);
    ofstrClose(chrL, dirOut+"/chrLength.txt", ERROR_OUT, P);
    ofstrClose(chrS, dirOut+"/chrStart.txt", ERROR_OUT, P);
    ofstrClose(chrNL, dirOut+"/chrNameLength.txt", ERROR_OUT, P);
};
void Genome::writeGenomeSequence(const string dirOut) 
{//write genome sequence
    ofstream genomeOutStream = ofstrOpenOwned(dirOut+"/Genome",ERROR_OUT, P);
    fstreamWriteBig(genomeOutStream,G,nGenome,dirOut+"/Genome",ERROR_OUT,P);
    ofstrClose(genomeOutStream, dirOut+"/Genome", ERROR_OUT, P);
};
