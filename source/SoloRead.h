#ifndef H_SoloRead
#define H_SoloRead

#include "SoloReadBarcode.h"
#include "SoloReadFeature.h"
#include "ReadAnnotations.h"

class SoloRead {
public:
    SoloReadBarcode *readBar=nullptr; //borrowed views of per-read owners below
    SoloReadFeature **readFeat=nullptr;
    
    SoloRead(Parameters &Pin, int32 iChunkIn);
    void readFlagReset();
    void releaseBarcode();
    void record(uint64 nTr, Transcript **alignOut, uint64 iRead, ReadAnnotations &readAnnot);
    
private:
    std::unique_ptr<SoloReadBarcode> barcodeStorage;
    std::unique_ptr<SoloReadFeature*[]> featurePointers;
    std::vector<std::unique_ptr<SoloReadFeature>> featureStorage;
    const int32 iChunk;
    Parameters &P;
    ParametersSolo &pSolo;
};

#endif
