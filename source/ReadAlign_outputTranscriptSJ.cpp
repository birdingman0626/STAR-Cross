#include "ReadAlign.h"
#include "OutSJ.h"
using namespace std;
void ReadAlign::outputTranscriptSJ(Transcript const &trOut, uint64 nTrOut, OutSJ *outSJ, uint64 sjReadStartN ) {//record junctions in outSJ array

    //TODO: make sure that a junction is recorded onyl once from one read.
    //For a multimapper, several alignments may contain the same junctions - now it's recorded several time.
//     if (nTrOut>1) return; //junctions from multi-mappers are not recorded

//     if (P.outSAMmode=="None") return; //no SAM output
    if (trOut.nExons==0)
        return;

    for (uint64 iex=0;iex<trOut.nExons-1;iex++) {//record all junctions
        if (trOut.canonSJ[iex]>=0) {//only record junctions, not indels or mate gap
            outSJ->oneSJ.junctionPointer(outSJ->data, outSJ->N);//get pointer to an empty junction in the data array
            *outSJ->oneSJ.start=trOut.exons[iex][EX_G]+trOut.exons[iex][EX_L]; //start of the intron
            *outSJ->oneSJ.gap=trOut.exons[iex+1][EX_G]-*outSJ->oneSJ.start;
            //overhangs: min method
            *outSJ->oneSJ.overhangLeft = min ( (uint32) trOut.exons[iex][EX_L],(uint32) trOut.exons[iex+1][EX_L] );
            *outSJ->oneSJ.overhangRight = *outSJ->oneSJ.overhangLeft;

            //check if this junction has been recorded from this read - this happens when the mates overlap and cross the same junctions
            bool duplicateSJ(false);
            for (uint64 ii=sjReadStartN; ii<outSJ->N; ii++) {//TODO if there are many junctions, need to make more efficient
                if ( *outSJ->oneSJ.start == loadUnaligned<uint64>(outSJ->data+ii*Junction::dataSize+Junction::startP) \
                  && *outSJ->oneSJ.gap   == loadUnaligned<uint32>(outSJ->data+ii*Junction::dataSize+Junction::gapP) ) {
                    duplicateSJ=true;
                    UnalignedPointer<uint16> overhang1=(uint16*) (outSJ->data+ii*Junction::dataSize+Junction::overhangLeftP);
                    if (*overhang1<*outSJ->oneSJ.overhangLeft) {
                        *overhang1=*outSJ->oneSJ.overhangLeft;
                        storeUnaligned(outSJ->data+ii*Junction::dataSize+Junction::overhangRightP, static_cast<uint16>(*overhang1));
                    };
                    break;
                };
            };
            if (duplicateSJ) continue; //do not record this junctions

            *outSJ->oneSJ.motif=trOut.canonSJ[iex];
            *outSJ->oneSJ.strand=(char) (trOut.canonSJ[iex]==0 ? 0 : (trOut.canonSJ[iex]+1)%2+1);
            *outSJ->oneSJ.annot=trOut.sjAnnot[iex];
            if (nTrOut==1) {
                *outSJ->oneSJ.countUnique=1;
                *outSJ->oneSJ.countMultiple=0;
            } else {
                *outSJ->oneSJ.countMultiple=1; //TODO: 1/nTrOut?
                *outSJ->oneSJ.countUnique=0; //TODO: 1/nTrOut?
            };

            outSJ->N++;//increment the number of recorded junctions
        };
    };
};
