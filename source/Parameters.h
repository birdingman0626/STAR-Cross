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

        //binning,windows,anchors
        uint64 winBinChrNbits, winBinNbits, winAnchorDistNbins, winFlankNbins, winBinN;
        uint64 winAnchorMultimapNmax; //max number of alignments for anchors
        double winReadCoverageRelativeMin;
        uint64 winReadCoverageBasesMin;

        //read parameters
        std::vector <std::string> readFilesType;
        int readFilesTypeN;
        std::string readFilesPrefix, readFilesPrefixFinal;
        std::vector <std::string> readFilesIn, readFilesInTmp;
        uint32 readFilesN;
        std::vector <std::vector <std::string> > readFilesNames;
        std::vector <std::string> readFilesCommand;
        std::vector <std::string> readFilesManifest;
               
        std::string readFilesCommandString; //actual command string
        int readFilesIndex;
        pid_t readFilesCommandPID[MAX_N_MATES]{};

        uint64 readMapNumber;
        uint64 iReadAll;
        uint64 readNmates, readNends;
        std::string readMatesLengthsIn;
        uint32 readQualityScoreBase;

        std::vector <std::string> readNameSeparator;
        std::vector <char> readNameSeparatorChar;

        std::string outSAMreadID;
        bool outSAMreadIDnumber;
        
        //new: structure for readFiles parameters
        struct {
            std::vector<std::string> samAttrKeepIn; //input vector of SAM tags to keep, if readFilesType=SAMtag
            std::unordered_set<uint16_t> samAttrKeep;
            bool samAttrKeepAll, samAttrKeepNone;
        } readFiles;

        ParametersClip pClip;

        //align parameters
        uint64 alignSJoverhangMin,alignSJDBoverhangMin,alignSplicedMateMapLmin; //min SJ donor/acceptor length
        double alignSplicedMateMapLminOverLmate;
        uint64 alignWindowsPerReadNmax; //max number of alignment windows per read
        uint64 alignTranscriptsPerWindowNmax; //maximum number of transcripts recorded per window
        uint64 alignTranscriptsPerReadNmax;   //max number of alignments per read
        uint64 alignIntronMin;//min length to call a gap an intron
        uint64 alignIntronMax;//max length to call
        uint64 alignMatesGapMax;//max gap between the mates (if paired-end)
        std::vector <int32> alignSJstitchMismatchNmax;

        //         struct {
        //             string strandString;
        //             int32 strand;
        //         } pReads;

        struct {
            std::string in;
            bool yes;
        } alignSoftClipAtReferenceEnds;

        struct {
            std::string in;
            bool ext[2][2];
        } alignEndsType;

        struct {
            std::vector<std::string> in;
            int nBasesMax;
            bool concordantPair;
        } alignEndsProtrude;

        struct {
            std::string in;
            bool flushRight;
        } alignInsertionFlush;


        //seed parameters
        uint64 seedMultimapNmax; //max number of multiple alignments per piece
        uint64 seedSearchLmax; //max length of the seed
        uint64 seedPerReadNmax; //max number of pieces per Read
        uint64 seedPerWindowNmax; //max number of aligns per window
        uint64 seedNoneLociPerWindow; //max number of aligns from one piece per window
        uint64 seedSearchStartLmax;
        double seedSearchStartLmaxOverLread; //length of split start points
        uint64 seedSplitMin, seedMapMin;

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

//         string bamRemoveDuplicatesType;
//         uint64 bamRemoveDuplicatesMate2basesN;
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
        
        //output filtering SJs
        std::string outSJfilterReads;
        std::vector <int32> outSJfilterCountUniqueMin, outSJfilterCountTotalMin;
        std::vector <int32> outSJfilterOverhangMin;
        std::vector <int32> outSJfilterDistToOtherSJmin; //min allowed distance to other SJ's donor/acceptor
        std::vector <int32> outSJfilterIntronMaxVsReadN;

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
//         uint64 twoPass.pass1readsN, twoPass.sjLimit;
//         string twoPass.dir,twopassSJpass1file;
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

        //storage limits
        uint64 limitGenomeGenerateRAM;
        std::vector<uint64> limitIObufferSize; //max size of the in/out buffer, bytes
        uint64 limitOutSAMoneReadBytes;
        uint64 limitOutSJoneRead, limitOutSJcollapsed;
        uint64 limitBAMsortRAM;
        uint64 limitSjdbInsertNsj;
        uint64 limitNreadsSoft;

        // penalties
        intScore scoreGap, scoreGapNoncan, scoreGapGCAG, scoreGapATAC, scoreDelBase, scoreDelOpen, scoreInsBase, scoreInsOpen;
        intScore scoreStitchSJshift;//Max negative score when
        double scoreGenomicLengthLog2scale;

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
    void readSAMheader(const std::string readFilesCommandString, const std::vector<std::string> readFilesNames);
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
};
#endif  // Parameters.h
