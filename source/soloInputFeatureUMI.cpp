#include "soloInputFeatureUMI.h"
#include "SoloReadFeature.h"
#include "binarySearch2.h"
#include "SoloTextIO.h"
using namespace std;
bool soloInputFeatureUMI(fstream *strIn, int32 featureType, bool readInfoYes, array<vector<uint64>,2> &sjAll, uint64 &iread, 
                            int32 &cbmatch, uint32 &feature, uint64 &umi, vector<uint32> &featVecU32, SoloReadFlagClass &readFlagCounts)
{
    if (!readNumber(*strIn, umi)) //end of file
        return false;

    if (readInfoYes) {
        readNumber(*strIn, iread); readNumber(*strIn, readFlagCounts.flag);
    };

    switch (featureType) {
        case SoloFeatureTypes::Gene :
        case SoloFeatureTypes::GeneFull :
        case SoloFeatureTypes::GeneFull_Ex50pAS :
        case SoloFeatureTypes::GeneFull_ExonOverIntron :
            readNumber(*strIn, feature);
            break;

        case SoloFeatureTypes::SJ : {
            uint64 sj[2]={0,0};
            readNumber(*strIn, sj[0]); readNumber(*strIn, sj[1]);
            feature=static_cast<uint32>(binarySearch2(sj[0],sj[1],sjAll[0].data(),sjAll[1].data(),sjAll[0].size())); //a miss (-1) becomes the (uint32)-1 "no feature" sentinel that callers skip
            break;
        };

        case SoloFeatureTypes::Transcript3p :
            feature=0;
            uint32 ntr=0, in1=0;
            readNumber(*strIn, ntr);
            featVecU32.resize(2*ntr);
            for (uint32 ii=0; ii<2*ntr; ii++) {
                readNumber(*strIn, in1);
                featVecU32[ii]=in1;
            };
            break;
        };

    readNumber(*strIn, cbmatch);

    return true;
};