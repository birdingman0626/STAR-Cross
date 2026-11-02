#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "ReadAlign.h"
#include "uninitArray.h"
using namespace std;
// Allocation bases are distinct from the mutable views used by the mapper.
// Large arenas remain uninitialized and retain their existing capacities/layout.
struct ReadAlign::Storage {
    std::unique_ptr<uint64*[]> splitR;
    std::array<std::unique_ptr<uint64[]>,3> split;
    std::unique_ptr<uiPC[]> PC;
    std::unique_ptr<uiWC[]> WC;
    std::unique_ptr<uint64[]> nWA, nWAP, WALrec, WlastAnchor;
    std::unique_ptr<uiWA*[]> WA;
    std::vector<std::unique_ptr<uiWA[]>> windows;
    std::unique_ptr<bool[]> WAincl;
    std::unique_ptr<uint64[]> swWinCov, scoreSeedBestInd, scoreSeedBestMM, seedChain;
    std::unique_ptr<intScore[]> scoreSeedToSeed, scoreSeedBest;
    std::unique_ptr<Transcript**[]> trAll;
    std::unique_ptr<uint64[]> nWinTr;
    std::unique_ptr<Transcript[]> trArray, alignTrAll;
    std::unique_ptr<Transcript*[]> trArrayPointer, outputTranscripts;
    std::vector<std::unique_ptr<Transcript>> transformed;
    std::unique_ptr<Transcript> trInit;
    std::unique_ptr<char*[]> Read0, Qual0, readNameMates, Read1, outBAMoneAlign;
    std::vector<std::unique_ptr<char[]>> reads, quals, names, bam;
    std::array<std::unique_ptr<char[]>,3> normalizedReads;
    std::unique_ptr<uint64[]> outBAMoneAlignNbytes;
    std::unique_ptr<fstream> junction;
    std::unique_ptr<ChimericDetection> chimDet;
    std::unique_ptr<SoloRead> soloRead;
    std::unique_ptr<SpliceGraph> splGraph;
};

ReadAlign::~ReadAlign() = default;

void ReadAlign::borrowChimericJunctionStream(fstream* stream) {
    if (stream == chunkOutChimJunction) return;
    storage->junction.reset();
    chunkOutChimJunction=stream;
    chimDet->ostreamChimJunction=stream;
}

ReadAlign::ReadAlign (Parameters& Pin, Genome &genomeIn, Transcriptome *TrIn, int iChunk)
                    : mapGen(genomeIn), genOut(*genomeIn.genomeOut.g), storage(new Storage), P(Pin), chunkTr(TrIn)
{
    readNmates=P.readNmates; //not readNends
    //RNGs
    rngMultOrder.seed((uint64_t)P.runRNGseed*(iChunk+1));
    rngUniformReal0to1=std::uniform_real_distribution<double> (0.0, 1.0);
    //transcriptome
    if ( P.quant.trSAM.yes ) {
        storage->alignTrAll=makeUninitArray<Transcript>(P.align.transcriptsPerReadNmax);
        alignTrAll=storage->alignTrAll.get();
    };

    if (P.pGe.gType==101) {//SuperTranscriptome
        storage->splGraph.reset(new SpliceGraph(*mapGen.superTr, P, this));
        splGraph=storage->splGraph.get();
    } else {//standard map algorithm:
        winBin[0].resize(P.win.binN, uintWinBinMax);
        winBin[1].resize(P.win.binN, uintWinBinMax);
        //split
        storage->splitR=makeUninitArray<uint64*>(3);
        splitR=storage->splitR.get();
        for (size_t i=0; i<3; ++i) {
            storage->split[i]=makeUninitArray<uint64>(P.maxNsplit);
            splitR[i]=storage->split[i].get();
        }
        //alignments
        storage->PC=makeUninitArray<uiPC>(P.seed.perReadNmax); PC=storage->PC.get();
        storage->WC=makeUninitArray<uiWC>(P.align.windowsPerReadNmax); WC=storage->WC.get();
        storage->nWA=makeUninitArray<uint64>(P.align.windowsPerReadNmax); nWA=storage->nWA.get();
        storage->nWAP=makeUninitArray<uint64>(P.align.windowsPerReadNmax); nWAP=storage->nWAP.get();
        storage->WALrec=makeUninitArray<uint64>(P.align.windowsPerReadNmax); WALrec=storage->WALrec.get();
        storage->WlastAnchor=makeUninitArray<uint64>(P.align.windowsPerReadNmax); WlastAnchor=storage->WlastAnchor.get();
    
        storage->WA=makeUninitArray<uiWA*>(P.align.windowsPerReadNmax); WA=storage->WA.get();
        storage->windows.resize(P.align.windowsPerReadNmax);
        for (uint64 ii=0;ii<P.align.windowsPerReadNmax;ii++) {
            storage->windows[ii]=makeUninitArray<uiWA>(P.seed.perWindowNmax);
            WA[ii]=storage->windows[ii].get();
        }
        storage->WAincl=makeUninitArray<bool>(P.seed.perWindowNmax); WAincl=storage->WAincl.get();

        #ifdef COMPILE_FOR_LONG_READS
        storage->swWinCov=makeUninitArray<uint64>(P.align.windowsPerReadNmax); swWinCov=storage->swWinCov.get();
        storage->scoreSeedToSeed=makeUninitArray<intScore>(P.seed.perWindowNmax*(P.seed.perWindowNmax+1)/2); scoreSeedToSeed=storage->scoreSeedToSeed.get();
        storage->scoreSeedBest=makeUninitArray<intScore>(P.seed.perWindowNmax); scoreSeedBest=storage->scoreSeedBest.get();
        storage->scoreSeedBestInd=makeUninitArray<uint64>(P.seed.perWindowNmax); scoreSeedBestInd=storage->scoreSeedBestInd.get();
        storage->scoreSeedBestMM=makeUninitArray<uint64>(P.seed.perWindowNmax); scoreSeedBestMM=storage->scoreSeedBestMM.get();
        storage->seedChain=makeUninitArray<uint64>(P.seed.perWindowNmax); seedChain=storage->seedChain.get();
        #endif
    };

    //aligns a.k.a. transcripts
    storage->trAll=makeUninitArray<Transcript**>(P.align.windowsPerReadNmax+1); trAll=storage->trAll.get();
    storage->nWinTr=makeUninitArray<uint64>(P.align.windowsPerReadNmax); nWinTr=storage->nWinTr.get();
    storage->trArray=makeUninitArray<Transcript>(P.align.transcriptsPerReadNmax); trArray=storage->trArray.get();
    storage->trArrayPointer=makeUninitArray<Transcript*>(P.align.transcriptsPerReadNmax); trArrayPointer=storage->trArrayPointer.get();
    for (uint64 ii=0;ii<P.align.transcriptsPerReadNmax;ii++)
        trArrayPointer[ii]= &(trArray[ii]);
    storage->trInit.reset(new Transcript); trInit=storage->trInit.get();
    
    if (mapGen.genomeOut.convYes) {//allocate output transcripts
        storage->outputTranscripts=makeUninitArray<Transcript*>(P.alignFilter.multimapMax);
        alignsGenOut.alMult=storage->outputTranscripts.get();
        storage->transformed.resize(P.alignFilter.multimapMax);
        for (uint32 ii=0; ii<P.alignFilter.multimapMax; ii++) {
            storage->transformed[ii].reset(new Transcript);
            alignsGenOut.alMult[ii]=storage->transformed[ii].get();
        }
    };
    
    //read
    storage->Read0=makeUninitArray<char*>(P.readNends); Read0=storage->Read0.get();
    storage->Qual0=makeUninitArray<char*>(P.readNends); Qual0=storage->Qual0.get();
    storage->readNameMates=makeUninitArray<char*>(P.readNends); readNameMates=storage->readNameMates.get();
    storage->reads.resize(P.readNends); storage->quals.resize(P.readNends); storage->names.resize(P.readNends);
    for (uint32 ii=0; ii<P.readNends; ii++) {
        storage->names[ii]=makeUninitArray<char>(DEF_readNameLengthMax); readNameMates[ii]=storage->names[ii].get();
        storage->reads[ii]=makeUninitArray<char>(DEF_readSeqLengthMax+1); Read0[ii]=storage->reads[ii].get();
        storage->quals[ii]=makeUninitArray<char>(DEF_readSeqLengthMax+1); Qual0[ii]=storage->quals[ii].get();
    };
    readNameExtra.resize(P.readNends);
    readName = readNameMates[0];

    storage->Read1=makeUninitArray<char*>(3); Read1=storage->Read1.get();
    for (size_t i=0; i<3; ++i) {
        storage->normalizedReads[i]=makeUninitArray<char>(DEF_readSeqLengthMax+1);
        Read1[i]=storage->normalizedReads[i].get();
    }
    
    for (auto &q: qualHist)
        q.fill(0);
    
    //outBAM
    storage->outBAMoneAlignNbytes=makeUninitArray<uint64>(P.readNmates+2); outBAMoneAlignNbytes=storage->outBAMoneAlignNbytes.get();
    storage->outBAMoneAlign=makeUninitArray<char*>(P.readNmates+2); outBAMoneAlign=storage->outBAMoneAlign.get();
    storage->bam.resize(P.readNmates+2);
    for (uint64 ii=0; ii<P.readNmates+2; ii++) {//not readNends: this is alignment
        storage->bam[ii]=makeUninitArray<char>(BAMoutput_oneAlignMaxBytes); outBAMoneAlign[ii]=storage->bam[ii].get();
    };
    resetN();
    
    //chim
    storage->junction.reset(new fstream); chunkOutChimJunction=storage->junction.get();
    storage->chimDet.reset(new ChimericDetection(P, trAll, nWinTr, Read1, mapGen, chunkOutChimJunction, this));
    chimDet=storage->chimDet.get();
    
    //solo
    storage->soloRead.reset(new SoloRead (P, iChunk)); soloRead=storage->soloRead.get();
    
    //clipping
    P.pClip.initializeClipMates(clipMates);

    //debug
    {
    #ifdef DEBUG_OutputLastRead
        lastReadStream.open((P.outFileTmp+"/lastRead_"+to_string(iChunk)).c_str());
    #endif
    };
};

void ReadAlign::resetN () {//reset resets the counters to 0 for a new read
    mapMarker=0;
    nA=0; nP=0; nW=0;
    nTr=0;
    nUM[0]=0; nUM[1]=0;
    seedStats={};
    chimOld.N=0;

    for (uint64 ii=0; ii<P.readNmates; ii++) {//not readNends: this is alignment
        maxScoreMate[ii]=0;
    };
};

