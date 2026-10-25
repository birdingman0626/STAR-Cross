#ifndef CODE_Variation
#define CODE_Variation

#include "IncludeDefine.h"
#include "Parameters.h"
#include <array>

// struct SNPnt
// {
//     char ref;
//     char a1;
//     char a2;
// };

class SNP
{
public:
    uint32 N=0; //number of snps
    uint64* loci=nullptr; //snp coordinates (borrowed from Variation)
    std::vector<uint64> lociV; //snp coordinates vector
//     SNPnt* nt; //reference and alternative bases
//     char **nt; //reference and alternative bases
//     char *nt1; //1D array to store nt
    std::vector<std::array<char,3>> nt;//reference and alternative bases

    //methods
    void snpOnBlocks(uint64 blockStart, uint64 blockL, int blockShift, std::vector<std::vector<std::array<int,2>>> &snpV);
};

class Variation
{
public:
    //methods
    Variation (Parameters &Pin, std::vector <uint64> &chrStart, std::map <std::string,uint64> &chrNameIndex, bool yesVar); //create transcriptome structure, load and initialize parameters
    void loadVCF(std::string fileIn); //load VCF file
    std::vector<std::vector<std::array<int,2>>> sjdbSnp(uint64 sjStart, uint64 sjEnd, uint64 sjdbOverhang1); //calculates snp loci in sjdb sequences

    //variables
    bool yes;
    SNP snp;

    Parameters &P; //TODO: make this private

private:
    std::unique_ptr<uint64[]> snpLociStorage;
    std::string vcfFile;
    //string varOutFileName;
    //ofstream varOutStream;//output file for variations

    std::vector <uint64> &chrStart; //this needs to be replaced with a structure that contains basic genome variables
    std::map <std::string,uint64> &chrNameIndex;
};

#endif
