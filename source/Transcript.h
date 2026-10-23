#ifndef CODE_Transcript
#define CODE_Transcript

#include "IncludeDefine.h"
#include "Parameters.h"
#include "Variation.h"
#include "Genome.h"
#include <set>

class Transcript {
public:
    uint64 exons[MAX_N_EXONS][EX_SIZE]; //coordinates of all exons: r-start, g-start, length
    std::vector <std::array<uint32,2>> cigar; //new way to record alignments, with CIGAR operations. For now, only used by splice-graph
    
    uint64 shiftSJ[MAX_N_EXONS][2]; //shift of the SJ coordinates due to genomic micro-repeats
    int canonSJ[MAX_N_EXONS]; //canonicity of each junction
    uint8 sjAnnot[MAX_N_EXONS]; //anotated or not
    uint8 sjStr[MAX_N_EXONS]; //strand of the junction
    uint64 intronMotifs[3];
    uint8 sjMotifStrand;
    bool sjYes=false;

    uint64 nExons; //number of exons in the read transcript

    //variables from ReadAlign
    uint64 *readLengthOriginal, *readLength;
    uint64 Lread, readLengthPairOriginal;
    uint64 iRead; //read identifier
    uint64 readNmates;
    char *readName;

    int iFrag; //frag number of the transcript, if the the transcript contains only one frag

    //loci
    uint64 rStart, roStart, rLength, gStart, gLength, cStart; //read, original read, and genomic start/length, chromosome start
    uint64 Chr,Str,roStr; //chromosome and strand and original read Strand
    uint32 haploType; //haplotype index for diploid genome
    
    bool primaryFlag;

    uint64 nMatch;//min number of matches
    uint64 nMM;//max number of mismatches
    uint64 mappedLength; //total mapped length, sum of lengths of all blocks(exons)

    uint64 extendL; //extension length
    intScore maxScore; //maximum Score

    uint64 nGap, lGap; //number of genomic gaps (>alignIntronMin) and their total length
    uint64 nDel; //number of genomic deletions (ie genomic gaps)
    uint64 nIns; //number of (ie read gaps)
    uint64 lDel; //total genomic deletion length
    uint64 lIns; //total genomic insertion length

    uint64 nUnique, nAnchor; //number of unique pieces in the alignment, number of anchor pieces in the alignment

    std::vector <int32> varInd;
    std::vector <int32> varGenCoord, varReadCoord ;
    std::vector <char> varAllele;
    
    //annotations
    std::set <uint32> alignGenes;

    Transcript(); //resets to 0
    void reset(); //reset to 0
    void resetMapG(); // reset map to 0
    void resetMapG(uint64); // reset map to 0 for Lread bases
    void add(Transcript*); // add
    intScore alignScore(char **Read1, char *G, Parameters &P);
    int variationAdjust(const Genome &mapGen, char *R);
    std::string generateCigarP(); //generates CIGAR
    void peOverlapSEtoPE(uint64* mSta, const Transcript &t);
    void extractSpliceJunctions(std::vector<std::array<uint64,2>> &sjOut, bool &annotYes);
    
    uint64 chrStartLengthExtended();
    
    bool transformGenome(Genome &genOut, Transcript & A);
    bool convertGenomeCigar(Genome &genOut, Transcript & A);

private:

};

#endif
