#include "genomeSAindex.h"
#include "TimeFunctions.h"
#include "SuffixArrayFuns.h"
#include "ErrorWarning.h"
using namespace std;
struct SAindexEvent {
    uint64 isa;
    uint64 indFull;
    int iL4;
};

static bool SAindexEventEqual(uint64 indFull1, int iL41, uint64 indFull2, int iL42)
{
    return indFull1==indFull2 && iL41==iL42;
};

static void SAindexProcessEvent(PackedArray &SAi, Genome &mapGen, uint64 isa, uint64 indFull, int iL4, uint64* ind0)
{
    for (uint64 iL=0; iL < mapGen.pGe.gSAindexNbases; iL++) {//calculate index

        uint64 indPref = indFull >> (2*(mapGen.pGe.gSAindexNbases-1-iL));

        if ( (int)iL==iL4 ) {//this suffix contains N and does not belong in SAi
            for (uint64 iL1=iL; iL1 < mapGen.pGe.gSAindexNbases; iL1++) {
                SAi.writePacked(mapGen.genomeSAindexStart[iL1]+ind0[iL1],SAi[mapGen.genomeSAindexStart[iL1]+ind0[iL1]] | mapGen.SAiMarkNmaskC);
            };
            break;//break the iL cycle
        };

        if ( indPref > ind0[iL] || isa==0 ) {//new && good index, record it

            SAi.writePacked(mapGen.genomeSAindexStart[iL]+indPref, isa);

            for (uint64 ii=ind0[iL]+1; ii<indPref; ii++) {//index is not present, record to the last present suffix
                SAi.writePacked(mapGen.genomeSAindexStart[iL]+ii, isa | mapGen.SAiMarkAbsentMaskC);
            };
            ind0[iL]=indPref;

        } else if ( indPref < ind0[iL] ) {
            ostringstream errOut;
            errOut << "BUG: next index is smaller than previous, EXITING\n" <<flush;
            exitWithError(errOut.str(),std::cerr, mapGen.P.inOut->logMain, EXIT_CODE_INPUT_FILES, mapGen.P);
        };
    };
};

static void SAindexScanEventsParallel(char *G, PackedArray &SA, Parameters &P, Genome &mapGen, uint64 iSA1, uint64 iSA2, vector<SAindexEvent> &events)
{
    const uint64 nSA=iSA2-iSA1+1;
    const uint64 chunkN=min<uint64>(nSA, max<uint64>(1, P.runThreadN*4));
    const int threadN=(int) min<uint64>(P.runThreadN, chunkN);
    vector<vector<SAindexEvent> > eventsByChunk(chunkN);

    #pragma omp parallel for num_threads(threadN) schedule(static)
    for (int64 iChunk=0; iChunk<(int64)chunkN; iChunk++) {//signed index: MSVC OpenMP 2.0
        const uint64 iStart=iSA1 + nSA*(uint64)iChunk/chunkN;
        const uint64 iEnd=iSA1 + nSA*((uint64)iChunk+1)/chunkN;

        vector<SAindexEvent> &chunkEvents=eventsByChunk[iChunk];
        chunkEvents.reserve((iEnd-iStart)/32+1);

        int iL4prev=-1;
        uint64 indFullPrev=0;
        if (iStart>iSA1) {
            indFullPrev=funCalcSAiFromSA(G,SA,mapGen,iStart-1,mapGen.pGe.gSAindexNbases,iL4prev);
        };

        for (uint64 isa=iStart; isa<iEnd; isa++) {
            int iL4=0;
            uint64 indFull=funCalcSAiFromSA(G,SA,mapGen,isa,mapGen.pGe.gSAindexNbases,iL4);
            if (isa==iSA1 || !SAindexEventEqual(indFull, iL4, indFullPrev, iL4prev)) {
                SAindexEvent event;
                event.isa=isa;
                event.indFull=indFull;
                event.iL4=iL4;
                chunkEvents.push_back(event);
            };
            indFullPrev=indFull;
            iL4prev=iL4;
        };
    };

    uint64 eventN=0;
    for (uint64 iChunk=0; iChunk<chunkN; iChunk++) {
        eventN += eventsByChunk[iChunk].size();
    };
    events.reserve(eventN);
    for (uint64 iChunk=0; iChunk<chunkN; iChunk++) {
        events.insert(events.end(), eventsByChunk[iChunk].begin(), eventsByChunk[iChunk].end());
    };
};

void genomeSAindex(char * G, PackedArray & SA, Parameters & P, PackedArray & SAi, Genome &mapGen)
 {
    mapGen.allocateSAindexStarts(mapGen.pGe.gSAindexNbases+1);
    mapGen.genomeSAindexStart[0]=0;
    for (uint64 ii=1;ii<=mapGen.pGe.gSAindexNbases;ii++) {//L-mer indices starts
        mapGen.genomeSAindexStart[ii] = mapGen.genomeSAindexStart[ii-1] + ( 1LLU<<(2*ii) );
    };
    mapGen.nSAi = mapGen.genomeSAindexStart[mapGen.pGe.gSAindexNbases];

    /* testing
    //     uint64* SAi1=new uint64[mapGen.nSAi];

    PackedArray SAio;
    SAio.defineBits(mapGen.GstrandBit+3,mapGen.nSAi);
    SAio.allocateArray();
    ifstream oldSAiin("./DirTrue/SAindex");
    oldSAiin.read(SAio.charArray,8*(mapGen.pGe.gSAindexNbases+2));//skip first bytes
    oldSAiin.read(SAio.charArray,SAio.lengthByte);
    oldSAiin.close();
    */


    mapGen.SAiMarkNbit=mapGen.GstrandBit+1;
    mapGen.SAiMarkAbsentBit=mapGen.GstrandBit+2;

    mapGen.SAiMarkNmaskC=1LLU << mapGen.SAiMarkNbit;
    mapGen.SAiMarkNmask=~mapGen.SAiMarkNmaskC;
    mapGen.SAiMarkAbsentMaskC=1LLU << mapGen.SAiMarkAbsentBit;
    mapGen.SAiMarkAbsentMask=~mapGen.SAiMarkAbsentMaskC;


    SAi.defineBits(mapGen.GstrandBit+3,mapGen.nSAi);
    SAi.allocateArray();

    time_t rawTime;
    time(&rawTime);
    P.inOut->logMain    << timeMonthDayTime(rawTime) <<" ... generating Suffix Array index\n" <<flush;
    *P.inOut->logStdOut << timeMonthDayTime(rawTime) <<" ... generating Suffix Array index\n" <<flush;

    /*testing
    PackedArray SA1=SA;
    uint64* ind0=new uint64[mapGen.pGe.gSAindexNbases];

    for (uint64 ii=0; ii<mapGen.pGe.gSAindexNbases; ii++) {
        ind0[ii]=-1;//this is needed in case "AAA...AAA",i.e. indPref=0 is not present in the genome for some lengths
    };
    uint64* SAi1=new uint64[mapGen.nSAi];

    for (uint64 isa=0; isa<mapGen.nSA; isa++) {//for all suffixes
        if (isa%100000000==0) P.inOut->logMain  << isa*100/mapGen.nSA << "% " << flush;

        uint64 SAstr=SA1[isa];
        bool dirG = (SAstr>>mapGen.GstrandBit) == 0; //forward or reverse strand of the genome
        SAstr &= mapGen.GstrandMask;
        if (!dirG) SAstr=mapGen.nGenome-1-SAstr;

        uint64 indPref=0;
        for (uint64 iL=0; iL < mapGen.pGe.gSAindexNbases; iL++) {//calculate index

            indPref <<= 2;

            uint64 g1= (uint64) G[dirG ? SAstr+iL : SAstr-iL]; //reverese if (-) strand

            if (g1>3) {//if N, this suffix does not belong in SAi
                for (uint64 iL1=iL; iL1 < mapGen.pGe.gSAindexNbases; iL1++) {
                    SAi1[mapGen.genomeSAindexStart[iL1]+ind0[iL1]] |= mapGen.SAiMarkNmaskC;
                };
                break;
            };

            if (!dirG) g1=3-g1; //complement if (-) strand

            indPref += (uint64) g1;

            if ( indPref > ind0[iL] || isa==0 ) {//new && good index, record it
                SAi1[mapGen.genomeSAindexStart[iL]+indPref]=isa;
                for (uint64 ii=ind0[iL]+1; ii<indPref; ii++) {//index is not present, record to the last present suffix
                    SAi1[mapGen.genomeSAindexStart[iL]+ii] = isa | mapGen.SAiMarkAbsentMaskC;
                };
                ind0[iL]=indPref;
            } else if ( indPref < ind0[iL] ) {
                ostringstream errOut;
                errOut << "BUG: next index is smaller than previous, EXITING\n" <<flush;
                exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
            };
        };
    };//for (uint64 isa=0; isa<mapGen.nSA; isa++)
    */

    genomeSAindexChunk(G, SA, P, SAi, 0, SA.length-1, mapGen);

    time(&rawTime);
    P.inOut->logMain    << timeMonthDayTime(rawTime) <<" ... completed Suffix Array index\n" <<flush;
    *P.inOut->logStdOut << timeMonthDayTime(rawTime) <<" ... completed Suffix Array index\n" <<flush;

//     for (uint64 ii=1;ii<=mapGen.pGe.gSAindexNbases-1;ii++) {//L-mer indices starts
//         cout <<ii<<endl;
//         for (uint64 jj=mapGen.genomeSAindexStart[ii-1]; jj<mapGen.genomeSAindexStart[ii]; jj++)
//         {
//             if (SAi[jj]!=SAio[jj])
//             {
//                 cout <<ii <<" "<< jj<<" "<<jj-mapGen.genomeSAindexStart[ii-1]<<" "<<SAi[jj]<<" "<<SAio[jj]<<" "<<endl;
//                 sleep(100);
//             };
//         };
//     };


 };


void genomeSAindexChunk(char * G, PackedArray & SA, Parameters & P, PackedArray & SAi, uint64 iSA1, uint64 iSA2, Genome &mapGen)
{
    const bool parallelEvents=P.runThreadN>=16 && iSA1==0 && iSA2+1==mapGen.nSA;

    P.inOut->logMain << "SAindex traversal strategy: " << (parallelEvents ? "parallel-events" : "skip-search") << "\n" << flush;

    uint64* ind0=new uint64[mapGen.pGe.gSAindexNbases];

    for (uint64 ii=0; ii<mapGen.pGe.gSAindexNbases; ii++) {
        ind0[ii]=-1;//this is needed in case "AAA...AAA",i.e. indPref=0 is not present in the genome for some lengths
    };

    if (parallelEvents) {
        vector<SAindexEvent> events;
        SAindexScanEventsParallel(G, SA, P, mapGen, iSA1, iSA2, events);

        P.inOut->logMain << "SAindex event count: " << events.size() << "\n" << flush;
        for (uint64 iEvent=0; iEvent<events.size(); iEvent++) {
            SAindexProcessEvent(SAi, mapGen, events[iEvent].isa, events[iEvent].indFull, events[iEvent].iL4, ind0);
        };
    } else {
        uint64 isaStep=mapGen.nSA/(1llu<<(2*mapGen.pGe.gSAindexNbases))+1;
//     isaStep=8;

        uint64 isa=iSA1;
        int iL4;
        uint64 indFull=funCalcSAiFromSA(G,SA,mapGen,isa,mapGen.pGe.gSAindexNbases,iL4);
        while (isa<=iSA2) {//for all suffixes
            SAindexProcessEvent(SAi, mapGen, isa, indFull, iL4, ind0);

            //find next index not equal to the current one
            funSAiFindNextIndex(G, SA, isaStep, isa, indFull, iL4, mapGen);//indFull and iL4 have been already defined at the previous step

        };//isa cycle
    };

    for (uint64 iL=0; iL < mapGen.pGe.gSAindexNbases; iL++) {//fill up unfilled indexes
    	for (uint64 ii=mapGen.genomeSAindexStart[iL]+ind0[iL]+1; ii<mapGen.genomeSAindexStart[iL+1]; ii++) {
    		SAi.writePacked(ii, mapGen.nSA | mapGen.SAiMarkAbsentMaskC);
    	};
    };

    delete [] ind0;

 };

void funSAiFindNextIndex(char * G, PackedArray & SA, uint64 isaStep, uint64 & isa, uint64 & indFull, int & iL4, Genome &mapGen)
 {
    uint64 indFullPrev=indFull;
    int iL4prev=iL4;
    isa+=isaStep;
    while (isa<mapGen.nSA && (indFull=funCalcSAiFromSA(G,SA,mapGen,isa,mapGen.pGe.gSAindexNbases,iL4))==indFullPrev && iL4==iL4prev)
    {//make large step in isa while the indFull/iL4 are still the same
        isa+=isaStep;
    };
    if (isa>=mapGen.nSA)
    {//reached the end of the SA
        indFull=funCalcSAiFromSA(G,SA,mapGen,mapGen.nSA-1,mapGen.pGe.gSAindexNbases,iL4);
        if (indFull==indFullPrev && iL4==iL4prev)
        {
            isa=mapGen.nSA;//no more indices, the last one is equal to the previous
            return;
        };
    };

    {//binary search
        uint64 i1=isa-isaStep;
        uint64 i2=min(isa,mapGen.nSA-1);
        while (i1+1<i2)
        {
            isa=i1/2 + i2/2 + (i1%2 + i2%2)/2;
            if ((indFull=funCalcSAiFromSA(G,SA,mapGen,isa,mapGen.pGe.gSAindexNbases,iL4))==indFullPrev && iL4==iL4prev)
            {
                i1=isa;
            } else
            {
                i2=isa;
            };
        };
        if (isa==i1)
        {
            isa=i2;
            indFull=funCalcSAiFromSA(G,SA,mapGen,isa,mapGen.pGe.gSAindexNbases,iL4);
        };
    };
};
