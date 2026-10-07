#include "Solo.h"
#include "TimeFunctions.h"
#include "streamFuns.h"

Solo::Solo(ReadAlignChunk **RAchunkIn, Parameters &Pin, Transcriptome &inTrans)
                       :  RAchunk(RAchunkIn), P(Pin), Trans(inTrans), pSolo(P.pSolo)
{
    if ( pSolo.type == 0 )
        return;
    
    barcodeStorage.reset(new SoloReadBarcode(P));
    readBarSum=barcodeStorage.get();
    
    if ( pSolo.type == pSolo.SoloTypes::CB_samTagOut )
        return;

    featureViews.reset(new SoloFeature*[pSolo.nFeatures]);
    soloFeat=featureViews.get();
    featureStorage.reserve(pSolo.nFeatures);
    for (uint32 ii=0; ii<pSolo.nFeatures; ii++) {
        featureStorage.emplace_back(new SoloFeature(P, RAchunk, Trans, pSolo.features[ii], readBarSum, soloFeat));
        soloFeat[ii]=featureStorage.back().get();
    }
};

///////////////////////////////////////////////////////////////////////////////////// post-mapping processing only
//overloaded: only soloCellFiltering
Solo::Solo(Parameters &Pin, Transcriptome &inTrans)
          :  RAchunk(nullptr), P(Pin), Trans(inTrans), pSolo(P.pSolo)
{
    if ( P.runMode != "soloCellFiltering" )
        return; //passing through, return back to executing STAR
        
    time_t timeCurrent;

    time( &timeCurrent);
    *P.inOut->logStdOut << timeMonthDayTime(timeCurrent) << " ..... starting SoloCellFiltering" <<endl;
    
    featureViews.reset(new SoloFeature*[1]);
    soloFeat=featureViews.get();
    
    featureStorage.emplace_back(new SoloFeature(P, NULL, Trans, -1, NULL, soloFeat));
    soloFeat[0]=featureStorage.back().get();
    soloFeat[0]->loadRawMatrix();
    soloFeat[0]->cellFiltering();
    
    time( &timeCurrent);
    *P.inOut->logStdOut << timeMonthDayTime(timeCurrent) << " ..... finished successfully\n" <<flush;
    P.inOut->logMain  << "ALL DONE!\n" << flush;
    // main returns normally, releasing scoped resources on this success path.
};

void Solo::releaseStorage() {
    featureStorage.clear();
    featureViews.reset();
    soloFeat=nullptr;
    barcodeStorage.reset();
    readBarSum=nullptr;
}


////////////////////////////////////////////////////////////////////////////////////
void Solo::processAndOutput()
{
    if (pSolo.type==0 )
        return;
    
    {//collect barcode statistics    
        if (pSolo.cbWLyes) {//now we can define WL and counts
            for (int ii=0; ii<P.runThreadN; ii++) {
                readBarSum->addCounts(*RAchunk[ii]->RA->soloRead->readBar);
                readBarSum->addStats(*RAchunk[ii]->RA->soloRead->readBar);
                RAchunk[ii]->RA->soloRead->releaseBarcode(); //not needed anymore
            };
        };

        ofstream *statsStream = &ofstrOpen(P.outFileNamePrefix+pSolo.outFileNames[0]+"Barcodes.stats",ERROR_OUT, P);
        std::unique_ptr<ofstream> statsStorage(statsStream);
        readBarSum->statsOut(*statsStream);
        statsStream->close();

        //pseudocounts
        if (pSolo.CBmatchWL.mm1_multi_pc) {
            for (uint32 ii=0; ii<pSolo.cbWLsize; ii++) {
                readBarSum->cbReadCountExact[ii]++;//add one to exact counts
            };
        };
    };
    
    if (pSolo.type==pSolo.SoloTypes::CB_samTagOut)
        return;

    {//process all features
        *P.inOut->logStdOut << timeMonthDayTime() << " ..... started Solo counting\n" <<flush;
        P.inOut->logMain    << timeMonthDayTime() << " ..... started Solo counting\n" <<flush;

        for (uint32 ii=0; ii<pSolo.nFeatures; ii++) {
            soloFeat[ii]->processRecords();
            //if (!pSolo.readInfoYes[soloFeat[ii]->featureType]) {
            //    delete soloFeat[ii];
            //};
        };

        *P.inOut->logStdOut << timeMonthDayTime() << " ..... finished Solo counting\n" <<flush;
        P.inOut->logMain    << timeMonthDayTime() << " ..... finished Solo counting\n" <<flush;
    };
};
