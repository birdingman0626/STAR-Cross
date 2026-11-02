#ifndef H_SoloFeatureTypes
#define H_SoloFeatureTypes

namespace SoloFeatureTypes
{
    enum {SJ=0, Transcript3p=1, GeneFull=2, GeneFull_ExonOverIntron=3, GeneFull_Ex50pAS=4, Gene=5, VelocytoSimple=6, Velocyto=7, N=8};
    const static std::vector<std::string> Names={"SJ", "Transcript3p", "GeneFull", "GeneFull_ExonOverIntron", "GeneFull_Ex50pAS", "Gene", "VelocytoSimple", "Velocyto"};
};

#endif