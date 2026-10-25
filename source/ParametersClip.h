#ifndef CODE_ParametersClip
#define CODE_ParametersClip

#include "IncludeDefine.h"
#include "ClipMate.h"
#include "ClipCR4.h"
#include <array>
#include <vector>

class Parameters;

class ReadClipInput
{
public:
    std::vector <uint32> N;
    std::vector <uint32> NafterAd;
    std::vector <std::string> adSeq;
    std::vector <double> adMMp;
};

class ParametersClip
{//
public:
    //bool yes; //trimming is performed

    std::vector<std::string> adapterType;
    
    std::array<ReadClipInput,2> in;

    void initialize(Parameters *pPin);
    void initializeClipMates(std::vector<std::vector<ClipMate>> &clipMates);
        
private:
    Parameters *pP;
};

#endif
