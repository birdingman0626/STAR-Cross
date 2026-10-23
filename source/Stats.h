#ifndef STATS_DEF
#define STATS_DEF

#include "IncludeDefine.h"
#include "Transcript.h"
#include "Parameters.h"


class Stats {
    public:
        uint64 readN;//number of reads from the file
        uint64 readBases;//number of input bases
//         uint64 mateLmax[2], mateLmin[2];//mates' max and min length

        uint64 mappedReadsU, mappedReadsM;
        uint64 mappedBases, mappedMismatchesN, mappedInsN, mappedDelN, mappedInsL, mappedDelL;
        double mappedPortion; //portion of the read length that has been mapped

        uint64 splicesN[SJ_MOTIF_SIZE];//non-can,3*can,annotated
        uint64 splicesNsjdb;

        uint64 unmappedOther, unmappedShort, unmappedMismatch, unmappedMulti, unmappedAll;

        uint64 chimericAll;

        time_t timeStart, timeStartMap, timeFinishMap, timeLastReport, timeFinish;
        
        Stats ();
        void resetN();
        void printShort(std::ostream*);
        void transcriptStats(Transcript &T, uint64 Lread);
        void addStats(Stats &S);
        void progressReportHeader(std::ofstream &progressStream);
        void progressReport(std::ofstream &progressStream) ;
        void reportFinal(std::ofstream &streamOut);
        void writeLines(std::ofstream &streamOut, const std::vector<int> outType, const std::string commStr, const std::string outStr);// write commented lines to text files with stats
        
        void qualHistCalc(const uint64 imate, const char* qual, const uint64 len);
        //void qualHistCalcSolo(const uint64 imate, const char* qual, const vector<uint32> stlen);
};
#endif
