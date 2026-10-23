#ifndef CODE_ParametersChimeric
#define CODE_ParametersChimeric

#include "IncludeDefine.h"

class Parameters;

class ParametersChimeric
{//
    public:
        uint64 segmentMin, junctionOverhangMin; //min chimeric donor/acceptor length
        uint64 segmentReadGapMax; //max read gap for stitching chimeric windows
        int scoreMin,scoreDropMax,scoreSeparation, scoreJunctionNonGTAG; //min chimeric score
        uint64 mainSegmentMultNmax;

        uint64 multimapScoreRange, multimapNmax, nonchimScoreDropMin;
        bool legacyScoring; //use upstream 2.7.11b chimeric overlap/exon-selection algorithm

        vector<int> outJunctionFormat;

        struct
        {
            vector <string> stringIn;
            bool genomicN;
        } filter;

        struct
        {
            vector <string> type;
            bool bam;
            bool bamHardClip;
            bool samOld;
            bool junctions;
        } out;
        
        void initialize(Parameters *pPin);
        
    private:
        Parameters *pP;
};

#endif