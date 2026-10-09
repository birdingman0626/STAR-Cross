#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "ReadAlign.h"

// Allocation bases are distinct from the mutable views used by the mapper.
// Large arenas remain uninitialized and retain their existing capacities/layout.
struct ReadAlign::Storage {
    std::unique_ptr<uint*[]> splitR;
    std::array<std::unique_ptr<uint[]>,3> split;
    std::unique_ptr<uiPC[]> PC;
    std::unique_ptr<uiWC[]> WC;
    std::unique_ptr<uint[]> nWA, nWAP, WALrec, WlastAnchor;
    std::unique_ptr<uiWA*[]> WA;
    std::vector<std::unique_ptr<uiWA[]>> windows;
    std::unique_ptr<bool[]> WAincl;
    std::unique_ptr<uint[]> swWinCov, scoreSeedBestInd, scoreSeedBestMM, seedChain;
    std::unique_ptr<intScore[]> scoreSeedToSeed, scoreSeedBest;
    std::unique_ptr<Transcript**[]> trAll;
    std::unique_ptr<uint[]> nWinTr;
    std::unique_ptr<Transcript[]> trArray, alignTrAll;
    std::unique_ptr<Transcript*[]> trArrayPointer, outputTranscripts;
    std::vector<std::unique_ptr<Transcript>> transformed;
    std::unique_ptr<Transcript> trInit;
    std::unique_ptr<char*[]> Read0, Qual0, readNameMates, Read1, outBAMoneAlign;
    std::vector<std::unique_ptr<char[]>> reads, quals, names, bam;
    std::array<std::unique_ptr<char[]>,3> normalizedReads;
    std::unique_ptr<uint[]> outBAMoneAlignNbytes;
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
        storage->alignTrAll.reset(new Transcript [P.alignTranscriptsPerReadNmax]);
        alignTrAll=storage->alignTrAll.get();
    };

    if (P.pGe.gType==101) {//SuperTranscriptome
        storage->splGraph.reset(new SpliceGraph(*mapGen.superTr, P, this));
        splGraph=storage->splGraph.get();
    } else {//standard map algorithm:
        winBin[0].resize(P.winBinN, uintWinBinMax);
        winBin[1].resize(P.winBinN, uintWinBinMax);
        //split
        storage->splitR.reset(new uint*[3]);
        splitR=storage->splitR.get();
        for (size_t i=0; i<3; ++i) {
            storage->split[i].reset(new uint[P.maxNsplit]);
            splitR[i]=storage->split[i].get();
        }
        //alignments
        storage->PC.reset(new uiPC[P.seedPerReadNmax]); PC=storage->PC.get();
        storage->WC.reset(new uiWC[P.alignWindowsPerReadNmax]); WC=storage->WC.get();
        storage->nWA.reset(new uint[P.alignWindowsPerReadNmax]); nWA=storage->nWA.get();
        storage->nWAP.reset(new uint[P.alignWindowsPerReadNmax]); nWAP=storage->nWAP.get();
        storage->WALrec.reset(new uint[P.alignWindowsPerReadNmax]); WALrec=storage->WALrec.get();
        storage->WlastAnchor.reset(new uint[P.alignWindowsPerReadNmax]); WlastAnchor=storage->WlastAnchor.get();
    
        storage->WA.reset(new uiWA*[P.alignWindowsPerReadNmax]); WA=storage->WA.get();
        storage->windows.resize(P.alignWindowsPerReadNmax);
        for (uint ii=0;ii<P.alignWindowsPerReadNmax;ii++) {
            storage->windows[ii].reset(new uiWA[P.seedPerWindowNmax]);
            WA[ii]=storage->windows[ii].get();
        }
        storage->WAincl.reset(new bool [P.seedPerWindowNmax]); WAincl=storage->WAincl.get();

        #ifdef COMPILE_FOR_LONG_READS
        storage->swWinCov.reset(new uint[P.alignWindowsPerReadNmax]); swWinCov=storage->swWinCov.get();
        storage->scoreSeedToSeed.reset(new intScore [P.seedPerWindowNmax*(P.seedPerWindowNmax+1)/2]); scoreSeedToSeed=storage->scoreSeedToSeed.get();
        storage->scoreSeedBest.reset(new intScore [P.seedPerWindowNmax]); scoreSeedBest=storage->scoreSeedBest.get();
        storage->scoreSeedBestInd.reset(new uint [P.seedPerWindowNmax]); scoreSeedBestInd=storage->scoreSeedBestInd.get();
        storage->scoreSeedBestMM.reset(new uint [P.seedPerWindowNmax]); scoreSeedBestMM=storage->scoreSeedBestMM.get();
        storage->seedChain.reset(new uint [P.seedPerWindowNmax]); seedChain=storage->seedChain.get();
        #endif
    };

    //aligns a.k.a. transcripts
    storage->trAll.reset(new Transcript**[P.alignWindowsPerReadNmax+1]); trAll=storage->trAll.get();
    storage->nWinTr.reset(new uint[P.alignWindowsPerReadNmax]); nWinTr=storage->nWinTr.get();
    storage->trArray.reset(new Transcript[P.alignTranscriptsPerReadNmax]); trArray=storage->trArray.get();
    storage->trArrayPointer.reset(new Transcript*[P.alignTranscriptsPerReadNmax]); trArrayPointer=storage->trArrayPointer.get();
    for (uint ii=0;ii<P.alignTranscriptsPerReadNmax;ii++)
        trArrayPointer[ii]= &(trArray[ii]);
    storage->trInit.reset(new Transcript); trInit=storage->trInit.get();
    
    if (mapGen.genomeOut.convYes) {//allocate output transcripts
        storage->outputTranscripts.reset(new Transcript*[P.alignFilter.multimapMax]);
        alignsGenOut.alMult=storage->outputTranscripts.get();
        storage->transformed.resize(P.alignFilter.multimapMax);
        for (uint32 ii=0; ii<P.alignFilter.multimapMax; ii++) {
            storage->transformed[ii].reset(new Transcript);
            alignsGenOut.alMult[ii]=storage->transformed[ii].get();
        }
    };
    
    //read
    storage->Read0.reset(new char*[P.readNends]); Read0=storage->Read0.get();
    storage->Qual0.reset(new char*[P.readNends]); Qual0=storage->Qual0.get();
    storage->readNameMates.reset(new char*[P.readNends]); readNameMates=storage->readNameMates.get();
    storage->reads.resize(P.readNends); storage->quals.resize(P.readNends); storage->names.resize(P.readNends);
    for (uint32 ii=0; ii<P.readNends; ii++) {
        storage->names[ii].reset(new char [DEF_readNameLengthMax]); readNameMates[ii]=storage->names[ii].get();
        storage->reads[ii].reset(new char [DEF_readSeqLengthMax+1]); Read0[ii]=storage->reads[ii].get();
        storage->quals[ii].reset(new char [DEF_readSeqLengthMax+1]); Qual0[ii]=storage->quals[ii].get();
    };
    readNameExtra.resize(P.readNends);
    readName = readNameMates[0];

    storage->Read1.reset(new char*[3]); Read1=storage->Read1.get();
    for (size_t i=0; i<3; ++i) {
        storage->normalizedReads[i].reset(new char[DEF_readSeqLengthMax+1]);
        Read1[i]=storage->normalizedReads[i].get();
    }
    
    for (auto &q: qualHist)
        q.fill(0);
    
    //outBAM
    storage->outBAMoneAlignNbytes.reset(new uint [P.readNmates+2]); outBAMoneAlignNbytes=storage->outBAMoneAlignNbytes.get();
    storage->outBAMoneAlign.reset(new char* [P.readNmates+2]); outBAMoneAlign=storage->outBAMoneAlign.get();
    storage->bam.resize(P.readNmates+2);
    for (uint ii=0; ii<P.readNmates+2; ii++) {//not readNends: this is alignment
        storage->bam[ii].reset(new char [BAMoutput_oneAlignMaxBytes]); outBAMoneAlign[ii]=storage->bam[ii].get();
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
    storedLmin=0; uniqLmax=0; uniqLmaxInd=0; multLmax=0; multLmaxN=0; multNminL=0; multNmin=0; multNmax=0; multNmaxL=0;
    chimN=0;

    for (uint ii=0; ii<P.readNmates; ii++) {//not readNends: this is alignment
        maxScoreMate[ii]=0;
    };
};

