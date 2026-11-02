#ifndef H_SoloReadBarcode
#define H_SoloReadBarcode
#include <set>
#include "IncludeDefine.h"
#include "Parameters.h"
#include "SoloReadBarcodeStats.h"
#include "BarcodeHint.h"

class SoloReadBarcode {
private:
    Parameters &P;
    
public:
    ParametersSolo &pSolo;    

    
    uint32 homoPolymer[4];//homopolymer constants
    std::string cbSeq, umiSeq, cbQual, umiQual, bSeq, bQual;
    std::vector<std::string> bStrings; //barcode strings from SAM tags
    std::string cbSeqCorrected;
    BarcodeHint hint; //precomputed whitelist lookup for the next read, used only if its key matches
    uint64 umiB;
    int32 cbMatch;//-1: no match, 0: exact, 1: 1 match with 1MM, >1: # of matches with 1MM
    int32 umiCheck;//umi check status
    std::string cbMatchString;//CB matches and qualities
    std::vector<uint64> cbMatchInd;//matches
    std::vector<uint32> cbReadCountExact;
    //map <uint32,uint32> cbReadCountMap;//count read per CB for no WL

    std::array<uint64,256> qualHist;
    SoloReadBarcodeStats stats;

    SoloReadBarcode(Parameters &Pin);
    void getCBandUMI(char **readSeq, char **readQual, uint64 *readLen, const std::string &readNameExtraIn, const uint32 &readFilesIndex, const char *readName);
    void addCounts(const SoloReadBarcode &rfIn);
    void addStats(const SoloReadBarcode &rfIn);
    void statsOut(std::ofstream &streamOut);
    void matchCBtoWL(std::string &cbSeq1, std::string &cbQual1, std::vector<uint64> &cbWL, int32 &cbMatch1, std::vector<uint64> &cbMatchInd1, std::string &cbMatchString1);
    bool convertCheckUMI();
    //getCBandUMI steps (SoloReadBarcode_getCBandUMI.cpp)
    std::string samTagValue(const std::string &attributes, const std::string &tag, const char *what, const char *option, const char *readName);
    void barcodeFromSAMtags(const std::string &readNameExtraIn, const char *readName);
    void checkBarcodeLength(const char *readName);
    bool cbUMIsimple();
    void cbSamTagOut();
    bool cbUMIcomplex();
    void addStats(const int32 cbMatch1);
    

protected:
    void readNameExtra();
};

#endif
