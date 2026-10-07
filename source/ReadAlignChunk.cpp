#include "ReadAlignChunk.h"
#ifdef _WIN32
    #include "wincompat.h"
#else
    #include <pthread.h>
#endif
#include "ErrorWarning.h"

ReadAlignChunk::ReadAlignChunk(Parameters& Pin, Genome &genomeIn, Transcriptome *TrIn, int iChunk) : P(Pin), mapGen(genomeIn) {//initialize chunk

    iThread=iChunk;

    if ( P.quant.yes ) {//allocate transcriptome structures
        transcriptomeStorage.reset(new Transcriptome(*TrIn));
        chunkTr=transcriptomeStorage.get();
        chunkTr->quantsAllocate();
        if (P.quant.geCount.yes) quantificationStorage.reset(chunkTr->quants);
    } else {
        chunkTr=NULL;
    };

    alignStorage.reset(new ReadAlign(P, mapGen, chunkTr, iChunk));
    RA=alignStorage.get();

    RA->iRead=0;

    inputPointers.reset(new char* [P.readNends]); chunkIn=inputPointers.get();
    inputStreamPointers.reset(new FixedIStream* [P.readNends]); readInStream=inputStreamPointers.get();
    inputStorage.resize(P.readNends); inputStreams.resize(P.readNends);

    for (uint ii=0;ii<P.readNends;ii++) {
       inputStorage[ii].reset(new char[P.chunkInSizeBytesArray]); chunkIn[ii]=inputStorage[ii].get();
       memset(chunkIn[ii],'\n',P.chunkInSizeBytesArray);
       inputStreams[ii].reset(new FixedIStream); readInStream[ii]=inputStreams[ii].get();
       readInStream[ii]->setBuffer(chunkIn[ii],P.chunkInSizeBytesArray);
       RA->readInStream[ii]=readInStream[ii];
    };


    if (P.outSAMbool) {
        outputStorage.reset(new char [P.chunkOutBAMsizeBytes]); chunkOutBAM=outputStorage.get();
        RA->outBAMarray=chunkOutBAM;
        outputStreamStorage.reset(new FixedOStream); chunkOutBAMstream=outputStreamStorage.get();
        chunkOutBAMstream->setBuffer(chunkOutBAM,P.chunkOutBAMsizeBytes);
        RA->outSAMstream=chunkOutBAMstream;
        RA->outSAMstream->seekp(0,ios::beg);
        chunkOutBAMtotal=0;
    };

    if (P.outBAMunsorted) {
        chunkOutBAMunsorted.reset(new BAMoutput (P.inOut->outBAMfileUnsorted, P));
        RA->outBAMunsorted = chunkOutBAMunsorted.get();
    } else {
        RA->outBAMunsorted=NULL;
    };

    if (P.outBAMcoord) {
        chunkOutBAMcoord.reset(new BAMoutput (iChunk, P.outBAMsortTmpDir, P));
        RA->outBAMcoord = chunkOutBAMcoord.get();
    } else {
        RA->outBAMcoord=NULL;
    };

    if ( P.quant.trSAM.bamYes ) {
        chunkOutBAMquant.reset(new BAMoutput (P.inOut->outQuantBAMfile,P));
        RA->outBAMquant = chunkOutBAMquant.get();
    } else {
        RA->outBAMquant=NULL;
    };

    if (P.outSJ.yes) {
        junctionStorage.reset(new OutSJ (P.limitOutSJcollapsed, P, mapGen)); chunkOutSJ=junctionStorage.get();
        RA->chunkOutSJ  = chunkOutSJ;
    } else {
        RA->chunkOutSJ  = NULL;
    };

    if (P.outFilterBySJoutStage == 1) {
        filteredJunctionStorage.reset(new OutSJ (P.limitOutSJcollapsed, P, mapGen)); chunkOutSJ1=filteredJunctionStorage.get();
        RA->chunkOutSJ1 = chunkOutSJ1;
    } else {
        RA->chunkOutSJ1  = NULL;
    };

    
    

    if (P.pCh.segmentMin>0) {
       if (P.pCh.out.samOld) {
            chunkFstreamOpen(P.outFileTmp + "/Chimeric.out.sam.thread", iChunk, RA->chunkOutChimSAM);
       };
       if (P.pCh.out.junctions) {
            chunkFstreamOpen(P.outFileTmp + "/Chimeric.out.junction.thread", iChunk, *RA->chunkOutChimJunction);
       };
    };

    if (P.outReadsUnmapped=="Fastx" ) {
        for (uint32 imate=0; imate < P.readNends; imate++) 
            chunkFstreamOpen(P.outFileTmp + "/Unmapped.out.mate"+ to_string(imate) +".thread",iChunk, RA->chunkOutUnmappedReadsStream[imate]);
    };

    if (P.outFilterType=="BySJout") {
        chunkFstreamOpen(P.outFileTmp + "/FilterBySJoutFiles.mate1.thread",iChunk, RA->chunkOutFilterBySJoutFiles[0]);
        if (P.readNends==2) chunkFstreamOpen(P.outFileTmp + "/FilterBySJoutFiles.mate2.thread",iChunk, RA->chunkOutFilterBySJoutFiles[1]); //here we do not output barcode read
    };

    if (P.wasp.yes) {
        waspStorage.reset(new ReadAlign(Pin,genomeIn,TrIn,iChunk)); RA->waspRA=waspStorage.get();
    };
    if (P.peOverlap.yes) {
        mergedStorage.reset(new ReadAlign(Pin,genomeIn,TrIn,iChunk)); RA->peMergeRA=mergedStorage.get();
        RA->peMergeRA->borrowChimericJunctionStream(RA->chunkOutChimJunction);
        RA->peMergeRA->outBAMunsorted=RA->outBAMunsorted;
        RA->peMergeRA->outBAMcoord=RA->outBAMcoord;
    };
};

///////////////
void ReadAlignChunk::chunkFstreamOpen(string filePrefix, int iChunk, fstream &fstreamOut) {//open fstreams for chunks
    ostringstream fNameStream1;
    fNameStream1 << filePrefix << iChunk;
    string fName1=fNameStream1.str();
    P.inOut->logMain << "Opening the file: " << fName1 << " ... " <<flush;

    remove(fName1.c_str()); //remove the file
    fstreamOut.open(fName1.c_str(),ios::out); //create empty file
    fstreamOut.close();
    fstreamOut.open(fName1.c_str(), ios::in | ios::out); //re-open the file in in/out mode

    if (fstreamOut.fail()) {
        P.inOut->logMain << "failed!\n";
        ostringstream errOut;
        errOut << "EXITING because of FATAL ERROR: could not create output file "<< fName1 << "\n";
        errOut << "Solution: check that you have permission to write this file\n";
        exitWithError(errOut.str(),std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
    };
    P.inOut->logMain << "ok" <<endl;
};

void ReadAlignChunk::chunkFstreamCat (fstream &chunkOut, ofstream &allOut, bool mutexFlag, pthread_mutex_t &mutexVal){
    chunkOut.flush();
    chunkOut.seekg(0,ios::beg);
    if (mutexFlag) pthread_mutex_lock(&mutexVal);
    allOut << chunkOut.rdbuf();
    allOut.clear();
    allOut.flush();
    allOut.clear();
    if (mutexFlag) pthread_mutex_unlock(&mutexVal);
    chunkOut.clear();
    chunkOut.seekp(0,ios::beg); //set put pointer at the beginning
};


void ReadAlignChunk::chunkFilesCat(ostream *allOut, string filePrefix, uint &iC) {//concatenates a file into main output
            while (true) {
                ostringstream name1("");
                name1 << filePrefix <<iC;
                ifstream fileChunkIn(name1.str().c_str());
                if (fileChunkIn.good()) {
                    *allOut << fileChunkIn.rdbuf();
                    allOut->flush();
                    allOut->clear();
                    fileChunkIn.close();
                    fileChunkIn.clear();
                    remove(name1.str().c_str());
                    iC++;
                } else {
                    fileChunkIn.close();
                    break;
                };
            };
};

