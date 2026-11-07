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
class ReadSoloFeatures; //per-read features, SoloReadFeature_record.cpp
struct SoloInputRecord; //one temporary-file record, SoloReadFeature_inputRecords.cpp

//where inputRecords puts the feature records of one temporary file: each CB has a slot of whole records in a shared array
struct SoloRecordSlots {
    bool countOnly=true;               //true: only count the records of each CB in pos[], store nothing
    const uint32 *wlToSlot=nullptr;    //whitelist CB -> slot, (uint32)-1 if the CB has no slot
    uint32 *const *slotStart=nullptr;  //slot -> its first record
    uint32 stride=0;                   //uint32 values per record
    std::vector<uint32> pos;           //per slot: record count (countOnly), or index in the slot of this file's next record
    uint64 nNoSlot=0;                  //records whose CB has no slot; nonzero means the slots were sized wrong
};

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
    void inputRecords(SoloRecordSlots &slots, std::vector<uint32> &cbReadCountTotal, std::vector<readInfoStruct> &readInfo, SoloReadFlagClass &readFlagCounts,
                      std::vector<uint32> &nReadPerCBunique1, std::vector<uint32> &nReadPerCBmulti1);

private:
    std::unique_ptr<std::fstream> streamStorage;
    const int32 featureType;

    Parameters &P;
    ParametersSolo &pSolo;

    //record steps (SoloReadFeature_record.cpp)
    void recordReadFlags(SoloReadBarcode &soloBar, uint64 nTr, Transcript **alignOut, ReadAnnotations &readAnnot);
    uint32 recordGene(SoloReadBarcode &soloBar, uint64 nTr, uint64 iRead, ReadAnnotations &readAnnot, ReadSoloFeatures &reFe);
    uint32 recordSJ(SoloReadBarcode &soloBar, uint64 nTr, Transcript **alignOut, uint64 iRead, ReadAnnotations &readAnnot, ReadSoloFeatures &reFe);
    uint32 recordTranscript3p(SoloReadBarcode &soloBar, uint64 iRead, ReadAnnotations &readAnnot, ReadSoloFeatures &reFe);
    uint32 recordVelocyto(uint64 iRead, ReadAnnotations &readAnnot);
    void addCBreadCount(SoloReadBarcode &soloBar, uint32 nFeat);
    //inputRecords steps (SoloReadFeature_inputRecords.cpp)
    bool inputSingleCB(SoloInputRecord &r, SoloRecordSlots &slots, std::vector<uint32> &cbReadCountTotal, std::vector<readInfoStruct> &readInfo);
    void inputMultipleCB(SoloInputRecord &r, SoloRecordSlots &slots, std::vector<uint32> &cbReadCountTotal, std::vector<readInfoStruct> &readInfo);
    void storeInputRecord(SoloInputRecord &r, SoloRecordSlots &slots, std::vector<readInfoStruct> &readInfo);
    void countInputRead(const SoloInputRecord &r, SoloReadFlagClass &readFlagCounts, std::vector<uint32> &nReadPerCBunique1, std::vector<uint32> &nReadPerCBmulti1);
};

#endif
