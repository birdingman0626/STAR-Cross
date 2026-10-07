#ifndef CODE_Quantifications
#define CODE_Quantifications
#include "IncludeDefine.h"
#include <memory>
#include <array>

#define uintQ unsigned long

class Quantifications {
    public:
        struct {//counting reads per gene, similar to HTseq
            uint32  nGe;      //number of genes
            int nType; //number of count types (columns)
            uintQ cMulti;     //count multimappers
            uintQ *cAmbig, *cNone;//ambigouous, no-feature
            uintQ **gCount;     // array of read counts per gene for two strands
        } geneCounts;

    Quantifications (uint32 nGeIn);
    Quantifications(const Quantifications&) = delete;
    Quantifications& operator=(const Quantifications&) = delete;

    void addQuants(const Quantifications & quantsIn); //adds quantsIn to the quants
    private:
        std::array<uintQ,3> ambiguous{}, noFeature{};
        std::array<uintQ*,3> countViews{};
        std::array<std::unique_ptr<uintQ[]>,3> counts;
};

#endif
