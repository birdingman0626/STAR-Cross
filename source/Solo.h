#ifndef H_Solo
#define H_Solo
#include "IncludeDefine.h"
#include "ReadAlignChunk.h"
#include "Transcriptome.h"
#include <fstream>

#include "SoloFeature.h"


class Solo {
private:
    ReadAlignChunk **RAchunk;
    Parameters &P;
    Transcriptome &Trans;
    std::unique_ptr<SoloReadBarcode> barcodeStorage;
    std::unique_ptr<SoloFeature*[]> featureViews;
    std::vector<std::unique_ptr<SoloFeature>> featureStorage;

public:
    ParametersSolo &pSolo;
    SoloFeature **soloFeat=nullptr;
    
    SoloReadBarcode *readBarSum=nullptr;

    Solo(ReadAlignChunk **RAchunk, Parameters &Pin, Transcriptome &inTrans);
    
    Solo(Parameters &Pin, Transcriptome &inTrans);//for soloCellFiltering
    Solo(const Solo&) = delete;
    Solo& operator=(const Solo&) = delete;
    void releaseStorage(); // after sorting/tag consumers, before chunks/temp deletion

    void processAndOutput();
};

#endif
