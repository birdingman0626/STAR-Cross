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
        bool chimericDetectionMult(uint64 nWin, uint64 *readLength, int maxNonChimAlignScore, ReadAlign *PEunmergedRA);
        std::fstream *ostreamChimJunction;
};

#endif
