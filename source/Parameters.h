#ifndef PARAMETERS_DEF
#define PARAMETERS_DEF

#include "IncludeDefine.h"
#include "InOutStreams.h"
#include "ParameterInfo.h"
#include <map>
#include "TimeFunctions.h"
#ifdef _WIN32
    #include "wincompat.h"
#else
    #include <unistd.h>
    #include <signal.h>
#endif
#include "ParametersChimeric.h"
#include "ParametersSolo.h"
#include "ParametersClip.h"
#include "ParametersGenome.h"
#include "AlignFilterConfig.h"
#include "OutputConfig.h"
#include <vector>
#include <array>
#include <unordered_set>

class Parameters {

    public:
        std::vector <ParameterInfoBase*> parArray, parArrayInitial;
        std::vector <std::string> parameterInputName;

        std::string commandLine, commandLineFull;

        //version
        std::string versionGenome;

        //system parameters
        std::string sysShell; //shell for executing system commands

        // run parameters
        std::string runMode;
        std::vector<std::string> runModeIn;
        int runThreadN;
        mode_t runDirPerm;
        std::string runDirPermIn; //permission for directores created at run-time
        int runRNGseed; //random number generator seed
        std::string legacyIn; //legacy algorithm selection
        std::string gpuSjdbRemapMode; //experimental packed SA remap, default off

        struct {
            std::string host;
            int    port;
            std::string metricsIn; // parsed to metrics in webui mode
            bool   metrics;
        } webui;

        struct {
            int32 type;//0 no restart, 1 no mapping - restart from _STARtmp files
        } runRestart; //restart options - in development
        
        //parameters
        std::vector <std::string> parametersFiles;

        //input
        std::string inputBAMfile;

        //genome
        char genomeNumToNT[6];
        ParametersGenome pGe, pGeOut;

        struct {//--win*: binning, windows, anchors
            uint64 binChrNbits, binNbits, anchorDistNbins, flankNbins, binN;
            uint64 anchorMultimapNmax; //max number of alignments for anchors
            double readCoverageRelativeMin;
            uint64 readCoverageBasesMin;
        } win;

        //read parameters
        int readFilesIndex;

        uint64 readMapNumber;
        uint64 iReadAll;
        uint64 readNmates, readNends;
        std::string readMatesLengthsIn;
        uint32 readQualityScoreBase;

        std::vector <std::string> readNameSeparator;
        std::vector <char> readNameSeparatorChar;

        std::string outSAMreadID;
        bool outSAMreadIDnumber;
        
        struct {//--readFiles*
            std::vector <std::string> type;
            int typeN;
            std::string prefix, prefixFinal;
            std::vector <std::string> in, inTmp;
            uint32 N;
            std::vector <std::vector <std::string> > names;
            std::vector <std::string> command;
            std::vector <std::string> manifest;
            std::string commandString; //actual command string
            pid_t commandPID[MAX_N_MATES]{};

            std::vector<std::string> samAttrKeepIn; //input vector of SAM tags to keep, if readFilesType=SAMtag
            std::unordered_set<uint16_t> samAttrKeep;
            bool samAttrKeepAll, samAttrKeepNone;
        } readFiles;

        ParametersClip pClip;

        struct {//--align*
            uint64 SJoverhangMin,SJDBoverhangMin,splicedMateMapLmin; //min SJ donor/acceptor length
            double splicedMateMapLminOverLmate;
            uint64 windowsPerReadNmax; //max number of alignment windows per read
            uint64 transcriptsPerWindowNmax; //maximum number of transcripts recorded per window
            uint64 transcriptsPerReadNmax;   //max number of alignments per read
            uint64 intronMin;//min length to call a gap an intron
            uint64 intronMax;//max length to call
            uint64 matesGapMax;//max gap between the mates (if paired-end)
            std::vector <int32> SJstitchMismatchNmax;

            struct {
                std::string in;
                bool yes;
            } softClipAtReferenceEnds;

            struct {
                std::string in;
                bool ext[2][2];
            } endsType;

            struct {
                std::vector<std::string> in;
                int nBasesMax;
                bool concordantPair;
            } endsProtrude;

            struct {
                std::string in;
                bool flushRight;
            } insertionFlush;
        } align;

        struct {//--seed*
            uint64 multimapNmax; //max number of multiple alignments per piece
            uint64 searchLmax; //max length of the seed
            uint64 perReadNmax; //max number of pieces per Read
            uint64 perWindowNmax; //max number of aligns per window
            uint64 noneLociPerWindow; //max number of aligns from one piece per window
            uint64 searchStartLmax;
            double searchStartLmaxOverLread; //length of split start points
            uint64 splitMin, mapMin;
            std::string searchBackend; //off/cpu/auto/required, experimental batched seeding
            uint64 batchSize;
        } seed;

        //chunk parameters
        uint64 chunkInSizeBytes,chunkInSizeBytesArray,chunkOutBAMsizeBytes;

        //output
        std::string outFileNamePrefix, outStd;
        std::string outTmpDir, outTmpKeep;
        std::string outLogFileName;

        //SAM output
        std::string outBAMfileCoordName, outBAMfileUnsortedName, outQuantBAMfileName;
        std::string outCRAMfileCoordName, outCRAMfileUnsortedName;//referenceless CRAM outputs (--outSAMtype CRAM)
        std::string samHeader, samHeaderHD, samHeaderSortedCoord, samHeaderExtra;
        std::string outSAMmode,  outSAMorder, outSAMprimaryFlag;
        std::vector<std::string> outSAMattributes, outSAMheaderHD, outSAMheaderPG;
        std::vector<std::string> outSAMattrRGline,outSAMattrRGlineSplit,outSAMattrRG;
        OutputConfig output;
        uint64 outSAMattrIHstart;
        std::string outSAMheaderCommentFile;

        struct {
            std::string in;
            uint32 type;
        } outSAMstrandField;

        int outSAMtlen;

        struct {bool NH,HI,AS,NM,MD,nM,jM,jI,RG,XS,rB,vG,vA,vW,ha,ch,MC,CR,CY,UR,UY,CB,UB,GX,GN,gx,gn,sM,sS,sQ,cN,sF;} outSAMattrPresent, outSAMattrPresentQuant;

        std::vector <int> outSAMattrOrder, outSAMattrOrderQuant;
        std::vector <std::string> outSAMtype;
        bool outBAMunsorted, outBAMcoord, outSAMbool;
        bool outCRAMbool;//true when --outSAMtype CRAM: produce BAM then transcode to referenceless CRAM
        uint32 outBAMcoordNbins;
        uint32 outBAMsortingBinsN;//user-defined number of bins for sorting
        std::string outBAMsortTmpDir;

        struct {
            std::string mode;
            bool yes;
            bool markMulti;
            uint64 mate2basesN;
        } removeDuplicates;

        int outBAMsortingThreadN, outBAMsortingThreadNactual;
        std::vector<uint64> outBAMsortingBinStart; //owned genomic starts for sorting bins
        uint16 outSAMflagOR, outSAMflagAND;

        struct {
            std::vector <std::string> mode;
            bool yes;
            bool within;//output unmapped reads within SAM/BAM files
            bool keepPairs;//keep mates together
        } outSAMunmapped;

        struct {
            std::vector <std::string> mode;
            bool yes;
            bool KeepOnlyAddedReferences;
            bool KeepAllAddedReferences;
        } outSAMfilter;

        struct {
            std::string mode;
            bool random;
        } outMultimapperOrder;

        struct {
            bool yes;
            uint64 NbasesMin;
            double MMp;
        } peOverlap;

        std::string outReadsUnmapped;
        int outQSconversionAdd;
        std::string outFileTmp;

        //output filtering
        AlignFilterConfig alignFilter;

        std::string outFilterIntronMotifs,outFilterIntronStrands;
        std::string outFilterType; //type of filtering
        int outFilterBySJoutStage; //indicates the stage of filtering by SJout

        struct {
            std::vector<std::string> type;
            bool yes;
        } outSJ;
        
        struct {//--outSJfilter*: output filtering of junctions
            std::string reads;
            std::vector <int32> countUniqueMin, countTotalMin;
            std::vector <int32> overhangMin;
            std::vector <int32> distToOtherSJmin; //min allowed distance to other SJ's donor/acceptor
            std::vector <int32> intronMaxVsReadN;
        } outSJfilter;

        //wiggle output
        std::vector <std::string> outWigType, outWigStrand, outWigNorm;
        std::string outWigReferencesPrefix;
        struct {
            bool yes;
            bool strand;
            int type;
            int format;
            int norm;
        } outWigFlags;

        //2-pass
        struct {
            bool yes; //true in 2-pass mode
            bool pass2; //true if now running the 2nd pass
            uint64 pass1readsN;
            int pass1readsN_par;
            std::string dir;
            std::string pass1sjFile;
            std::string mode;
        } twoPass;

        //inserting junctions on the fly
        struct {
            bool yes; //insert?
            bool pass1;//insert on the 1st pass?
            bool pass2;//insert on the 2nd pass?
            std::string outDir;
        } sjdbInsert;

        struct {//--limit*: storage limits
            uint64 genomeGenerateRAM;
            std::vector<uint64> IObufferSize; //max size of the in/out buffer, bytes
            uint64 outSAMoneReadBytes;
            uint64 outSJoneRead, outSJcollapsed;
            uint64 BAMsortRAM;
            uint64 sjdbInsertNsj;
            uint64 nreadsSoft;
        } limit;

        struct {//--score*: penalties
            intScore gap, gapNoncan, gapGCAG, gapATAC, delBase, delOpen, insBase, insOpen;
            intScore stitchSJshift;//max negative score when shifting the stitching junction
            double genomicLengthLog2scale;
        } score;

        //quantification parameters
        //input

        struct {
          bool yes=false; //if any quantification is done
          std::vector <std::string> mode; //quantification mode input string

            struct {
                bool yes=false;
                bool bamYes;
                bool indel;
                bool softClip;
                bool singleEnd;
                int bamCompression;
                std::string output;
                std::string bamPrimary; // --quantTranscriptomeBAMprimary
                bool primaryByReadIndex=false; // ReadIndex: primary independent of threads
            } trSAM;

            struct {
                bool yes=false;
                std::string outFile;
            } geCount;

            struct {
                bool yes=false;
            } geneFull;
          
            struct {
                bool yes=false;
            } geneFull_Ex50pAS;

            struct {
                bool yes=false;
            } geneFull_ExonOverIntron;

            struct {
                bool yes=false;
            } gene;
          
        } quant;

        //variation parameters
        struct {
            bool yes=false;
            bool heteroOnly=false;
            std::string vcfFile;
        } var;

        struct {
            bool yes=false;
            bool SAMtag;
            std::string outputMode;
        } wasp;

        //solo
        ParametersSolo pSolo;

        //chimeric
        ParametersChimeric pCh;

        //splitting
        uint64 maxNsplit;

        //not really parameters, but global variables:
        std::array<std::vector<uint64>,2> sjAll;
        uint64 sjNovelN, *sjNovelStart, *sjNovelEnd; //novel junctions collapased and filtered
        // Pass copies borrow the arrays and share their actual lifetime.
        std::shared_ptr<uint64[]> sjNovelStartStorage,sjNovelEndStorage;

    ////////////////////// CLEAN-UP needed
    InOutStreams *inOut; //main input output streams

    uint64 Lread;

    Parameters();
    explicit Parameters(InOutStreams& borrowedStreams);
    int readParsFromFile(std::ifstream*, std::ofstream*, int); //read parameters from one file
    int readPars(); // read parameters from all files
    int scanOneLine (std::string &lineIn, int inputLevel, int inputLevelRequested);
    void scanAllLines (std::istream &streamIn, int inputLevel, int inputLevelRequested);
    void inputParameters (int argInN, char* argIn[]); //input parameters: default, from files, from command line
    void openReadsFiles();
    void readFilesInit();
    void closeReadsFiles();
    void readSAMheader(const std::string commandString, const std::vector<std::string> fileNames);
    void samAttributes();
    void samAttrRequiresBAM(bool attrYes, std::string attrTag);

private:
    // Pass 1 copies still borrow registered value addresses. Share registration
    // lifetime, not the values or CLI-managed streams.
    std::shared_ptr<std::vector<std::unique_ptr<ParameterInfoBase>>> parameterStorage;
    // Private helpers called from the constructor and inputParameters()
    void registerParameters();                        // Parameters_register.cpp
    void inputParameters_parseSources(int argInN, char* argIn[]); // Parameters_inputSources.cpp
    void inputParameters_runtimeSetup();              // Parameters_runtimeSetup.cpp
    void inputParameters_validate();                  // Parameters_validateInput.cpp
    // inputParameters_runtimeSetup stages, in call order
    void setupTempDirectory();
    void initWigAndDuplicates();
    void runBAMinputMode();
    void openAlignmentOutput();
    void openBAMoutput();
    // inputParameters_validate stages, in call order
    void validateRunParameters();
    void validateTwoPass();
    void openReadsAndUnmappedFiles();
    void validateMapqAndWASP();
    void validateQuantification();
    void validateAlignEnds();
    void validateSjdbInsert();
    void validateReadAndAlignOptions();
    void validateBuffersAndOutSJ();
    // inputParameters_parseSources stages, in call order
    void loadDefaultParameters();
    std::string scanInitialCommandLine(int argInN, char* argIn[]);
    void openMainLog();
    void selectStdOutStream();
    void logInitialCommandLine();
    void readParametersFiles();
    void logFinalParameters(const char *executable);
    // samAttributes stages, in call order
    void samAttributesReset();
    void samAttributeAdd(const std::string &attr);
    void samAttributesImplied();
    void samAttributesRequireBAM();
};
#endif  // Parameters.h
