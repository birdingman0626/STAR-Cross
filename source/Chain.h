#ifndef DEF_Chain
#define DEF_Chain

#include "IncludeDefine.h"
#include "Parameters.h"
#include "ErrorWarning.h"

class OneChain
{
    public:
        uint64 bN;
        std::string chr1,chr2;//1/2 (old/new) chr names
        std::vector <uint64> bStart1, bStart2, bLen; //blocks starts in 1/2, lengths
};

class Chain {
    public:
// //         uint64 bN;//number of blocks
// //         vector <uint64> bStart1, bStart2, bLen; //blocks starts in 1/2, lengths

        Chain(Parameters &Pin, std::string chainFileNameIn);
        void liftOverGTF(std::string gtfFileName, std::string outFileName);
    private:
        Parameters &P;
        std::string chainFileName;
        void chainLoad();
        std::map <std::string,OneChain> chrChains;
};

#endif