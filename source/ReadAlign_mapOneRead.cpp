#include "ReadAlign.h"
#include "SequenceFuns.h"
#include "Stats.h"
#include "serviceFuns.cpp"
#include "SeedDiagnostics.h"
#include "SeedRounds.h"
#include <chrono>
using namespace std;
int ReadAlign::mapOneRead() {
    #ifdef STAR_CAPTURE_SEEDS
    seedTrace::beginRead(iReadAll);
    seedTrace::Timer diagnostic(&seedTrace::Worker::mapNs);
    #endif

    #ifdef OFF_BEFORE_SEEDING
        #warning OFF_BEFORE_SEEDING
        nW=0;
        return 0;
    #endif

    revertStrand = false; //the 2nd read is awlays on opposite strand. 1st and 2nd reads have already been reversed.

    if (preparedSeedSummary) {
        Nsplit=preparedSeedSummary->splits;
        if(!Nsplit) splitR[1][0]=preparedSeedSummary->longestSplit;
    } else if (Lread>0) {
        Nsplit=qualitySplit(Read1[0], Lread, P.maxNsplit, P.seed.splitMin, splitR);
        // splitR[0][fragnum] => good region start position   (from SequenceFuns.cpp)
        // splitR[1][fragnum] => good reagion length
        // splitR[2][fragnum] => fragnum ?
    } else {
        Nsplit=0;
    };

    resetN(); //reset aligns counters to 0

    //reset/initialize a transcript
    trInit->reset();
    trInit->Chr=0;    trInit->Str=0; trInit->roStr=0;    trInit->cStart=0;     trInit->gLength=0; //to generate nice output of 0 for non-mapped reads
    trInit->iRead=iRead;
    trInit->Lread=Lread;
    trInit->nExons=0;
    trInit->readLengthOriginal=readLengthOriginal;
    trInit->readLengthPairOriginal=readLengthPairOriginal;
    trInit->readLength=readLength;
    trInit->readNmates=readNmates; //not readNends: this is alignment
    trInit->readName=readName;

    trBest=trInit;

    if(preparedSeeds) {
        const auto* seeds=std::exchange(preparedSeeds,nullptr);
        if(const auto* summary=std::exchange(preparedSeedSummary,nullptr)) {
            nP=summary->pieces;nA=summary->alignments;nUM[0]=summary->unique;nUM[1]=summary->multiple;
            seedStats.uniqLmax=summary->uniqLmax;seedStats.uniqLmaxInd=summary->uniqLmaxInd;
            seedStats.multLmax=summary->multLmax;seedStats.multLmaxN=summary->multLmaxN;
            seedStats.multNmin=summary->multNmin;seedStats.multNminL=summary->multNminL;
            seedStats.multNmax=summary->multNmax;seedStats.multNmaxL=summary->multNmaxL;
            if(nP!=seeds->size() || nP>P.seed.perReadNmax) throw std::logic_error("invalid consolidated GPU seeds");
            for(size_t i=0;i<seeds->size();++i) {
                const auto& seed=(*seeds)[i];
                PC[i][PC_rStart]=seed.direction==0 ? seed.start : seed.start+1-seed.match.length;
                PC[i][PC_Length]=seed.match.length;PC[i][PC_Dir]=seed.direction;
                PC[i][PC_Nrep]=seed.match.multiplicity;PC[i][PC_SAstart]=seed.match.lower;
                PC[i][PC_SAend]=seed.match.upper;PC[i][PC_iFrag]=seed.fragment;
            }
        } else for(const auto& seed:*seeds) {
            uint64 bounds[2]={seed.match.lower,seed.match.upper};
            storeAligns(seed.direction,seed.start,seed.match.multiplicity,seed.match.length,bounds,seed.fragment);
        }
    } else {
    // Lread==0 (read fully clipped) leaves Nsplit==0 and skips the loop below; Lread-1 would
    // wrap there and overflow the double->uint64 conversion.
    uint64 searchStartLmax=P.seed.searchStartLmax; // 50
    if (Lread>0)
        searchStartLmax=min(searchStartLmax, (uint64) (P.seed.searchStartLmaxOverLread*(Lread-1))); // read length
    // align all good pieces
    for (uint64 ip=0; ip<Nsplit; ip++) {


        // if the good piece is long, then try multiple times to map it
        uint64 Nstart = P.seed.searchStartLmax>0 && searchStartLmax<splitR[1][ip] ? splitR[1][ip]/searchStartLmax+1 : 1;
        uint64 Lstart = splitR[1][ip]/Nstart;  // length of segment to map.
        bool flagDirMap=true;

        for (uint64 iDir=0; iDir<2; iDir++) {//loop over two directions (leftToRight and RightToLeft)

            uint64 Lmapped, L;

            for (uint64 istart=0; istart<Nstart; istart++) {  // each try to map a segment.

//               #ifdef COMPILE_FOR_LONG_READS
//
//               #else
                if (flagDirMap || istart>0) {//check if the 1st piece in reveree direction does not need to be remapped
                    Lmapped=0;  // length of segment mapped so far.

                    // begin mapping starting from segment start position (istart*Lstart)
                    while ( istart*Lstart + Lmapped + P.seed.mapMin < splitR[1][ip] ) {//map until unmapped portion is <=minLmap (default: 5)

                        // Shift is the position in the read to begin mapping from.
                        uint64 Shift = iDir==0 ? ( splitR[0][ip] + istart*Lstart + Lmapped ) : \
                                   ( splitR[0][ip] + splitR[1][ip] - istart*Lstart-1-Lmapped); //choose Shift for forward or reverse

                        uint64 seedLength=splitR[1][ip] - Lmapped - istart*Lstart; // what's left of the read to align.
                        maxMappableLength2strands(Shift, seedLength, iDir, 0, mapGen.nSA-1, L, splitR[2][ip]);//L=max mappable length, unique or multiple
                        if (iDir==0 && istart==0 && Lmapped==0 && Shift+L == splitR[1][ip] ) {//this piece maps full length and does not need to be mapped from the opposite direction
                            flagDirMap=false;
                        };
                        Lmapped+=L;
                    };//while ( istart*Lstart + Lmapped + P.minLmap < splitR[1][ip] )
                };//if (flagDirMap || istart>0)

                if (P.seed.searchLmax>0) {//search fixed length. Not very efficient, need to improve
                    // off by default.
                    uint64 Shift = iDir==0 ? ( splitR[0][ip] + istart*Lstart ) : \
                                   ( splitR[0][ip] + splitR[1][ip] - istart*Lstart-1); //choose Shift for forward or reverse
                    uint64 seedLength = min(P.seed.searchLmax, iDir==0 ? (splitR[0][ip] + splitR[1][ip]-Shift):(Shift+1) );
                    maxMappableLength2strands(Shift, seedLength, iDir, 0, mapGen.nSA-1, L, splitR[2][ip]);//L=max mappable length, unique or multiple
                };


//               #endif
            };//for (uint64 istart=0; istart<Nstart; istart++)
        };
    };

    }
    #ifdef OFF_AFTER_SEEDING
        #warning OFF_AFTER_SEEDING
        return 0;
    #endif

    #ifdef STAR_CAPTURE_SEEDS
    seedTrace::finalSeeds(nP,PC);
    #endif

    if (Lread<P.alignFilter.matchMin) {//read is too short (trimmed too much?)
        mapMarker=MARKER_READ_TOO_SHORT;
        trBest->rLength=0; //min good piece length
        nW=0;
    } else if (Nsplit==0) {//no good pieces
        mapMarker=MARKER_NO_GOOD_PIECES;
        trBest->rLength=splitR[1][0]; //min good piece length
        nW=0;
    } else if (Nsplit>0 && nA==0) {
        mapMarker=MARKER_ALL_PIECES_EXCEED_seedMultimapNmax;
        trBest->rLength=seedStats.multNminL;
        nW=0;
    } else if (Nsplit>0 && nA>0) {//otherwise there are no good pieces, or all pieces map too many times: read cannot be mapped
//         qsort((void*) PC, nP, sizeof(uint64)*PC_SIZE, funCompareUint2);//sort PC by rStart and length
        auto spStart=std::chrono::steady_clock::now();
        stitchPieces(Read1, Lread);
        alignPhaseSeconds[9]+=std::chrono::duration<double>(std::chrono::steady_clock::now()-spStart).count();
    };

    preparedHits=nullptr;
    return 0;
};
