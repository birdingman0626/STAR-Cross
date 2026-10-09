#ifndef H_Genome
#define H_Genome

#include "IncludeDefine.h"
#include "Parameters.h"
#include "PackedArray.h"
#include "SeedIndexView.h"
#include "SharedMemory.h"
#include "Variation.h"
#include "SuperTranscriptome.h"

class GTF;

class Genome {
private:
    key_t shmKey;
    char *shmStart;
    uint OpenStream(string name, ifstream & stream, uint size);
    void HandleSharedMemoryException(const SharedMemoryException & exc, uint64 shmSize);
    std::unique_ptr<char[]> sequenceStorage;
    std::unique_ptr<SharedMemory> sharedMemoryStorage;
    // Insertion snapshots can still borrow the old bin table after a refill.
    std::vector<std::unique_ptr<uint[]>> chromosomeBinStorage;
    std::vector<std::unique_ptr<uint[]>> suffixIndexStartStorage;
    struct JunctionStorage {
        std::unique_ptr<uint[]> donor, acceptor, start, end;
        std::unique_ptr<uint8[]> motif, shiftLeft, shiftRight, strand;
    };
    std::vector<std::unique_ptr<JunctionStorage>> junctionStorage;
    std::unique_ptr<Variation> variationStorage;
    std::unique_ptr<SuperTranscriptome> superTranscriptomeStorage;
    std::unique_ptr<Genome> outputGenomeStorage;
public:
    operator SeedIndexView() const {
        return {G,SA,nGenome,GstrandBit,GstrandMask,nSA};
    }
    Parameters &P;
    ParametersGenome &pGe;
    SharedMemory *sharedMemory;

    enum {exT,exS,exE,exG,exL}; //indexes in the exonLoci array from GTF
     
    char *G=nullptr, *G1=nullptr;
    uint64 nGenome=0, nG1alloc=0;
    PackedArray SA,SAinsert,SApass1,SApass2;
    PackedArray SAi;
    Variation *Var=nullptr;

    uint nGenomeInsert=0, nGenomePass1=0, nGenomePass2=0, nSAinsert=0, nSApass1=0, nSApass2=0;


    //chr parameters
    vector <uint64> chrStart, chrLength, chrLengthAll;
    uint genomeChrBinNbases=0, chrBinN=0, *chrBin=nullptr;
    vector <string> chrName, chrNameAll;
    map <string,uint64> chrNameIndex;

    uint *genomeSAindexStart=nullptr;//starts of the L-mer indices in the SAindex, 1<=L<=pGe.gSAindexNbases

    uint nSA=0, nSAbyte=0, nChrReal=0;//genome length, SA length, # of chromosomes, vector of chromosome start loci
    uint nGenome2=0, nSA2=0, nSAbyte2=0, nChrReal2=0; //same for the 2nd pass
    uint nSAi=0; //size of the SAindex
    unsigned char GstrandBit=0, SAiMarkNbit=0, SAiMarkAbsentBit=0; //SA index bit for strand information
    uint GstrandMask=0, SAiMarkAbsentMask=0, SAiMarkAbsentMaskC=0, SAiMarkNmask=0, SAiMarkNmaskC=0;//maske to remove strand bit from SA index, to remove mark from SAi index

    //SJ database parameters
    uint sjdbOverhang, sjdbLength; //length of the donor/acceptor, length of the sj "chromosome" =2*pGe.sjdbOverhang+1 including spacer
    uint sjChrStart=0,sjdbN=0; //first sj-db chr
    uint sjGstart=0; //start of the sj-db genome sequence
    uint *sjDstart=nullptr,*sjAstart=nullptr,*sjStr=nullptr, *sjdbStart=nullptr, *sjdbEnd=nullptr; //sjdb loci
    uint8 *sjdbMotif=nullptr; //motifs of annotated junctions
    uint8 *sjdbShiftLeft=nullptr, *sjdbShiftRight=nullptr; //shifts of junctions
    uint8 *sjdbStrand=nullptr; //junctions strand, not used yet

   //sequence insert parameters
    uint genomeInsertL=0; //total length of the sequence to be inserted on the fly
    uint genomeInsertChrIndFirst=0; //index of the first inserted chromosome

    //SuperTranscriptome genome
    SuperTranscriptome *superTr=nullptr;

    Genome (Parameters &P, ParametersGenome &pGe);
    // A snapshot borrows allocations; it must not outlive or release its source.
    enum class Snapshot { Borrowed };
    Genome(const Genome& source, Snapshot);
    Genome(const Genome&) = delete;
    Genome& operator=(const Genome&) = delete;
    ~Genome() = default; // owned storage only; never logs through Parameters
    void releaseSharedMemory(); // call before destroying Parameters' log streams

    void freeMemory();
    void genomeLoad();
    void genomeOutLoad();
    void chrBinFill();
    void initializeVariation(bool enabled);
    void allocateSAindexStarts(uint count);
    void allocateJunctionAnnotations(uint count);
    void allocateJunctionCoordinates(uint count); // after annotations, same junction set
    void chrInfoLoad();
    void genomeSequenceAllocate(uint64 nGenomeIn, uint64 &nG1allocOut, char*& Gout, char*& G1out);
    void loadSJDB(string &genDir);

    void insertSequences();

    //void consensusSequence(); DEPRECATED
    
    void genomeGenerate();
    void writeChrInfo(const string dirOut);
    void concatenateChromosomes(const vector<vector<uint8>> &vecSeq, const vector<string> &vecName, const uint64 padBin);
    void writeGenomeSequence(const string dirOut);
    
    //transform genome coordinates
    struct {
        bool convYes;
        bool gapsAreJunctions;
        Genome *g;
        string convFile;
        vector<array<uint64,3>> convBlocks;
        uint64 nMinusStrandOffset;//offset for the (-) strand, typically=nGenomeReal
    } genomeOut{};
    
    typedef struct {
        uint64 pos;
        int32 len;//0: SNV, <0: deletion; >0: insertion
        array<string,2> seq;//sequence for SNV and insertions, empty for deletions
    } VariantInfo;
    
    void transformGenome(GTF *gtf) ;
    void transformChrLenStart(map<string,vector<VariantInfo>> &vcfVariants, vector<uint64> &chrStart1, vector<uint64> &chrLength1);
    void transformGandBlocks(map<string,vector<VariantInfo>> &vcfVariants, vector<uint64> &chrStart1, vector<uint64> &chrLength1, vector<array<uint64,3>> &transformBlocks, char *Gnew);
    void transformBlocksWrite(vector<array<uint64,3>> &transformBlocks);
    void transformExonLoci(vector<array<uint64,exL>> &exonLoci, vector<array<uint64,3>> &transformBlocks);
};
#endif
