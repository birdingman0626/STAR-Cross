#include "sjdbBuildIndex.h"
#include "ErrorWarning.h"
#include "SuffixArrayFuns.h"
#include "SequenceFuns.h"
#include "serviceFuns.cpp"
#include "IncludeDefine.h"
#include "streamFuns.h"
#include "binarySearch2.h"
#include <cmath>
#include <memory>
#include <vector>
#include "gpuSjdbRemap.h"

#include "funCompareUintAndSuffixes.h"
#include "uninitArray.h"
using namespace std;
static uint64 sjdbSortBucket(uint64 key, uint64 keyRange, uint64 bucketN)
{
    // Monotonic bucket mapping without a 128-bit intermediate (MSVC has no native
    // __uint128). Only monotonicity in the key is required for the bucketed
    // parallel sort to reproduce the exact total order, so width = ceil(keyRange/
    // bucketN) and bucket = key/width gives an overflow-safe, identical result.
    uint64 bucketWidth=(keyRange + bucketN - 1)/bucketN;
    if (bucketWidth==0) bucketWidth=1;
    uint64 bucket=key/bucketWidth;
    return bucket<bucketN ? (uint64) bucket : bucketN-1;
};

static bool sjdbSortIndicesParallel(Parameters &P, uint64* indArray, uint64 nInd, uint64 keyRange, char* Gsj)
{
    g_funCompareUintAndSuffixes_G=Gsj;

    if (P.runThreadN<4 || nInd<1000000 || keyRange==0) {
        qsort((void*) indArray, nInd, 2*sizeof(uint64), funCompareUintAndSuffixes);
        return false;
    };

    uint64 bucketN=1;
    while (bucketN<(uint64) P.runThreadN*64 && bucketN<16384) {
        bucketN <<= 1;
    };
    if (bucketN>nInd) {
        bucketN=1;
        while ((bucketN<<1)<=nInd) bucketN <<= 1;
    };
    if (bucketN<2) {
        qsort((void*) indArray, nInd, 2*sizeof(uint64), funCompareUintAndSuffixes);
        return false;
    };

    const uint64 indBytes=2*nInd*sizeof(uint64);
    const uint64 auxBytes=indBytes + (uint64) P.runThreadN*bucketN*2*sizeof(uint64) + (bucketN+1)*sizeof(uint64);
    if (P.limit.genomeGenerateRAM>0 && auxBytes>P.limit.genomeGenerateRAM/4) {
        qsort((void*) indArray, nInd, 2*sizeof(uint64), funCompareUintAndSuffixes);
        return false;
    };

    vector<uint64> bucketCount((uint64) P.runThreadN*bucketN,0);
    #pragma omp parallel num_threads(P.runThreadN)
    {
        uint64 tid=(uint64) omp_get_thread_num();
        uint64* threadBucketCount=bucketCount.data()+tid*bucketN;
        #pragma omp for schedule(static)
        for (int64 ii=0; ii<(int64)nInd; ii++) {//signed index: MSVC OpenMP 2.0
            ++threadBucketCount[sjdbSortBucket(indArray[2*ii],keyRange,bucketN)];
        };
    };

    vector<uint64> bucketStart(bucketN+1,0);
    for (uint64 iBucket=0; iBucket<bucketN; iBucket++) {
        uint64 bucketTotal=0;
        for (int iThread=0; iThread<P.runThreadN; iThread++) {
            bucketTotal += bucketCount[(uint64) iThread*bucketN+iBucket];
        };
        bucketStart[iBucket+1]=bucketStart[iBucket]+bucketTotal;
    };

    vector<uint64> bucketThreadStart((uint64) P.runThreadN*bucketN,0);
    for (uint64 iBucket=0; iBucket<bucketN; iBucket++) {
        uint64 offset=bucketStart[iBucket];
        for (int iThread=0; iThread<P.runThreadN; iThread++) {
            bucketThreadStart[(uint64) iThread*bucketN+iBucket]=offset;
            offset += bucketCount[(uint64) iThread*bucketN+iBucket];
        };
    };

    auto indArray1Storage=makeUninitArray<uint64>(2*nInd+2);
    uint64* indArray1=indArray1Storage.get();//borrowed by the OpenMP workers below
    #pragma omp parallel num_threads(P.runThreadN)
    {
        uint64 tid=(uint64) omp_get_thread_num();
        uint64* threadBucketStart=bucketThreadStart.data()+tid*bucketN;
        #pragma omp for schedule(static)
        for (int64 ii=0; ii<(int64)nInd; ii++) {//signed index: MSVC OpenMP 2.0
            uint64 iBucket=sjdbSortBucket(indArray[2*ii],keyRange,bucketN);
            uint64 jj=threadBucketStart[iBucket]++;
            indArray1[2*jj]=indArray[2*ii];
            indArray1[2*jj+1]=indArray[2*ii+1];
        };
    };

    #pragma omp parallel for num_threads(P.runThreadN) schedule(dynamic,1)
    for (int iBucket=0; iBucket<(int) bucketN; iBucket++) {
        qsort((void*) (indArray1+2*bucketStart[(uint64) iBucket]),
              bucketStart[(uint64) iBucket+1]-bucketStart[(uint64) iBucket],
              2*sizeof(uint64), funCompareUintAndSuffixes);
    };

    memcpy(indArray,indArray1,indBytes);
    return true;
};

namespace {

// State shared by the junction-insertion steps. mapGen is the genome being extended,
// mapGen1 the snapshot it had before this insertion.
struct SjdbInsertion {
    Parameters &P;
    Genome &mapGen, &mapGen1;
    char *Gsj;            //junction sequences, forward then reverse-complement, spacer-terminated
    uint64 nGsj;          //bytes of forward junction sequence
    uint64 *indArray;     //pairs (old SA position, offset in Gsj) of suffixes to insert, sorted, -999 sentinel
    uint64 nInd=0;        //number of suffixes to insert
    uint32 *oldSJind;     //new index of each junction already in mapGen1
    uint64 sjNew=0;       //junctions not yet in mapGen1

    uint64 genomeEnd() const {return mapGen.chrStart[mapGen.nChrReal];};
    uint64 packedNewIndex(uint64 ind1, uint64 N2bit) const
    {//Gsj offset -> packed SA value in the extended genome
        return ind1<nGsj ? ind1+genomeEnd() : (ind1-nGsj) | N2bit;
    };
};

void prepareJunctionText(SjdbInsertion &s)
{//spacer-terminate each junction, append the reverse complement
    const uint64 sjdbLength=s.mapGen.sjdbLength;
    for (uint64 ii=1; ii<=s.mapGen.sjdbN; ii++)
    {
        s.Gsj[ii*sjdbLength-1]=GENOME_spacingChar; //to make sure this is > than any genome char
    };
    s.Gsj[s.nGsj*2]=GENOME_spacingChar;//mark the end of the text

    for (uint64 ii=0; ii<s.nGsj; ii++) {//reverse complement junction sequences
        s.Gsj[s.nGsj*2-1-ii]=s.Gsj[ii]<4 ? 3-s.Gsj[ii] : s.Gsj[ii]; //reverse complement
    };
};

void findInsertionPoints(SjdbInsertion &s, const char *G1c, uint64 nIndicesSJ1)
{//SA insertion point for every suffix of every new junction; old junctions only get their new index
    Genome &mapGen=s.mapGen, &mapGen1=s.mapGen1;
    uint64 sjNew=0;
    #pragma omp parallel num_threads(s.P.runThreadN)
    #pragma omp for schedule (dynamic,1000) reduction(+:sjNew)
    for (int64 isj=0; isj<(int64)(2*mapGen.sjdbN); isj++) {//find insertion points for each of the sequences

        char* seq1[2]; // stack array, not heap (fix: memory leak in loop)
        seq1[0]=s.Gsj+isj*mapGen.sjdbLength;
        seq1[1]=(char*) G1c+isj*mapGen.sjdbLength;

        uint64 isj1=isj<mapGen.sjdbN ? isj : 2*mapGen.sjdbN-1-isj;
        int sjdbInd = mapGen1.sjdbN==0 ? -1 : binarySearch2(mapGen.sjdbStart[isj1],mapGen.sjdbEnd[isj1],mapGen1.sjdbStart,mapGen1.sjdbEnd,mapGen1.sjdbN);
        if (sjdbInd<0)
        {//count new junctions
            ++sjNew;
        } else
        {//record new index of the old junctions
            s.oldSJind[sjdbInd]=isj1;
        };

        for (uint64 istart=0; istart<nIndicesSJ1;istart++) {
            uint64 ind1=2*(isj*nIndicesSJ1+istart);
            if (sjdbInd>=0 || seq1[0][istart]>3)
            {//no index for already included junctions, or suffices starting with N
                s.indArray[ind1]=-1;
            } else
            {
                //-1LLU results in suffixes for the new junctions to be always included in SA *after* the suffixes of the old junctions
                //for identical suffixes, this may result in unstable ordering
                s.indArray[ind1] =  suffixArraySearch1(mapGen, seq1, istart, 10000, -1LLU, true, 0, mapGen.nSA-1, 0) ;
                s.indArray[ind1+1] = isj*mapGen.sjdbLength+istart;
            };
        };
    };
    s.sjNew = sjNew/2;//novel junctions were double counted on two strands

    time_t rawtime;
    time ( &rawtime );
    s.P.inOut->logMain  << timeMonthDayTime(rawtime) << "   Finished SA search: number of new junctions=" << s.sjNew <<", old junctions="<<mapGen.sjdbN-s.sjNew<<endl;

    for (uint64 ii=0; ii<2*mapGen.sjdbN*nIndicesSJ1; ii++) {//remove entries that cannot be inserted, this cannot be done in the parallel cycle above
        if (s.indArray[ii*2]!= (uint64) -1) {
            s.indArray[s.nInd*2]=s.indArray[ii*2];
            s.indArray[s.nInd*2+1]=s.indArray[ii*2+1];
            ++s.nInd;
        };
    };
};

bool remapWithoutNewSuffixes(SjdbInsertion &s, PackedArray &SA, PackedArray &SA2)
{//no suffix added and geometry unchanged: GPU remap, or a plain copy when no junction moved
    Parameters &P=s.P;
    Genome &mapGen=s.mapGen, &mapGen1=s.mapGen1;
    bool remapped=false;
    const bool supported=s.nInd==0 && s.sjNew==0 && mapGen.nGenome==mapGen1.nGenome
        && SA.wordLength==SA2.wordLength && SA.lengthByte==SA2.lengthByte;
    if(P.gpuSjdbRemapMode!="off") {
        GpuSjdbRemapResult result{GpuSjdbRemapResult::Unavailable,"new suffixes or changed index geometry unsupported"};
        if(supported) result=gpuSjdbRemap({SA.charArray,SA2.charArray,mapGen1.nSA,SA.lengthByte,
            mapGen1.nGenome,s.genomeEnd(),mapGen.sjdbLength,
            s.oldSJind,mapGen1.sjdbN,unsigned(SA.wordLength)});
        P.inOut->logMain << "GPU_SJDB_REMAP status=" << int(result.status) << " reason=" << result.reason
            << " upload_seconds=" << result.uploadSeconds << " kernel_seconds=" << result.kernelSeconds
            << " download_seconds=" << result.downloadSeconds << " device_bytes=" << result.deviceBytes
            << " chunks=" << result.chunks << endl;
        if(result.status==GpuSjdbRemapResult::Failed ||
           (result.status==GpuSjdbRemapResult::Unavailable && P.gpuSjdbRemapMode=="required"))
            exitWithError("FATAL: experimental GPU SA remap: "+result.reason+"\n",std::cerr,P.inOut->logMain,EXIT_CODE_RUNTIME,P);
        remapped=result.status==GpuSjdbRemapResult::Complete;
    }
    if(!remapped && supported) {
        bool identity=true;
        for(uint64 j=0;j<mapGen1.sjdbN;++j) if(s.oldSJind[j]!=j) {identity=false;break;}
        if(identity) {
            const uint64 bytes=(mapGen1.nSA*SA.wordLength+7)/8;
            // STAR reserves SA buffers in one allocation; destination can overlap
            // the source even when no suffixes are added.
            memmove(SA2.charArray,SA.charArray,bytes);
            memset(SA2.charArray+bytes,0,SA2.lengthByte-bytes);
            if((mapGen1.nSA*SA.wordLength)%8)
                SA2.charArray[bytes-1]&=(1u<<((mapGen1.nSA*SA.wordLength)%8))-1;
            remapped=true;
            P.inOut->logMain << "SJDB_SA_REMAP backend=cpu-identity-copy bytes=" << bytes << endl;
        }
    }
    return remapped;
};

void mergeSuffixArrays(const SjdbInsertion &s, PackedArray &SA, PackedArray &SA2, uint64 nGsjNew)
{//old SA with shifted junction indices, new junction suffixes inserted before their insertion points
    const Genome &mapGen=s.mapGen, &mapGen1=s.mapGen1;
    const uint64 *indArray=s.indArray;
    const uint64 N2bit= 1LLU << mapGen.GstrandBit;
    const uint64 strandMask=~N2bit;
    const uint64 genomeEnd=s.genomeEnd();

    uint64 isj=0, isa2=0;
    for (uint64 isa=0;isa<mapGen1.nSA;isa++) {
        while (isa==indArray[isj*2]) {//insert sj index before the existing index
            SA2.writePacked(isa2,s.packedNewIndex(indArray[isj*2+1],N2bit));
            ++isa2; ++isj;
        };

        uint64 ind1=SA[isa];
        if ( (ind1 & N2bit)>0 )
        {//- strand
            uint64 ind1s = mapGen1.nGenome - (ind1 & strandMask);
            if (ind1s>=genomeEnd)
            {//this index was an old sj, may need to shift it
                uint64 sj1 = (ind1s-genomeEnd)/mapGen.sjdbLength;//old junction index
                ind1s += (s.oldSJind[sj1]-sj1)*mapGen.sjdbLength;
                ind1 = (mapGen.nGenome - ind1s) | N2bit;
            } else
            {
                ind1+=nGsjNew; //reverse complementary indices are all shifted by the length of junctions
            };
        } else
        {//+ strand
            if (ind1>=genomeEnd)
            {//this index was an old sj, may need to shift it
                uint64 sj1 = (ind1-genomeEnd)/mapGen.sjdbLength;//old junction index
                ind1 += (s.oldSJind[sj1]-sj1)*mapGen.sjdbLength;
            };
        };

        SA2.writePacked(isa2,ind1);
        ++isa2;
    };

    for (;isj<s.nInd;isj++) {//insert last new indices after the last old index
        SA2.writePacked(isa2,s.packedNewIndex(indArray[isj*2+1],N2bit));
        ++isa2;
    };
};

void insertSAindex(const SjdbInsertion &s, PackedArray &SAi)
{//shift SAi entries by the junction suffixes inserted before them; fill prefixes the junctions introduce
    const Genome &mapGen=s.mapGen;
    const uint64 *indArray=s.indArray;
    const uint64 nInd=s.nInd;
    const char *Gsj=s.Gsj;
    for (uint64 iL=0; iL < mapGen.pGe.gSAindexNbases; iL++) {
        uint64 iSJ=0;
        uint64 ind0=mapGen.genomeSAindexStart[iL]-1;//last index that was present in the old genome
        for (uint64 ii=mapGen.genomeSAindexStart[iL];ii<mapGen.genomeSAindexStart[iL+1]; ii++) {//scan through the longest index
            uint64 iSA1=SAi[ii];
            uint64 iSA2=iSA1 & mapGen.SAiMarkNmask & mapGen.SAiMarkAbsentMask;

            if ( iSJ<nInd && (iSA1 &  mapGen.SAiMarkAbsentMaskC)>0 )
            {//index missing from the old genome
                uint64 iSJ1=iSJ;
                int64 ind1=funCalcSAi((char*) Gsj+indArray[2*iSJ+1],iL);
                while (ind1 < (int64)(ii-mapGen.genomeSAindexStart[iL]) && indArray[2*iSJ]-1<iSA2) {
                    ++iSJ;
                    if (iSJ>=nInd) break;//reached the sentinel: indArray[2*nInd+1]==(uint64)-999, so funCalcSAi(Gsj-999,...) would read out of bounds before Gsj and can segfault (upstream issue #2657). Stopping here yields the intended "no matching junction" -> else branch.
                    ind1=funCalcSAi((char*) Gsj+indArray[2*iSJ+1],iL);
                };
                if (ind1 == (int64)(ii-mapGen.genomeSAindexStart[iL]) ) {
                    SAi.writePacked(ii,indArray[2*iSJ]-1+iSJ+1);
                    for (uint64 ii0=ind0+1; ii0<ii; ii0++) {//fill all the absent indices with this value
                        SAi.writePacked(ii0,(indArray[2*iSJ]-1+iSJ+1) | mapGen.SAiMarkAbsentMaskC);
                    };
                    ++iSJ;
                    ind0=ii;
                } else {
                    iSJ=iSJ1;
                };
            } else
            {//index was present in the old genome
                while (iSJ<nInd && indArray[2*iSJ]-1+1<iSA2) {//for this index insert "smaller" junctions
                    ++iSJ;
                };

                while (iSJ<nInd && indArray[2*iSJ]-1+1==iSA2) {//special case, the index falls right behind SAi
                    if (funCalcSAi((char*) Gsj+indArray[2*iSJ+1],iL) >= (int64) (ii-mapGen.genomeSAindexStart[iL]) ) {//this belongs to the next index
                        break;
                    };
                    ++iSJ;
                };

                SAi.writePacked(ii,iSA1+iSJ);

                for (uint64 ii0=ind0+1; ii0<ii; ii0++) {//fill all the absent indices with this value
                    SAi.writePacked(ii0,(iSA2+iSJ) | mapGen.SAiMarkAbsentMaskC);
                };
                ind0=ii;
            };
        };
    };
};

uint64 markSAindexN(const SjdbInsertion &s, PackedArray &SAi)
{//set the N mark on SAi prefixes cut short by an N/spacer inside an inserted junction suffix
    const Genome &mapGen=s.mapGen;
    const uint64 nGsj=s.nGsj;
    const char *Gsj=s.Gsj;
    // Cache the first N/spacer position for each junction suffix. Most suffixes
    // have no N in the SAindex prefix, so the SAi marking pass can skip them.
    vector<uint8> sjdbFirstN(2*nGsj+1,mapGen.pGe.gSAindexNbases);
    uint64 nextN=2*nGsj;
    for (int64 ii=2*(int64)nGsj; ii>=0; ii--) {
        if (Gsj[ii]>3) {
            nextN=(uint64) ii;
        };
        uint64 nDist=nextN-(uint64) ii;
        if (nDist<mapGen.pGe.gSAindexNbases) {
            sjdbFirstN[ii]=(uint8) nDist;
        };
    };

    uint64 sjdbSAiNmarkN=0;
    for (uint64 isj=0;isj<s.nInd;isj++) {
        uint64 isjG=s.indArray[2*isj+1];
        uint64 iL=sjdbFirstN[isjG];
        if (iL>=mapGen.pGe.gSAindexNbases) {
            continue;
        };
        sjdbSAiNmarkN++;

        int64 ind1=0;
        for (uint64 iL0=0; iL0<iL; iL0++) {
            ind1 <<= 2;
            ind1 += (uint64) Gsj[isjG+iL0];
        };

        ind1 <<= 2;
        for (uint64 iL1=iL; iL1 < mapGen.pGe.gSAindexNbases; iL1++) {
            ind1+=3;
            int64 ind2=mapGen.genomeSAindexStart[iL1]+ind1;
            for (; ind2>=0; ind2--) {//find previous index that is not absent
                if ( (SAi[ind2] & mapGen.SAiMarkAbsentMaskC)==0 ) {
                    break;
                };
            };
            SAi.writePacked(ind2,SAi[ind2] | mapGen.SAiMarkNmaskC);
            ind1 <<= 2;
        };
    };
    return sjdbSAiNmarkN;
};

} // namespace

void sjdbBuildIndex (Parameters &P, char *Gsj, char *G, PackedArray &SA, PackedArray &SA2, PackedArray &SAi, Genome &mapGen, Genome &mapGen1) {

    if (mapGen.sjdbN==0)
    {//no junctions to insert
        return;
    };

    time_t rawtime;
    time ( &rawtime );
    P.inOut->logMain   << timeMonthDayTime(rawtime) << " ..... inserting junctions into the genome indices" <<endl;
    *P.inOut->logStdOut  << timeMonthDayTime(rawtime) << " ..... inserting junctions into the genome indices" <<endl;

    const uint64 nGsj=mapGen.sjdbLength*mapGen.sjdbN;
    const uint64 nIndicesSJ1=mapGen.sjdbLength;//keep all indices - this is pre-2.4.1 of generating the genome

    auto oldSJind=makeUninitArray<uint32>(mapGen1.sjdbN);
    auto indArray=makeUninitArray<uint64>(2*mapGen.sjdbN*(nIndicesSJ1+1)*2);//SA index and index in the genome * nJunction * nIndices per junction * 2 for reverse compl
    SjdbInsertion s{P, mapGen, mapGen1, Gsj, nGsj, indArray.get(), 0, oldSJind.get(), 0};

    prepareJunctionText(s);
    {
        auto G1c=makeUninitArray<char>(nGsj*2+1);
        complementSeqNumbers(Gsj, G1c.get(), nGsj*2+1);
        findInsertionPoints(s, G1c.get(), nIndicesSJ1);
    };

    bool parallelSort=sjdbSortIndicesParallel(P, s.indArray, s.nInd, mapGen1.nSA+1, Gsj);
    time ( &rawtime );
    P.inOut->logMain  << timeMonthDayTime(rawtime) << "   Finished sorting SA indicesL nInd="<<s.nInd
                       << " strategy=" << (parallelSort ? "parallel-bucket" : "qsort") <<endl;

    s.indArray[2*s.nInd]=-999; //mark the last junction
    s.indArray[2*s.nInd+1]=-999; //mark the last junction

    mapGen.nGenome=s.genomeEnd()+nGsj;
    mapGen.nSA+=s.nInd;

    char GstrandBit1 = (char) (uint64) floor(log(mapGen.nGenome)/log(2))+1;
    if (GstrandBit1<32) GstrandBit1=32; //TODO: use simple access function for SA

    P.inOut->logMain <<"Genome size with junctions="<<mapGen.nGenome<<"  "<<s.genomeEnd()<<"   "<<nGsj<<"\n";
    P.inOut->logMain <<"GstrandBit1="<<int(GstrandBit1)<<"   GstrandBit="<<int(mapGen.GstrandBit)<<"\n";

    if ( GstrandBit1 > mapGen.GstrandBit)
    {//too many junctions were added - GstrandBit changed
        ostringstream errOut;
        errOut << "EXITING because of FATAL ERROR: cannot insert junctions on the fly because of strand GstrandBit problem\n";
        errOut << "SOLUTION: please contact STAR author at https://groups.google.com/forum/#!forum/rna-star\n";
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_GENOME_FILES, P);
    };

    SA2.defineBits(mapGen.GstrandBit+1,mapGen.nSA);
    const uint64 nGsjNew=s.sjNew*mapGen.sjdbLength; //this is the actual number of bytes added to the genome, while nGsj is the total size of all junctions

    if (!remapWithoutNewSuffixes(s, SA, SA2))
        mergeSuffixArrays(s, SA, SA2, nGsjNew);
    time ( &rawtime );
    P.inOut->logMain  << timeMonthDayTime(rawtime) << "   Finished inserting junction indices" <<endl;

    insertSAindex(s, SAi);
    const uint64 sjdbSAiNmarkN=markSAindexN(s, SAi);
    time ( &rawtime );
    P.inOut->logMain  << timeMonthDayTime(rawtime) << "   Finished SAi N marking: candidates=" << sjdbSAiNmarkN
                       << " strategy=first-N-cache" <<endl;
    P.inOut->logMain  << timeMonthDayTime(rawtime) << "   Finished SAi" <<endl;

    //change parameters, most parameters are already re-defined in sjdbPrepare.cpp
    SA.defineBits(mapGen.GstrandBit+1,mapGen.nSA);//same as SA2
    SA.pointArray(SA2.charArray);
    mapGen.nSAbyte=SA.lengthByte;
    mapGen.sjGstart=s.genomeEnd();
    memcpy(G+s.genomeEnd(),Gsj, nGsj);
};
