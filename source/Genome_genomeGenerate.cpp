#include <cmath>
#include <cstdlib>
#include <new>

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

#include "serviceFuns.cpp"
#include "streamFuns.h"
#include "SequenceFuns.h"


char* globalG;
uint64 globalL;

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


// Compare two suffixes starting from word offset wordStart (upstream PR #2687:
// callers that share a known-equal prefix word skip it via wordStart=1). The
// genome word loads go through loadUintLE so the comparison is byte-order safe
// (native single-instruction load on little-endian; portable on big-endian).
inline int funCompareSuffixesFromWord ( const void *a, const void *b, uint64 wordStart){

    const char *ga=((globalG-7LLU)+(*((uint64*)a))) - 8*wordStart;
    const char *gb=((globalG-7LLU)+(*((uint64*)b))) - 8*wordStart;

    uint64 jj=0;
    int  ii=0;
    uint64 va=0,vb=0;

    while (jj+wordStart < globalL) {
        va=loadUintLE(ga-8*jj); // little-endian 8-byte load (native on LE)
        vb=loadUintLE(gb-8*jj);

        #define has5(v) ((((v)^0x0505050505050505) - 0x0101010101010101) & ~((v)^0x0505050505050505) & 0x8080808080808080)

        if (has5(va) && has5(vb))
        {//there is 5 in the sequence - only compare bytes before 5
            for (ii=7;ii>=0;ii--)
            {//extract byte ii via shift: portable regardless of host byte order
                uchar va1=(uchar)((va>>(8*ii))&0xffLLU);
                uchar vb1=(uchar)((vb>>(8*ii))&0xffLLU);
                if (va1>vb1)
                {
                    return 1;
                } else if (va1<vb1)
                {
                    return -1;
                } else if (va1==5)
                {//va=vb at the end of chr
                    if ( *((uint64*)a) > *((uint64*)b) )
                    {//anti-stable order,since indexes are sorted in the reverse order
                        return  -1;
                    } else
                    {//a cannot be equal to b
                        return 1;
                    };
                };
            };
        } else
        {//no 5, simple comparison
            if (va>vb)
            {
                return 1;
            } else if (va<vb)
            {
                return -1;
            };
        };
        jj++;
    };

    //suffixes are equal up to globalL, no simply compare the indexes
    if ( *((uint64*)a) > *((uint64*)b) )
    {//anti-stable order,since indexes are sorted in the reverse order
        return  -1;
    } else
    {//a cannot be equal to b
        return 1;
    };
};

inline int funCompareSuffixes ( const void *a, const void *b){
    return funCompareSuffixesFromWord(a,b,0);
};

inline int funCompareSuffixesSkipFirstWord ( const void *a, const void *b){
    return funCompareSuffixesFromWord(a,b,1);
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
        uint64 g=(uint64) G[ii-jj];
        if (g>5) g=5;
        prefix=prefix*6+g;
    };
    return prefix;
};

inline uint64 funSAsortPrefixAtOffset(char* G, uint64 ii, uint64 offset, uint64 prefixLength) {
    uint64 prefix=0;
    for (uint64 jj=0; jj<prefixLength; jj++) {
        uint64 g=(uint64) G[ii-offset-jj];
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

void Genome::genomeGenerate() {

    //check parameters
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
    
    //time
    time_t rawTime;
    string timeString;

    time(&rawTime);
    P.inOut->logMain     << timeMonthDayTime(rawTime) <<" ... starting to generate Genome files\n" <<flush;
    *P.inOut->logStdOut  << timeMonthDayTime(rawTime) <<" ... starting to generate Genome files\n" <<flush;

    //define some parameters from input parameters
    genomeChrBinNbases=1LLU << pGe.gChrBinNbits;

    nGenome = genomeScanFastaFiles(P,NULL,false,*this);//first scan the fasta file to find all the sizes
    genomeSequenceAllocate(nGenome, nG1alloc, G, G1);
    genomeScanFastaFiles(P,G,true,*this);    //load the genome sequence

    uint64 nGenomeTrue=0;
    for (auto &cl : chrLength)
    	nGenomeTrue += cl; //nGenomeTrue = sum of chr lengths

    P.inOut->logMain <<"Genome sequence total length = " << nGenomeTrue << "\n";
    P.inOut->logMain <<"Genome size with padding = "<< nGenome <<"\n";

    //consensusSequence(); //replace with consensus allele DEPRECATED
        
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

    //preparing to generate SA
    for (uint64 ii=0;ii<nGenome;ii++) {//- strand
        //if (G[ii]>5)
        //    cerr << ii <<" "<< G[ii]<<"\n";
        G[2*nGenome-1-ii]=G[ii]<4 ? 3-G[ii] : G[ii];
    };   
    nSA=0;
    for (uint64 ii=0;ii<2*nGenome;ii+=pGe.gSAsparseD) {
        if (G[ii]<4) {
            nSA++;
        };
    };

    // GstrandBit
    GstrandBit = (char) (uint64) floor(log(nGenome+P.limitSjdbInsertNsj*sjdbLength)/log(2))+1; //GstrandBit uses P.limitSjdbInsertNsj even if no insertion requested, in case it will be requested at the mapping stage
    if (GstrandBit<32) GstrandBit=32; //TODO: should not this be 31? Need to test for small genomes. TODO: use simple access function for SA
    P.inOut->logMain <<"Estimated genome size with padding and SJs: total=genome+SJ="<<nGenome+P.limitSjdbInsertNsj*sjdbLength<<" = "<<nGenome<<" + "<<P.limitSjdbInsertNsj*sjdbLength<<"\n";
    P.inOut->logMain << "GstrandBit=" << int(GstrandBit) <<"\n";
    GstrandMask = ~(1LLU<<GstrandBit);
    SA.defineBits(GstrandBit+1,nSA);

    if (P.sjdbInsert.yes) {//reserve space for junction insertion       
        SApass1.defineBits( GstrandBit+1, nSA+2*sjdbLength*min((uint64)sjdbLoci.chr.size(),P.limitSjdbInsertNsj) );//TODO: this allocation is wasteful, get a better estimate of the number of junctions
    } else {//same as SA
        SApass1.defineBits(GstrandBit+1,nSA);
    };

    P.inOut->logMain  << "Number of SA indices: "<< nSA << "\n"<<flush;

    //sort SA
    time ( &rawTime );
    P.inOut->logMain     << timeMonthDayTime(rawTime) <<" ... starting to sort Suffix Array. This may take a long time...\n" <<flush;
    *P.inOut->logStdOut  << timeMonthDayTime(rawTime) <<" ... starting to sort Suffix Array. This may take a long time...\n" <<flush;


//     if (false)
    {//sort SA chunks

        for (uint64 ii=0;ii<nGenome;ii++) {//re-fill the array backwards for sorting
            swap(G[2*nGenome-1-ii],G[ii]);
        };
        globalG=G;
        globalL=pGe.gSuffixLengthMax/sizeof(uint64);
        // Count sort prefixes over STAR's 0..5 genome alphabet.
        uint64 indPrefLen=6;
        const uint64 saAvailableBytesForPrefix=P.limitGenomeGenerateRAM>nG1alloc ? P.limitGenomeGenerateRAM-nG1alloc : 0;
        if (P.runThreadN>=16 && nGenome>=50000000) {
            uint64 indPrefN8=1;
            for (uint64 ii=0;ii<8;ii++) indPrefN8 *= 6;
            const uint64 indPrefTempBytes8=(uint64) P.runThreadN*indPrefN8*2*sizeof(uint64) + (indPrefN8+1)*sizeof(uint64);
            if (indPrefTempBytes8 < saAvailableBytesForPrefix/4) {
                indPrefLen=8;
            };
        };
        uint64 indPrefN=1;
        for (uint64 ii=0;ii<indPrefLen;ii++) indPrefN *= 6;
        uint64* indPrefCount = new uint64 [indPrefN];
        memset(indPrefCount,0,indPrefN*sizeof(indPrefCount[0]));
        const bool saSortProfile=getenv("STAR_PROFILE_SA_SORT")!=NULL;
        nSA=0;
        for (uint64 ii=0;ii<2*nGenome;ii+=pGe.gSAsparseD) {
            if (G[ii]<4) {
                uint64 p1=funSAsortPrefix(G,ii,indPrefLen);
                indPrefCount[p1]++;
                nSA++;
            };
        };

        uint64 indPrefEmptyN=0;
        uint64 indPrefSingletonN=0;
        uint64 indPrefNonEmptyN=0;
        uint64 indPrefSortableN=0;
        uint64 indPrefMaxCount=0;
        uint64 indPrefMaxIndex=0;
        if (saSortProfile) {
            for (uint64 ii=0; ii<indPrefN; ii++) {
                if (indPrefCount[ii]==0) {
                    indPrefEmptyN++;
                } else {
                    indPrefNonEmptyN++;
                    if (indPrefCount[ii]==1) {
                        indPrefSingletonN++;
                    } else {
                        indPrefSortableN++;
                    };
                    if (indPrefCount[ii]>indPrefMaxCount) {
                        indPrefMaxCount=indPrefCount[ii];
                        indPrefMaxIndex=ii;
                    };
                };
            };
        };

        uint64 saChunkSize=(P.limitGenomeGenerateRAM-nG1alloc)/8/P.runThreadN; //number of SA indexes per chunk
        saChunkSize=saChunkSize*6/10; //allow extra space for qsort
        //uint64 saChunkN=((nSA/saChunkSize+1)/P.runThreadN+1)*P.runThreadN;//ensure saChunkN is divisible by P.runThreadN
        //saChunkSize=nSA/saChunkN+100000;//final chunk size
        if (P.runThreadN>1) saChunkSize=min(saChunkSize,nSA/(P.runThreadN-1));
        uint64 saChunkN = nSA / saChunkSize + 1;//estimate
        uint64* indPrefStart = new uint64 [saChunkN*2]; //start and stop, *2 just in case
        uint64* indPrefChunkCount = new uint64 [saChunkN*2];
        indPrefStart[0]=0;
        saChunkN=0;//start counting chunks
        uint64 chunkSize1=indPrefCount[0];
        for (uint64 ii=1; ii<indPrefN; ii++) {
            chunkSize1 += indPrefCount[ii];
            if (chunkSize1 > saChunkSize) {
                saChunkN++;
                indPrefStart[saChunkN]=ii;
                indPrefChunkCount[saChunkN-1]=chunkSize1-indPrefCount[ii];
                chunkSize1=indPrefCount[ii];
            };
        };
        saChunkN++;
        indPrefStart[saChunkN]=indPrefN+1;
        indPrefChunkCount[saChunkN-1]=chunkSize1;

        uint64* saChunkStart = new uint64 [saChunkN+1];
        saChunkStart[0]=0;
        for (uint64 iChunk=0; iChunk<saChunkN; iChunk++) {
            saChunkStart[iChunk+1]=saChunkStart[iChunk]+indPrefChunkCount[iChunk];
        };

        P.inOut->logMain  << "Number of chunks: " << saChunkN <<";   chunks size limit: " << saChunkSize*8 <<" bytes\n" <<flush;

        time ( &rawTime );
        P.inOut->logMain     << timeMonthDayTime(rawTime) <<" ... sorting Suffix Array chunks...\n" <<flush;
        *P.inOut->logStdOut  << timeMonthDayTime(rawTime) <<" ... sorting Suffix Array chunks...\n" <<flush;

        const uint64 genomeScanN=(2*nGenome+pGe.gSAsparseD-1)/pGe.gSAsparseD;
        const uint64 saAvailableBytes=P.limitGenomeGenerateRAM>nG1alloc ? P.limitGenomeGenerateRAM-nG1alloc : 0;
        const uint64 saAllChunkScatterBytes=nSA*sizeof(uint64) + (uint64) P.runThreadN*indPrefN*2*sizeof(uint64) + (indPrefN+1)*sizeof(uint64) + (saChunkN+1)*sizeof(uint64);
        const uint64 saChunkBatchSize=max(saChunkSize*(uint64) P.runThreadN,saChunkSize);
        uint64 saChunkBatchN=0;
        if (P.runThreadN>=16 && saChunkN>1) {
            uint64 iChunkBatch=0;
            while (iChunkBatch<saChunkN) {
                uint64 iChunkBatchNext=iChunkBatch;
                uint64 saBatchN=0;
                while (iChunkBatchNext<saChunkN && (saBatchN==0 || saBatchN+indPrefChunkCount[iChunkBatchNext]<=saChunkBatchSize)) {
                    saBatchN += indPrefChunkCount[iChunkBatchNext];
                    iChunkBatchNext++;
                };
                saChunkBatchN++;
                iChunkBatch=iChunkBatchNext;
            };
        };

        // Batched fill trades one genome scan per chunk for two scans per batch.
        // It pays off once enough chunks are being sorted in parallel.
        const bool saChunkBatchFill=P.runThreadN>=16 && saChunkN>1;
        const uint64 saRetainedChunkBytes=nSA*sizeof(uint64);
        const uint64 saRamPackPeakBytes=saRetainedChunkBytes+SApass1.lengthByte;
        const uint64 saRamPeakBytes=max(saAllChunkScatterBytes,saRamPackPeakBytes);
        const uint64 saRamHeadroomBytes=max(saRamPeakBytes/10,(uint64) 2000000000LLU);
        const uint64 saRamRequiredBytes=saRamPeakBytes+saRamHeadroomBytes;
        const uint64 systemAvailableBytes=systemAvailableMemoryBytes();
        const bool saRamLimitOK=saAvailableBytes>=saRamRequiredBytes;
        const bool saRamSystemOK=systemAvailableBytes>0 && systemAvailableBytes>=saRamRequiredBytes;
        bool saChunksInMemoryActive=saRamLimitOK && saRamSystemOK;
        P.inOut->logMain  << "SA chunk all-scatter available bytes: " << saAvailableBytes << "; required temporary bytes: " << saAllChunkScatterBytes << "\n" <<flush;
        P.inOut->logMain  << "SA chunk estimated batched-fill batches: " << saChunkBatchN << "\n" <<flush;
        P.inOut->logMain  << "SA chunk fill strategy: " << (saChunkBatchFill ? "batched" : "per-chunk") << "\n" <<flush;
        P.inOut->logMain  << "SA chunk sort prefix length: " << indPrefLen << "\n" <<flush;
        P.inOut->logMain  << "SA chunk sort granularity: " << (saChunkBatchFill ? "prefix-bin" : "chunk") << "\n" <<flush;
        P.inOut->logMain  << "SA chunk retained bytes: " << saRetainedChunkBytes << "; RAM peak bytes: " << saRamPeakBytes << "; RAM headroom bytes: " << saRamHeadroomBytes << "\n" <<flush;
        P.inOut->logMain  << "SA chunk system available bytes: " << systemAvailableBytes << "; limit available bytes: " << saAvailableBytes << "\n" <<flush;
        if (saSortProfile) {
            uint64 saChunkMinCount=indPrefChunkCount[0];
            uint64 saChunkMaxCount=indPrefChunkCount[0];
            uint64 saChunkMaxIndex=0;
            for (uint64 iChunk=1; iChunk<saChunkN; iChunk++) {
                if (indPrefChunkCount[iChunk]<saChunkMinCount) {
                    saChunkMinCount=indPrefChunkCount[iChunk];
                };
                if (indPrefChunkCount[iChunk]>saChunkMaxCount) {
                    saChunkMaxCount=indPrefChunkCount[iChunk];
                    saChunkMaxIndex=iChunk;
                };
            };
            P.inOut->logMain << "SA sort profile: enabled\n" <<flush;
            P.inOut->logMain << "SA sort profile prefix bins: total=" << indPrefN
                              << "; empty=" << indPrefEmptyN
                              << "; nonempty=" << indPrefNonEmptyN
                              << "; singleton=" << indPrefSingletonN
                              << "; sortable=" << indPrefSortableN
                              << "; max_bin=" << indPrefMaxCount
                              << "; max_prefix=" << indPrefMaxIndex << "\n" <<flush;
            P.inOut->logMain << "SA sort profile chunks: count=" << saChunkN
                              << "; min_indices=" << saChunkMinCount
                              << "; max_indices=" << saChunkMaxCount
                              << "; max_chunk=" << saChunkMaxIndex
                              << "; mean_indices=" << (saChunkN>0 ? nSA/saChunkN : 0) << "\n" <<flush;
        };

        uint64* saChunksInMemory=NULL;
        if (saChunksInMemoryActive) {
            saChunksInMemory=new (nothrow) uint64 [nSA];
            if (saChunksInMemory==NULL) {
                saChunksInMemoryActive=false;
                P.inOut->logMain << "SA chunk storage fallback: disk-backed after RAM allocation failure\n" <<flush;
            };
        };
        P.inOut->logMain  << "SA chunk storage strategy: " << (saChunksInMemoryActive ? "RAM-retained" : "disk-backed") << "\n" <<flush;

        double saProfileFillCountSeconds=0.0;
        double saProfileFillScatterSeconds=0.0;
        double saProfileSortSeconds=0.0;
        double saProfileFinalizeSeconds=0.0;
        uint64 saProfileSkipFirstWordRangeN=0;
        uint64 saProfileSkipFirstWordIndexN=0;
        uint64 saProfileSubBinPrefixN=0;
        uint64 saProfileSubBinRangeN=0;
        uint64 saProfileSubBinIndexN=0;

        if (saChunkBatchFill) {
            uint64 iChunkBatch=0;
            while (iChunkBatch<saChunkN) {//fill a batch of chunks with two genome scans, then sort and write each chunk
                uint64 iChunkBatchNext=iChunkBatch;
                uint64 saBatchN=0;
                while (iChunkBatchNext<saChunkN && (saBatchN==0 || saBatchN+indPrefChunkCount[iChunkBatchNext]<=saChunkBatchSize)) {
                    saBatchN += indPrefChunkCount[iChunkBatchNext];
                    iChunkBatchNext++;
                };
                uint64 batchChunkN=iChunkBatchNext-iChunkBatch;
                uint64* saBatch=saChunksInMemoryActive ? saChunksInMemory+saChunkStart[iChunkBatch] : new uint64 [saBatchN];
                uint64* saBatchStart=new uint64 [batchChunkN+1];
                saBatchStart[0]=0;
                for (uint64 iChunk=0;iChunk<batchChunkN;iChunk++) {
                    saBatchStart[iChunk+1]=saBatchStart[iChunk]+indPrefChunkCount[iChunkBatch+iChunk];
                };

                uint64 batchPrefStart=indPrefStart[iChunkBatch];
                uint64 batchPrefEnd=min(indPrefStart[iChunkBatchNext],indPrefN);
                uint64 batchPrefN=batchPrefEnd-batchPrefStart;

                uint64* saPrefStart=new uint64 [batchPrefN+1];
                saPrefStart[0]=0;
                for (uint64 iPref=0;iPref<batchPrefN;iPref++) {
                    saPrefStart[iPref+1]=saPrefStart[iPref]+indPrefCount[batchPrefStart+iPref];
                };
                if (saPrefStart[batchPrefN]!=saBatchN) {
                    ostringstream errOut;
                    errOut << "EXITING because of FATAL problem while generating the suffix array\n";
                    errOut << "The number of indices collected for suffix array batch " << iChunkBatch;
                    errOut << " = " << saPrefStart[batchPrefN] << " is not equal to expected " << saBatchN << "\n";
                    errOut << "SOLUTION: try to re-run suffix array generation, if it still does not work, report this problem to the author\n"<<flush;
                    exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
                };

                uint64* saThreadPrefCount=new uint64 [(uint64) P.runThreadN*batchPrefN];
                memset(saThreadPrefCount,0,sizeof(saThreadPrefCount[0])*(uint64) P.runThreadN*batchPrefN);

                double saProfileTime0=omp_get_wtime();
                #pragma omp parallel num_threads(P.runThreadN)
                {
                    int tid=omp_get_thread_num();
                    uint64* threadPrefCount=saThreadPrefCount+(uint64) tid*batchPrefN;
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
                    saProfileFillCountSeconds += omp_get_wtime()-saProfileTime0;
                };

                uint64* saThreadPrefStart=new uint64 [(uint64) P.runThreadN*batchPrefN];
                for (uint64 iPref=0;iPref<batchPrefN;iPref++) {
                    uint64 prefCount=0;
                    for (int iThread=0;iThread<P.runThreadN;iThread++) {
                        saThreadPrefStart[(uint64) iThread*batchPrefN+iPref]=saPrefStart[iPref]+prefCount;
                        prefCount += saThreadPrefCount[(uint64) iThread*batchPrefN+iPref];
                    };
                    if (prefCount!=indPrefCount[batchPrefStart+iPref]) {
                        ostringstream errOut;
                        errOut << "EXITING because of FATAL problem while generating the suffix array\n";
                        errOut << "The number of indices collected for suffix array prefix " << batchPrefStart+iPref << " = " << prefCount;
                        errOut << " is not equal to expected " << indPrefCount[batchPrefStart+iPref] << "\n";
                        errOut << "SOLUTION: try to re-run suffix array generation, if it still does not work, report this problem to the author\n"<<flush;
                        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
                    };
                };

                saProfileTime0=omp_get_wtime();
                #pragma omp parallel num_threads(P.runThreadN)
                {
                    int tid=omp_get_thread_num();
                    uint64* threadPrefStart=saThreadPrefStart+(uint64) tid*batchPrefN;
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
                    saProfileFillScatterSeconds += omp_get_wtime()-saProfileTime0;
                };

                saProfileTime0=omp_get_wtime();
                const bool saCanSkipFirstWord=indPrefLen>=(uint64) sizeof(uint64) && globalL>1;
                const uint64 saSubBinExtraLen=3;
                const uint64 saSubBinN=216; // 6^3
                const uint64 saSubBinMinN=1000000;
                vector<uint64*> saSortRange;
                vector<uint64> saSortRangeN;
                vector<char> saSortRangeSkipFirstWord;
                saSortRange.reserve(batchPrefN);
                saSortRangeN.reserve(batchPrefN);
                saSortRangeSkipFirstWord.reserve(batchPrefN);
                for (uint64 iPref=0; iPref < batchPrefN; iPref++) {
                    uint64* saPref=saBatch+saPrefStart[iPref];
                    uint64 saPrefN=saPrefStart[iPref+1]-saPrefStart[iPref];
                    if (saPrefN>1) {
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

                            uint64* saPrefTmp=NULL;
                            if (saSubBinNonEmptyN>1) {
                                saPrefTmp=new (nothrow) uint64 [saPrefN];
                            };
                            if (saPrefTmp!=NULL) {
                                uint64 saSubBinCursor[saSubBinN];
                                memcpy(saSubBinCursor,saSubBinStart,sizeof(saSubBinCursor));
                                for (uint64 ii=0; ii<saPrefN; ii++) {
                                    uint64 iSub=funSAsortPrefixAtOffset(G,saPref[ii],indPrefLen,saSubBinExtraLen);
                                    saPrefTmp[saSubBinCursor[iSub]]=saPref[ii];
                                    saSubBinCursor[iSub]++;
                                };
                                memcpy(saPref,saPrefTmp,saPrefN*sizeof(saPref[0]));
                                delete [] saPrefTmp;

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
                                if (saSortProfile) {
                                    saProfileSubBinPrefixN++;
                                    saProfileSubBinRangeN += saSubBinSortRangeN;
                                    saProfileSubBinIndexN += saPrefN;
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

                #pragma omp parallel for num_threads(P.runThreadN) schedule(dynamic,16) reduction(+:saProfileSkipFirstWordRangeN,saProfileSkipFirstWordIndexN)
                for (int iRange=0; iRange < (int) saSortRangeN.size(); iRange++) {
                    uint64* saRange=saSortRange[iRange];
                    uint64 saRangeN=saSortRangeN[iRange];
                    if (saSortRangeSkipFirstWord[iRange]) {
                        qsort(saRange,saRangeN,sizeof(saRange[0]),funCompareSuffixesSkipFirstWord);
                        if (saSortProfile) {
                            saProfileSkipFirstWordRangeN++;
                            saProfileSkipFirstWordIndexN += saRangeN;
                        };
                    } else {
                        qsort(saRange,saRangeN,sizeof(saRange[0]),funCompareSuffixes);
                    };
                };
                if (saSortProfile) {
                    saProfileSortSeconds += omp_get_wtime()-saProfileTime0;
                };

                saProfileTime0=omp_get_wtime();
                #pragma omp parallel for num_threads(P.runThreadN) schedule(dynamic,1)
                for (int iChunk=0; iChunk < (int) batchChunkN; iChunk++) {
                    uint64 iChunkAbs=iChunkBatch+(uint64) iChunk;
                    uint64* saChunk=saBatch+saBatchStart[iChunk];
                    for (uint64 ii=0;ii<indPrefChunkCount[iChunkAbs];ii++) {
                        saChunk[ii]=2*nGenome-1-saChunk[ii];
                    };
                    if (!saChunksInMemoryActive) {
                        string chunkFileName=pGe.gDir+"/SA_"+to_string(iChunkAbs);
                        ofstream saChunkFile = ofstrOpenOwned(chunkFileName,ERROR_OUT, P);
                        fstreamWriteBig(saChunkFile, (char*) saChunk, sizeof(saChunk[0])*indPrefChunkCount[iChunkAbs],chunkFileName,ERROR_OUT,P);
                        saChunkFile.close();
                    };
                };
                if (saSortProfile) {
                    saProfileFinalizeSeconds += omp_get_wtime()-saProfileTime0;
                };

                delete [] saThreadPrefStart;
                delete [] saThreadPrefCount;
                delete [] saPrefStart;
                delete [] saBatchStart;
                if (!saChunksInMemoryActive) {
                    delete [] saBatch;
                };
                iChunkBatch=iChunkBatchNext;
            };
        } else {
            #pragma omp parallel for num_threads(P.runThreadN) schedule(dynamic,1) reduction(+:saProfileFillScatterSeconds,saProfileSortSeconds,saProfileFinalizeSeconds)
            for (int iChunk=0; iChunk < (int) saChunkN; iChunk++) {//start the chunk cycle: sort each chunk with qsort and write to a file
                uint64* saChunk=saChunksInMemoryActive ? saChunksInMemory+saChunkStart[iChunk] : new uint64 [indPrefChunkCount[iChunk]];//allocate local array for each chunk
                double saProfileTime0=omp_get_wtime();
                for (uint64 ii=0,jj=0;ii<2*nGenome;ii+=pGe.gSAsparseD) {//fill the chunk with SA indices
                    if (G[ii]<4) {
                        uint64 p1=funSAsortPrefix(G,ii,indPrefLen);
                        if (p1>=indPrefStart[iChunk] && p1<indPrefStart[iChunk+1]) {
                            saChunk[jj]=ii;
                            jj++;
                            if (jj==indPrefChunkCount[iChunk]) break;
                        };
                    };
                };
                if (saSortProfile) {
                    saProfileFillScatterSeconds += omp_get_wtime()-saProfileTime0;
                };

                //sort the chunk
                saProfileTime0=omp_get_wtime();
                if (indPrefChunkCount[iChunk]>1) {
                    qsort(saChunk,indPrefChunkCount[iChunk],sizeof(saChunk[0]),funCompareSuffixes);
                };
                if (saSortProfile) {
                    saProfileSortSeconds += omp_get_wtime()-saProfileTime0;
                };
                saProfileTime0=omp_get_wtime();
                for (uint64 ii=0;ii<indPrefChunkCount[iChunk];ii++) {
                    saChunk[ii]=2*nGenome-1-saChunk[ii];
                };
                //write files
                if (!saChunksInMemoryActive) {
                    string chunkFileName=pGe.gDir+"/SA_"+to_string( (uint64) iChunk);
                    ofstream saChunkFile = ofstrOpenOwned(chunkFileName,ERROR_OUT, P);
                    fstreamWriteBig(saChunkFile, (char*) saChunk, sizeof(saChunk[0])*indPrefChunkCount[iChunk],chunkFileName,ERROR_OUT,P);
                    saChunkFile.close();
                    delete [] saChunk;
                    saChunk=NULL;
                };
                if (saSortProfile) {
                    saProfileFinalizeSeconds += omp_get_wtime()-saProfileTime0;
                };
            };
        };

        if (saSortProfile) {
            ostringstream saProfileOut;
            saProfileOut << fixed << setprecision(3);
            saProfileOut << "SA sort profile phases: fill_count_seconds=" << saProfileFillCountSeconds
                         << "; fill_scatter_seconds=" << saProfileFillScatterSeconds
                         << "; sort_seconds=" << saProfileSortSeconds
                         << "; finalize_seconds=" << saProfileFinalizeSeconds;
            P.inOut->logMain << saProfileOut.str() << "\n" <<flush;
            P.inOut->logMain << "SA sort profile comparator: skip_first_word_ranges=" << saProfileSkipFirstWordRangeN
                              << "; skip_first_word_indices=" << saProfileSkipFirstWordIndexN << "\n" <<flush;
            P.inOut->logMain << "SA sort profile sub-bins: prefixes=" << saProfileSubBinPrefixN
                              << "; ranges=" << saProfileSubBinRangeN
                              << "; indices=" << saProfileSubBinIndexN << "\n" <<flush;
        };

        time ( &rawTime );
        P.inOut->logMain     << timeMonthDayTime(rawTime) << (saChunksInMemoryActive ? " ... packing SA from RAM chunks...\n" : " ... loading chunks from disk, packing SA...\n") <<flush;
        *P.inOut->logStdOut  << timeMonthDayTime(rawTime) << (saChunksInMemoryActive ? " ... packing SA from RAM chunks...\n" : " ... loading chunks from disk, packing SA...\n") <<flush;

        //read chunks and pack into full SA
        SApass1.allocateArray();
        SA.pointArray(SApass1.charArray + SApass1.lengthByte-SA.lengthByte); //SA is shifted to have space for junction insertion
        uint64 N2bit= 1LLU << GstrandBit;
        uint64 packedInd=0;

        #ifdef genenomeGenerate_SA_textOutput
                ofstream SAtxtStream ((pGe.gDir + "/SAtxt").c_str());
        #endif

        if (saChunksInMemoryActive) {//pack directly from RAM-retained chunks (upstream PR #2687)
            for (uint64 ii=0; ii<nSA; ii++) {
                SA.writePacked( ii, (saChunksInMemory[ii]<nGenome) ? saChunksInMemory[ii] : ( (saChunksInMemory[ii]-nGenome) | N2bit ) );

                #ifdef genenomeGenerate_SA_textOutput
                    SAtxtStream << saChunksInMemory[ii] << "\n";
                #endif
            };
            packedInd=nSA;
            delete [] saChunksInMemory;
            saChunksInMemory=NULL;
        } else {//disk-backed fallback: load chunk files one by one and convert to packed
            #define SA_CHUNK_BLOCK_SIZE 10000000
            uint64* saIn=new uint64[SA_CHUNK_BLOCK_SIZE]; //TODO make adjustable

            for (uint64 iChunk=0;iChunk<saChunkN;iChunk++) {//load files one by one and convert to packed
                ostringstream saChunkFileNameStream("");
                saChunkFileNameStream<< pGe.gDir << "/SA_" << iChunk;
                ifstream saChunkFile(saChunkFileNameStream.str().c_str(), std::ios::binary);//binary mode: SA chunks are raw binary, must not be text-translated on Windows
                while (! saChunkFile.eof()) {//read blocks from each file
                    uint64 chunkBytesN=fstreamReadBig(saChunkFile,(char*) saIn,SA_CHUNK_BLOCK_SIZE*sizeof(saIn[0]));
                    for (uint64 ii=0;ii<chunkBytesN/sizeof(saIn[0]);ii++) {
                        SA.writePacked( packedInd+ii, (saIn[ii]<nGenome) ? saIn[ii] : ( (saIn[ii]-nGenome) | N2bit ) );

                        #ifdef genenomeGenerate_SA_textOutput
                            SAtxtStream << saIn[ii] << "\n";
                        #endif
                    };
                    packedInd += chunkBytesN/sizeof(saIn[0]);
                };
                saChunkFile.close();
                remove(saChunkFileNameStream.str().c_str());//remove the chunk file
            };
            delete [] saIn;
        };

        #ifdef genenomeGenerate_SA_textOutput
                SAtxtStream.close();
        #endif

        if (packedInd != nSA ) {//
            ostringstream errOut;
            errOut << "EXITING because of FATAL problem while generating the suffix array\n";
            errOut << "The number of indices read from chunks = "<<packedInd<<" is not equal to expected nSA="<<nSA<<"\n";
            errOut << "SOLUTION: try to re-run suffix array generation, if it still does not work, report this problem to the author\n"<<flush;
            exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
        };

        //DONE with suffix array generation

        for (uint64 ii=0;ii<nGenome;ii++) {//return to normal order for future use
            swap(G[2*nGenome-1-ii],G[ii]);
        };
        delete [] indPrefCount;
        delete [] indPrefStart;
        delete [] indPrefChunkCount;
        delete [] saChunkStart;
    };

    time ( &rawTime );
    P.inOut->logMain     << timeMonthDayTime(rawTime) <<" ... finished generating suffix array\n" <<flush;
    *P.inOut->logStdOut  << timeMonthDayTime(rawTime) <<" ... finished generating suffix array\n" <<flush;

    genomeSAindex(G, SA, P, SAi, *this);

    sjdbN=0;
    if (P.sjdbInsert.yes) {//insert junctions
        P.sjdbInsert.outDir=pGe.gDir;
        P.twoPass.pass2=false;

        Genome genome1(*this, Genome::Snapshot::Borrowed); // insertion-time view; *this owns storage
        sjdbInsertJunctions(P, *this, genome1, sjdbLoci);
    };

    pGe.gFileSizes.clear();
    pGe.gFileSizes.push_back(nGenome);
    pGe.gFileSizes.push_back(SA.lengthByte);

    //write genome parameters file
    genomeParametersWrite(pGe.gDir+("/genomeParameters.txt"), P, ERROR_OUT, *this);

    //write genome to disk
    time ( &rawTime );
    P.inOut->logMain     << timeMonthDayTime(rawTime) <<" ... writing Genome to disk ...\n" <<flush;
    *P.inOut->logStdOut  << timeMonthDayTime(rawTime) <<" ... writing Genome to disk ...\n" <<flush;

    writeGenomeSequence(pGe.gDir);

    //write SA
    time ( &rawTime );
    P.inOut->logMain  << "SA size in bytes: "<<SA.lengthByte << "\n"<<flush;

    P.inOut->logMain     << timeMonthDayTime(rawTime) <<" ... writing Suffix Array to disk ...\n" <<flush;
    *P.inOut->logStdOut  << timeMonthDayTime(rawTime) <<" ... writing Suffix Array to disk ...\n" <<flush;

    ofstream SAout = ofstrOpenOwned(pGe.gDir+"/SA",ERROR_OUT, P);
    fstreamWriteBig(SAout,(char*) SA.charArray, (streamsize) SA.lengthByte,pGe.gDir+"/SA",ERROR_OUT,P);
    SAout.close();

    //write SAi
    time(&rawTime);
    P.inOut->logMain    << timeMonthDayTime(rawTime) <<" ... writing SAindex to disk\n" <<flush;
    *P.inOut->logStdOut << timeMonthDayTime(rawTime) <<" ... writing SAindex to disk\n" <<flush;

    //write SAi to disk
    ofstream SAiOut = ofstrOpenOwned(pGe.gDir+"/SAindex",ERROR_OUT, P);

    fstreamWriteBig(SAiOut, (char*) &pGe.gSAindexNbases, sizeof(pGe.gSAindexNbases),pGe.gDir+"/SAindex",ERROR_OUT,P);
    fstreamWriteBig(SAiOut, (char*) genomeSAindexStart, sizeof(genomeSAindexStart[0])*(pGe.gSAindexNbases+1),pGe.gDir+"/SAindex",ERROR_OUT,P);
    fstreamWriteBig(SAiOut,  SAi.charArray, SAi.lengthByte,pGe.gDir+"/SAindex",ERROR_OUT,P);
    SAiOut.close();

    SApass1.deallocateArray();

    time(&rawTime);
    P.inOut->logMain    << timeMonthDayTime(rawTime) << " ..... finished successfully\n" <<flush;
    *P.inOut->logStdOut << timeMonthDayTime(rawTime) << " ..... finished successfully\n" <<flush;
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
    chrN.close();chrL.close();chrS.close(); chrNL.close();
};
void Genome::writeGenomeSequence(const string dirOut) 
{//write genome sequence
    ofstream genomeOut = ofstrOpenOwned(dirOut+"/Genome",ERROR_OUT, P);
    fstreamWriteBig(genomeOut,G,nGenome,dirOut+"/Genome",ERROR_OUT,P);
    genomeOut.close();
};
