#ifndef H_SoloReadFeature
#define H_SoloReadFeature
#include <set>
#include <map>
#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "SoloReadBarcode.h"
#include "SoloCommon.h"
#include "SoloReadFeatureStats.h"
#include "ReadAnnotations.h"
#include <memory>

class SoloFeature;

class SoloReadFeature {
public:

    uint32 homoPolymer[4];//homopolymer constants

    std::vector<uint32> cbReadCount;
    std::map <uintCB,uint32> cbReadCountMap;
    
    std::vector<uint32> transcriptDistCount;
    
    bool readInfoYes ,readIndexYes;

    std::fstream *streamReads=nullptr; //borrowed by Solo until counting/sorting finish

    std::string cbSeq, umiSeq, cbQual, umiQual;

    SoloReadFlagClass readFlag;
    
    SoloReadFeatureStats stats;

    SoloReadFeature (int32 feTy, Parameters &Pin, int iChunk);
    void record(SoloReadBarcode &soloBar, uint64 nTr, Transcript **alignOut, uint64 iRead, ReadAnnotations &readAnnot);
    void addCounts(const SoloReadFeature &soloCBin);
    void addStats(const SoloReadFeature &soloCBin);
    void statsOut(std::ofstream &streamOut);
    void inputRecords(uint32 **cbP, uint32 cbPstride, std::vector<uint32> &cbReadCountTotal, std::vector<readInfoStruct> &readInfo, SoloReadFlagClass &readFlagCounts,
                      std::vector<uint32> &nReadPerCBunique1, std::vector<uint32> &nReadPerCBmulti1);

private:
    std::unique_ptr<std::fstream> streamStorage;
    const int32 featureType;

    Parameters &P;
    ParametersSolo &pSolo;
};

#endif
