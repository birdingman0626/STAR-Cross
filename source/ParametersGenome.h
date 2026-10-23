#ifndef CODE_ParametersGenome
#define CODE_ParametersGenome

#include "IncludeDefine.h"
#include <unordered_set>

class Parameters;

class ParametersGenome {//"constant" genome parameters - user input
public:
    std::string gDir;
    std::string gLoad;
    
    uint32 gType;//type code
    std::string gTypeString;
    
    std::vector <std::string> gFastaFiles;
    std::vector <std::string> gChainFiles;
    //string gConsensusFile; DEPRECATED

    struct {
        int32 type;
        std::string typeString;
        std::string vcfFile;
        std::vector<std::string> output; //which output to transform
        bool outYes=false, outSAM=false, outSJ=false, outQuant=false;
    } transform;
    
    uint64 gSAindexNbases;//length of the SA pre-index strings
    uint64 gChrBinNbits;
    uint64 gSAsparseD;//SA sparsity
    uint64 gSuffixLengthMax;//maximum length of the suffixes, has to be longer than read length
    std::vector <uint64> gFileSizes;//size of the genome files

    std::vector <std::string> sjdbFileChrStartEnd;
    std::string sjdbGTFfile;
    std::string sjdbGTFchrPrefix;
    
    std::string sjdbGTFfeatureExon;
    std::string sjdbGTFtagExonParentTranscript;
    std::string sjdbGTFtagExonParentGene;
    std::vector<std::string> sjdbGTFtagExonParentGeneName;
    std::vector<std::string> sjdbGTFtagExonParentGeneType;
    
    std::string sjdbInsertSave;
    uint64 sjdbOverhang;
    int sjdbOverhang_par;
    int sjdbScore;

    struct {
        std::vector<std::string> mitoStrings;
        std::unordered_set<uint64> mito;
    } chrSet;

    
    void initialize(Parameters *Pin);

private:
    Parameters *pP;
    
};

#endif
