#ifndef H_SoloFeature
#define H_SoloFeature

#include <fstream>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "IncludeDefine.h"
#include "ReadAlignChunk.h"
#include "Transcriptome.h"

#include "SoloCommon.h"
#include "SoloRead.h"
#include "ReadAlignChunk.h"

#include "SoloFilteredCells.h"

class SoloFeature {
private:
    std::unique_ptr<SoloReadFeature> summaryStorage;
    std::unique_ptr<SoloReadFeature*[]> threadFeatureViews;
    std::vector<std::unique_ptr<std::fstream>> redistributionStorage;
    Parameters &P;
    ReadAlignChunk **RAchunk;    
    Transcriptome &Trans;
    
    SoloFeature **soloFeatAll;
    
    static const uint32 umiArrayStride=3;
    enum {rguG, rguU, rguR};
    uint32 rguStride;
    
public:
    ParametersSolo &pSolo;

    SoloReadFeature *readFeatSum=nullptr, **readFeatAll=nullptr;
    SoloReadBarcode *readBarSum;

    const int32 featureType;   

    uint64 nReadsMapped, nReadsInput; //total number of mapped reads
    uint32 nCB;
    uint32 featuresNumber; //number of features (i.e. genes, SJs, etc)

    uint32 *rGeneUMI=nullptr;//mapped reads sorted by CB; temporary borrow
    uint32 *rCBn=nullptr;//number of reads for detected CBs in the whitelist
    uint32 **rCBp=nullptr;//array of pointers to each CB sub-array; temporary borrow

    std::vector<uint32> indCB;//index of detected CBs in the whitelist
    std::vector<uint32> indCBwl; //reverse of indCB: index of WL CBs in detected CB list
    std::vector<uint32> nUMIperCB, nUMIperCBsorted;//number of UMIs per CB, and the same sorted (descendant)
    std::vector<uint32> nGenePerCB, nGenePerCBmulti;//number of genes (with >0 UMIs) per CB
    std::vector<uint32> nReadPerCB;//number of reads per CB. With multimappers: all aligns per CB
    std::vector<uint32> nReadPerCBunique, nReadPerCBtotal; //number of unique and multiple reads per CB
    uint32 nReadPerCBmax;

    std::vector<double> nUMIperCBmulti;

    std::vector<uint32> countCellGeneUMI;//sparsified matrix for the counts, each entry is: geneID count1 count2 ... countNcounts
    std::vector<uint32> countCellGeneUMIindex;//index of CBs in the count matrix
    uint32 countMatStride; //number of counts per entry in the count matrix
    
    struct {
        std::vector<double> m;
        std::vector<uint32> i;
        uint32 s;
    } countMatMult;
    
    std::vector<std::unordered_map<uint32, std::unordered_set<uint64>>> cbFeatureUMImap; //for SmartSeq counting
       
    std::string outputPrefix, outputPrefixFiltered;
    
    SoloFilteredCells filteredCells;
    
    std::array<std::vector<uint64>,2> sjAll;
    
    std::vector<readInfoStruct> readInfo; //corrected CB/UMI information for each read
    SoloReadFlagClass readFlagCounts;

    
    std::vector<uint32> redistrFilesCBindex, redistrFilesCBfirst; //redistr file for each CB, CB boundaries in redistributed files
    std::vector<uint64> redistrFilesNreads; //number of reads in each file
    std::vector <std::fstream*> redistrFilesStreams;

    SoloFeature(Parameters &Pin, ReadAlignChunk **RAchunk, Transcriptome &inTrans, int32 feTy, SoloReadBarcode *readBarSumIn, SoloFeature **soloFeatAll);
    SoloFeature(const SoloFeature&) = delete;
    SoloFeature& operator=(const SoloFeature&) = delete;
    void clearLarge(); //clear large vectors
    void processRecords();
    void sumThreads();
    void countSmartSeq();
    void countCBgeneUMI();
    void inputRecordsAllFiles(const std::vector<uint32> &wlToSlot, uint32 **rCBpa, std::vector<uint32> &nReadPerCBunique1, std::vector<uint32> &nReadPerCBmulti1);
    void countVelocyto();
    void quantTranscript();
    
    void collapseUMI(uint32 iCB, uint32 *umiArray);
    void collapseUMI_CR(uint32 iCB, uint32 *umiArray);
    void collapseUMIall();
    void collapseUMIperCB(uint32 iCB, std::vector<uint32> &umiArray, std::vector<uint32> &gID,  std::vector<uint32> &gReadS);
    //collapseUMIperCB steps (SoloFeature_collapseUMIall.cpp)
    using UmiGeneCounts = std::unordered_map<uintUMI, std::unordered_map<uint32,uint32>>; //UMI -> gene -> read count
    void groupReadsByGene(uint32 *rGU, uint32 rN, std::vector<uint32> &gID, std::vector<uint32> &gReadS, uint32 &nGenes, uint32 &nGenesMult);
    void filterMultiGeneUMIs(const uint32 *rGU, uint32 uniqueEnd, UmiGeneCounts &umiGeneMapCount);
    void collapseGeneUMIs(uint32 iCB, uint32 iG, uint32 *rGU, const std::vector<uint32> &gID, const std::vector<uint32> &gReadS, std::vector<uint32> &umiArray,
                          UmiGeneCounts &umiGeneMapCount, UmiGeneCounts &umiGeneMapCount0, std::unordered_map<uintUMI,uintUMI> &umiCorrected);
    void countMultiGeneUMI_CR(uint32 iCB, uint32 nGenes, uint32 *rGU, const std::vector<uint32> &gID, const std::vector<uint32> &gReadS,
                              UmiGeneCounts &umiGeneMapCount, UmiGeneCounts &umiGeneMapCount0, std::vector<std::unordered_map<uintUMI,uintUMI>> &umiCorrected);
    void collapseMultiGeneReads(uint32 iCB, uint32 *rGU, const std::vector<uint32> &gReadS, uint32 nGenes, uint32 nGenesMult, const UmiGeneCounts &umiGeneMapCount);
    std::vector<std::vector<uint32>> multiGeneUMIgenes(uint32 *rGUm, uint32 nRm, const UmiGeneCounts &umiGeneMapCount);
    std::vector<double> uniqueGeneCounts(uint32 iCB, const std::map<uint32,uint32> &genesM, uint32 indDedup);
    static std::vector<double> multiGeneRescue(const std::vector<std::vector<uint32>> &umiGenes, const std::vector<double> &gEuniform, const std::vector<double> &gEu);
    static std::vector<double> multiGenePropUnique(const std::vector<std::vector<uint32>> &umiGenes, const std::vector<double> &gEu);
    static std::vector<double> multiGeneEM(const std::vector<std::vector<uint32>> &umiGenes, const std::vector<double> &gEuniform, const std::vector<double> &gEu);

    uint32 umiArrayCorrect_CR         (const uint32 nU0, uintUMI *umiArr, const bool readInfoRec, const bool nUMIyes, std::unordered_map <uintUMI,uintUMI> &umiCorr);
    uint32 umiArrayCorrect_Directional(const uint32 nU0, uintUMI *umiArr, const bool readInfoRec, const bool nUMIyes, std::unordered_map <uintUMI,uintUMI> &umiCorr, const int32 dirCountAdd);
    uint32 umiArrayCorrect_Graph      (const uint32 nU0, uintUMI *umiArr, const bool readInfoRec, const bool nUMIyes, std::unordered_map <uintUMI,uintUMI> &umiCorr);

    void outputResults(bool cellFilterYes, std::string outputPrefixMat);
    //outputResults steps (SoloFeature_outputResults.cpp)
    void outputFeatures(const std::string &outputPrefixMat);
    uint64 outputBarcodes(bool cellFilterYes, const std::string &outputPrefixMat);
    void outputCountMatrices(bool cellFilterYes, const std::string &outputPrefixMat, uint64 nCellGeneEntries);
    void outputUniqueAndMultMatrices(const std::string &outputPrefixMat);
    uint64 uniqueAndMultCellCounts(uint32 icb, uint32 iDed, uint32 mIndex, bool nGeneUMIperCBmultiFill, std::ostringstream &matOutStringStream);
    void addBAMtags(char *&bam0, uint32 &size0, char* bam1);
    void statsOutput();
    void redistributeReadsByCB();
    
    void cellFiltering();
    void emptyDrops_CR();
    void loadRawMatrix();
};

#endif
