#ifndef H_ReadAnnotations
#define H_ReadAnnotations

#include "IncludeDefine.h"
#include "SoloCommon.h"
#include "SoloFeatureTypes.h"

class ReadAnnotFeature {//annotations for one feature
public:
    std::set<uint32> fSet;  //set of genes for this read
    std::vector<std::set<uint32>> fAlign; //gene for each alignment of this read
    uint32 ovType;
    enum overlapTypes {none, exonic, exonicAS, exonic50p, exonic50pAS, intronic, intronicAS, intergenic, N};

    void reset() {
        fSet={}; fAlign={}; ovType = 0;
    };
};

class ReadAnnotations {
public:
            
            //vector<int32> geneFull_Al, geneFull_Ex50pAS_Al, geneFull_ExonOverIntron_Al, geneConcordant_Al; //gene for each align

            std::array<ReadAnnotFeature, SoloFeatureTypes::N> annotFeatures;

            std::vector<std::array<uint32,2>> transcriptConcordant;
            std::vector<int32> geneExonOverlap;
            std::array<uint32,2> geneVelocytoSimple;//first element is gene, then counts of transcript types
            std::vector<trTypeStruct> trVelocytoType;//first element is gene, then counts of transcript types
            
            void reset () {
                transcriptConcordant = {};
                trVelocytoType = {};
                for (auto &raf : annotFeatures)
                    raf.reset();
            };
};

#endif


