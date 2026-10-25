#ifndef CODE_ChimericDetection
#define CODE_ChimericDetection

#include "IncludeDefine.h"
#include "Parameters.h"
#include "Transcript.h"
#include "ChimericAlign.h"
#include "Genome.h"

class ReadAlign;

class ChimericDetection {
    private:
        Parameters &P;
        ReadAlign *RA;
        Transcript ***trAll;
        uint64 nW, *nWinTr;
        char** Read1;
        Genome &outGen;

    public:
        std::vector <ChimericAlign> chimAligns;

        ChimericDetection(Parameters &Pin, Transcript ***trAll, uint64 *nWinTr, char** Read1in, Genome &genomeIn, std::fstream *ostreamChimJunctionIn, ReadAlign *RA);
        ~ChimericDetection() { releaseChimAligns(); } // the last read's stitched copies
        ChimericDetection(const ChimericDetection&) = delete; // chimAligns own raw copies
        ChimericDetection& operator=(const ChimericDetection&) = delete;
        void releaseChimAligns(); // frees al1/al2 copied by chimericStitching, clears chimAligns
        bool chimericDetectionMult(uint64 nWin, uint64 *readLength, int maxNonChimAlignScore, ReadAlign *PEunmergedRA);
        std::fstream *ostreamChimJunction;
};

#endif
