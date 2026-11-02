#include "ReadAlign.h"
#include "BAMfunctions.h"
#include "ChimericAlign.h"
using namespace std;
void ReadAlign::chimericDetectionOldOutput() {

    if (!chimRecord) {
        return;
    };

    //re-calculate the score for chimeric transcripts
    chimOld.tr[0].alignScore(Read1, mapGen.G, P);
    chimOld.tr[1].alignScore(Read1, mapGen.G, P);

    if (P.pCh.out.bam) //BAM output
        ChimericAlign::chimericBAMoutput(&chimOld.tr[0], &chimOld.tr[1], this, 0, 1, true, P);

    if (P.pCh.out.samOld) {

        chimOld.N=2; //this  is hard-coded for now
        if (chimOld.tr[0].exons[0][EX_iFrag]!=chimOld.tr[0].exons[chimOld.tr[0].nExons-1][EX_iFrag]) {//tr0 has both mates
            chimOld.tr[0].primaryFlag=true;//paired portion is primary
            chimOld.tr[1].primaryFlag=false;
        } else if (chimOld.tr[1].exons[0][EX_iFrag]!=chimOld.tr[1].exons[chimOld.tr[1].nExons-1][EX_iFrag]) {//tr1 has both mates
            chimOld.tr[1].primaryFlag=true;//paired portion is primary
            chimOld.tr[0].primaryFlag=false;
        } else if (chimOld.tr[0].exons[0][EX_iFrag]!=chimOld.tr[1].exons[0][EX_iFrag]) {//tr0 and tr1 are single different mates
            chimOld.tr[0].primaryFlag=true;
            chimOld.tr[1].primaryFlag=true;
        } else  {//two chimeric segments are on the same mate - this can only happen for single-end reads
            int chimRepresent = (chimOld.tr[0].maxScore > chimOld.tr[1].maxScore) ? 0 : 1;
            chimOld.tr[chimRepresent].primaryFlag=true;
            chimOld.tr[1-chimRepresent].primaryFlag=false;
        };

        for (uint64 iTr=0;iTr<chimOld.N;iTr++)
        {//write all chimeric pieces to Chimeric.out.sam/junction
            if (P.readNmates==2) {//PE: need mate info //not readNends: this is alignment
                uint64 iex=0;
                if ( chimOld.tr[1-iTr].exons[0][EX_iFrag] != chimOld.tr[1-iTr].exons[chimOld.tr[1-iTr].nExons-1][EX_iFrag] )
                {//the other segment has 2 mates, need to find the opposite mate
                    for (;iex<chimOld.tr[1-iTr].nExons;iex++) {
                        if (chimOld.tr[1-iTr].exons[iex][EX_iFrag]!=chimOld.tr[iTr].exons[0][EX_iFrag]) {
                            break;
                        };
                    };
                };

                uint64 mateChr=chimOld.tr[1-iTr].Chr;
                uint64 mateStart=chimOld.tr[1-iTr].exons[iex][EX_G];
                char mateStrand=(char) (chimOld.tr[1-iTr].Str!=chimOld.tr[1-iTr].exons[iex][EX_iFrag]);

                outputTranscriptSAM(chimOld.tr[iTr], chimOld.N, iTr, mateChr, mateStart, mateStrand, -1, NULL, &chunkOutChimSAM);
            } else
            {
                outputTranscriptSAM(chimOld.tr[iTr], chimOld.N, iTr, (uint64) -1, (uint64) -1, -1, -1, NULL, &chunkOutChimSAM);
            };
        };
    };

    if (P.pCh.out.junctions) {
        //junction + SAMp
        *chunkOutChimJunction << mapGen.chrName[chimOld.tr[0].Chr] <<"\t"<< chimOld.j0 - mapGen.chrStart[chimOld.tr[0].Chr]+1 <<"\t"<< (chimOld.tr[0].Str==0 ? "+":"-") \
                <<"\t"<< mapGen.chrName[chimOld.tr[1].Chr] <<"\t"<< chimOld.j1 - mapGen.chrStart[chimOld.tr[1].Chr]+1 <<"\t"<< (chimOld.tr[1].Str==0 ? "+":"-") \
                <<"\t"<< chimOld.motif <<"\t"<< chimOld.repeat0  <<"\t"<< chimOld.repeat1 <<"\t"<< readName+1 \
                <<"\t"<< chimOld.tr[0].exons[0][EX_G] - mapGen.chrStart[chimOld.tr[0].Chr]+1 <<"\t"<< outputTranscriptCIGARp(chimOld.tr[0]) \
                <<"\t"<< chimOld.tr[1].exons[0][EX_G] - mapGen.chrStart[chimOld.tr[1].Chr]+1 <<"\t"<<  outputTranscriptCIGARp(chimOld.tr[1]);
        if (P.outSAMattrPresent.RG)
            *chunkOutChimJunction <<"\t"<< P.outSAMattrRG.at(readFilesIndex);
        *chunkOutChimJunction <<"\n"; //<<"\t"<< chimOld.tr[0].exons[0][EX_iFrag]+1 --- no need for that, since chimOld.tr[0] is always on the first mate
    };

    return;
};
