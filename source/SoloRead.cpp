#include "SoloRead.h"

SoloRead::SoloRead(Parameters &Pin, int32 iChunkIn) :  iChunk(iChunkIn), P(Pin), pSolo(P.pSolo)
{
    barcodeStorage.reset(new SoloReadBarcode(P)); readBar=barcodeStorage.get();
    
    if (pSolo.type==0)
        return;
    if (pSolo.type==pSolo.SoloTypes::CB_samTagOut)
        return;
    
    featurePointers.reset(new SoloReadFeature*[pSolo.nFeatures]); readFeat=featurePointers.get();
    featureStorage.resize(pSolo.nFeatures);

    for (uint32 ii=0; ii<pSolo.nFeatures; ii++)
    {
        featureStorage[ii].reset(new SoloReadFeature(pSolo.features[ii], P, iChunk));
        readFeat[ii]=featureStorage[ii].get();
    }
};

void SoloRead::releaseBarcode() {
    barcodeStorage.reset();
    readBar=nullptr;
}

void SoloRead::readFlagReset()
{
    for (uint32 ii=0; ii<pSolo.nFeatures; ii++)
        readFeat[ii]->readFlag.flag = 0;
};
