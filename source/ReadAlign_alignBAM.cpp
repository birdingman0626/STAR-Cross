#include "ReadAlign.h"
#include "SequenceFuns.h"
#include "ErrorWarning.h"
#include "IncludeDefine.h"
# include "BAMfunctions.h"

#include <type_traits>
using namespace std;
void ReadAlign::samAttrNM_MD (Transcript const &trOut, uint64 iEx1, uint64 iEx2, uint64 &tagNM, string &tagMD) {
    tagNM=0;
    tagMD="";
    char* R=Read1[trOut.roStr==0 ? 0:2];
    uint64 matchN=0;
    uint32 nMM=0, nI=0, nD=0;
    for (uint64 iex=iEx1;iex<=iEx2;iex++) {
        for (uint64 ii=0;ii<trOut.exons[iex][EX_L];ii++) {
            char r1=R[ii+trOut.exons[iex][EX_R]];
            char g1=genOut.G[ii+trOut.exons[iex][EX_G]];
            if ( r1!=g1 || r1==4 || g1==4) {
                ++nMM;
                tagMD+=to_string(matchN);
                tagMD+=P.genomeNumToNT[(uint8) g1];
                matchN=0;
            } else {
                matchN++;
            };
        };
        if (iex<iEx2) {
            if (trOut.canonSJ[iex] < 0) {//indels, junction are not included in edit distance
                nD+=trOut.exons[iex+1][EX_G]-(trOut.exons[iex][EX_G]+trOut.exons[iex][EX_L]);//deletion
            };
            nI+=trOut.exons[iex+1][EX_R]-trOut.exons[iex][EX_R]-trOut.exons[iex][EX_L];//insertion
            if (trOut.canonSJ[iex]==-1) {//deletion. TODO: This does not work if there is both deletion and insertion!!!
                tagMD+=to_string(matchN) + "^";
                for (uint64 ii=trOut.exons[iex][EX_G]+trOut.exons[iex][EX_L];ii<trOut.exons[iex+1][EX_G];ii++) {
                    tagMD+=P.genomeNumToNT[(uint8) genOut.G[ii]];
                };
                matchN=0;
            };
        };
    };
    tagMD+=to_string(matchN);
    tagNM=nMM+nI+nD;
};

// Values shared by all mates of one alignBAM call. The mate fields can be updated
// by an unmapped mate whose partner is mapped; later mates see the update.
struct ReadAlign::BamRecordContext {
    Transcript const &trOut;
    uint64 nTrOut, iTrOut;
    uint64 trChrStart, mateChr, mateStart;
    char mateStrand;
    int alignType;
    bool *mateMap;
    bool flagPaired;
    uint64 nMates;
    uint64 iExMate; //last exon of the first mate
    uint64 leftMate; //the mate (0 or 1) which is on the left
    uint64 tLen, leftMostMate;
    int32 gxgnGene; //gene to output in GX/GN
};

// One mate's record, built step by step.
struct ReadAlign::BamMateRecord {
    uint64 iEx1=0, iEx2=0, Mate=0, Str=0;
    uint16 samFLAG=0;
    uint32 packedCIGAR[BAM_CIGAR_MaxSize];
    uint32 nCIGAR=0; //number of CIGAR operations
    int MAPQ=0;
    uint32 attrN=0;
    char attrOutArray[BAM_ATTR_MaxSize];
    uint64 trimL1=0, trimR1=0;
    vector<int32> SJintron;
    vector<char> SJmotif;
};

int ReadAlign::alignBAM(Transcript const &trOut, uint64 nTrOut, uint64 iTrOut, uint64 trChrStart, uint64 mateChr, uint64 mateStart, char mateStrand, int alignType, bool *mateMap, const vector<int> &outSAMattrOrder, char** outBAMarrayOut, uint64* outBAMarrayN) {
    //return: number of lines (mates)

    //alignType>=0: unmapped reads
    //          -1: normal mapped reads
    //          -10: chimeric alignment, not supplemental (like -11,-12,-13)
    //          -11: chimeric alignment, supplemental, hard-clipping, chimeric junction on the left
    //          -12: chimeric alignment, supplemental, hard-clipping, chimeric junction on the right
    //          -13: chimeric alignment, supplemental, soft-clipping

    if (P.outSAMmode=="None") return 0; //no SAM/BAM output

    outBAMarrayN[0]=0;
    outBAMarrayN[1]=0;

    BamRecordContext c{trOut, nTrOut, iTrOut, trChrStart, mateChr, mateStart, mateStrand, alignType, mateMap,
                       P.readNmates==2, //not readNends: this is about alignments
                       1, 0, 0, 0, 0, -1};

    //for SAM output need to split mates
    if (alignType<0) {//mapped reads: SAM
        for (c.iExMate=0;c.iExMate<trOut.nExons-1;c.iExMate++) {
            if (trOut.canonSJ[c.iExMate]==-3){
                c.nMates=2;
                break;
            };
        };
    } else {
        c.nMates=0;
    };

    if (c.nMates>1 && P.outSAMtlen==2) {
        c.tLen=max(trOut.exons[trOut.nExons-1][EX_G]+trOut.exons[trOut.nExons-1][EX_L],trOut.exons[c.iExMate][EX_G]+trOut.exons[c.iExMate][EX_L])-min(trOut.exons[0][EX_G],trOut.exons[c.iExMate+1][EX_G]);
        c.leftMostMate=(trOut.exons[0][EX_G]<=trOut.exons[c.iExMate+1][EX_G] ? 0 : 1);
    };

    if (c.flagPaired) c.leftMate=trOut.Str;

    if (P.outSAMattrPresent.MC) {
        calcCIGAR(trOut, c.nMates, c.iExMate, c.leftMate);
    };

    if (P.outSAMattrPresent.GX || P.outSAMattrPresent.GN) {
        auto annFeat = readAnnot.annotFeatures[P.pSolo.samAttrFeature];
        if (annFeat.fSet.size()==1 && iTrOut < annFeat.fAlign.size() && annFeat.fAlign[iTrOut].size()==1) {
            //2nd condition needed when this function is called to output transcriptomic alignments, where GX/GN are not allowed, and iTrOut can be large
            c.gxgnGene = *annFeat.fAlign[iTrOut].begin();
        };
    };

    for (uint64 imate=0;imate < (alignType<0 ? c.nMates:P.readNmates);imate++) { //not readNends: this is about alignments
        BamMateRecord m;
        if (alignType>=0) {//this mate is unmapped
            if (mateMap!=NULL && mateMap[imate]) continue; //this mate was mapped, do not record it as unmapped
            bamUnmappedMate(c, m, imate);
        } else {//this mate is mapped
            bamMappedMate(c, m, imate);
            bamMappedAttributes(c, m, imate, outSAMattrOrder);
        };

        if (P.readFiles.typeN==10 && !P.readFiles.samAttrKeepNone) {
            if (m.attrN > BAM_ATTR_MaxSize)
                exitWithError("EXITING: BAM attributes exceed buffer capacity\n", std::cerr, P.inOut->logMain, EXIT_CODE_INPUT_FILES, P);
            m.attrN+=bamAttrArrayWriteSAMtags(readNameExtra[m.Mate], m.attrOutArray+m.attrN, BAM_ATTR_MaxSize-m.attrN, P);
        };
        outBAMarrayN[imate]=bamWriteRecord(c, m, imate, outBAMarrayOut[imate]);
    };

    return ( outBAMarrayN[1]==0 ? 1 : 2);
};

void ReadAlign::bamUnmappedMate(BamRecordContext &c, BamMateRecord &m, uint64 imate)
{// flag and attributes of an unmapped mate; a mapped partner sets the mate fields
    Transcript const &trOut=c.trOut;
    m.samFLAG=0x4;
    if (P.readNmates==2) {//paired read, //not readNends: this is about alignments
        m.samFLAG|=0x1 + (imate==0 ? 0x40 : 0x80);
        if (c.mateMap[1-imate]) {//mate mapped
            if (trOut.Str!=(1-imate)) m.samFLAG|=0x20; //mate strand reverted
            c.mateChr=trOut.Chr;
            c.trChrStart=genOut.chrStart[c.mateChr];
            c.mateStart=trOut.exons[0][EX_G] - c.trChrStart;
            c.mateStrand= trOut.Str == (1-imate) ? 0 : 1;

            if (!trOut.primaryFlag && P.outSAMunmapped.keepPairs) {//mapped mate is not primary, keep unmapped mate for each pair, hence need to mark some as not primary
                m.samFLAG|=0x100;
            };
        } else {//mate unmapped
            m.samFLAG|=0x8;
        };
    };

    if (readFilter=='Y') m.samFLAG|=0x200; //not passing quality control

    m.Mate=imate;
    m.Str=m.Mate;

    char *a=m.attrOutArray;
    uint32 &n=m.attrN;
    n=0;
    n+=bamAttrArrayWriteInt(0,"NH",a+n,P);
    n+=bamAttrArrayWriteInt(0,"HI",a+n,P);
    n+=bamAttrArrayWriteInt(trOut.maxScore,"AS",a+n,P);
    n+=bamAttrArrayWriteInt(trOut.nMM,"nM",a+n,P);
    n+=bamAttrArrayWrite((to_string((uint64) c.alignType)).at(0), "uT",a+n); //cast to uint64 is only necessary for old compilers

    if (P.outSAMattrPresent.RG)
        n+=bamAttrArrayWrite(P.outSAMattrRG.at(readFilesIndex),"RG",a+n);
    if (P.outSAMattrPresent.CR)
        n+=bamAttrArrayWrite(soloRead->readBar->cbSeq,"CR",a+n);
    if (P.outSAMattrPresent.CY)
        n+=bamAttrArrayWrite(soloRead->readBar->cbQual,"CY",a+n);
    if (P.outSAMattrPresent.CB && soloRead->readBar->cbSeqCorrected!="")
        n+=bamAttrArrayWrite(soloRead->readBar->cbSeqCorrected,"CB",a+n);
    if (P.outSAMattrPresent.UR)
        n+=bamAttrArrayWrite(soloRead->readBar->umiSeq,"UR",a+n);
    if (P.outSAMattrPresent.UY)
        n+=bamAttrArrayWrite(soloRead->readBar->umiQual,"UY",a+n);
    if (P.outSAMattrPresent.sM)
        n+=bamAttrArrayWrite(soloRead->readBar->cbMatch,"sM",a+n);
    if (P.outSAMattrPresent.sS)
        n+=bamAttrArrayWrite(soloRead->readBar->bSeq,"sS",a+n);
    if (P.outSAMattrPresent.sQ)
        n+=bamAttrArrayWrite(soloRead->readBar->bQual,"sQ",a+n);
    if (P.outSAMattrPresent.cN) {
        vector <int32> v1={(int32)clipMates[imate][0].clippedN, (int32)clipMates[imate][1].clippedN};
        n+=bamAttrArrayWrite(v1,"cN",a+n);
    };
}

void ReadAlign::bamMappedMate(BamRecordContext &c, BamMateRecord &m, uint64 imate)
{// flag, CIGAR, junction tags and MAPQ of a mapped mate
    Transcript const &trOut=c.trOut;
    if (c.flagPaired) {//paired reads
        m.samFLAG=0x0001;
        if (c.iExMate==trOut.nExons-1) {//single mate
            if (c.mateChr>genOut.nChrReal) m.samFLAG|=0x0008; //not mapped as pair
        } else {//properly paired
            m.samFLAG|=0x0002; //mapped as pair
        };
    } else {//single end
        m.samFLAG=0;
    };

    if (readFilter=='Y') m.samFLAG|=0x200; //not passing quality control

    if (c.alignType==-11 || c.alignType==-12 || c.alignType==-13) m.samFLAG|=0x800; //mark chimeric alignments
    if (!trOut.primaryFlag) m.samFLAG|=0x100; //mark not primary align

    m.iEx1 = (imate==0 ? 0 : c.iExMate+1);
    m.iEx2 = (imate==0 ? c.iExMate : trOut.nExons-1);
    m.Mate=trOut.exons[m.iEx1][EX_iFrag];
    m.Str= trOut.Str;//note that Strand = the mate on the left

    if (m.Mate==0) {
        m.samFLAG|= m.Str*0x10;
        if (c.nMates==2) m.samFLAG|= (1-m.Str)*0x20;
    } else {//second mate strand need to be reverted
        m.samFLAG|= (1-m.Str)*0x10;
        if (c.nMates==2) m.samFLAG|= m.Str*0x20;
    };

    if (c.flagPaired) {
        m.samFLAG|= (m.Mate==0 ? 0x0040 : 0x0080);
        if (c.nMates==1 && c.mateStrand==1) m.samFLAG|=0x20;//revert strand using inout value of mateStrand (e.g. for chimeric aligns)
    };

    const uint64 trimL=leftClip(m.Mate, m.Str);
    const uint64 leftMate=c.leftMate;

    m.nCIGAR=0;
    m.trimL1 = trimL + trOut.exons[m.iEx1][EX_R] - (trOut.exons[m.iEx1][EX_R]<readLength[leftMate] ? 0 : readLength[leftMate]+1);
    if (m.trimL1>0) {
        m.packedCIGAR[m.nCIGAR++]=m.trimL1<<BAM_CIGAR_OperationShift | (c.alignType==-11 ? BAM_CIGAR_H : BAM_CIGAR_S);
    };

    for (uint64 ii=m.iEx1;ii<=m.iEx2;ii++) {
        if (ii>m.iEx1) {//record gaps
            uint64 gapG=trOut.exons[ii][EX_G]-(trOut.exons[ii-1][EX_G]+trOut.exons[ii-1][EX_L]);
            uint64 gapR=trOut.exons[ii][EX_R]-trOut.exons[ii-1][EX_R]-trOut.exons[ii-1][EX_L];
            //it's possible to have a D or N and I at the same time
            if (gapR>0){
                m.packedCIGAR[m.nCIGAR++]=gapR<<BAM_CIGAR_OperationShift | BAM_CIGAR_I;
            };
            if (trOut.canonSJ[ii-1]>=0 || trOut.sjAnnot[ii-1]==1) {//junction: N
                m.packedCIGAR[m.nCIGAR++]=gapG<<BAM_CIGAR_OperationShift | BAM_CIGAR_N;
                m.SJmotif.push_back(trOut.canonSJ[ii-1] + (trOut.sjAnnot[ii-1]==0 ? 0 : SJ_SAM_AnnotatedMotifShift)); //record junction type
                m.SJintron.push_back((int32) (trOut.exons[ii-1][EX_G] + trOut.exons[ii-1][EX_L] + 1 - c.trChrStart) );//record intron start
                m.SJintron.push_back((int32) (trOut.exons[ii][EX_G] - c.trChrStart)); //record intron end
            } else if (gapG>0) {//deletion: D
                m.packedCIGAR[m.nCIGAR++]=gapG<<BAM_CIGAR_OperationShift | BAM_CIGAR_D;
            };
        };
        if (trOut.exons[ii][EX_L]>0) //do not record 0-length blocks (which could appear near junctions followed by deletions)
            m.packedCIGAR[m.nCIGAR++]=trOut.exons[ii][EX_L]<<BAM_CIGAR_OperationShift | BAM_CIGAR_M;
    };

    if (m.SJmotif.size()==0) {//no junctions recorded, mark with -1
        m.SJmotif.push_back(-1);
        m.SJintron.push_back(-1);
    };

    m.trimR1=(trOut.exons[m.iEx1][EX_R]<readLength[leftMate] ?
        readLengthOriginal[leftMate] : readLength[leftMate]+1+readLengthOriginal[m.Mate])
        - trOut.exons[m.iEx2][EX_R]-trOut.exons[m.iEx2][EX_L] - trimL;
    if ( m.trimR1 > 0 ) {
        m.packedCIGAR[m.nCIGAR++]=m.trimR1<<BAM_CIGAR_OperationShift | (c.alignType==-12 ? BAM_CIGAR_H : BAM_CIGAR_S);
    };

    m.MAPQ=P.output.uniqueMapq;
    if (c.nTrOut>=5) {
        m.MAPQ=0;
    } else if (c.nTrOut>=3) {
        m.MAPQ=1;
    } else if (c.nTrOut==2) {
        m.MAPQ=3;
    };
}

namespace {
// ";"-joined gene IDs or names of one alignment's features, "-" if none
template <class Names, class Genes>
string joinGenes(const Names &names, const Genes &genes)
{
    string algenes;
    for (auto &gg: genes)
        algenes += names[gg] + ";";
    if (algenes.size()>0) {
        algenes.pop_back();
    } else {
        algenes = "-";
    };
    return algenes;
}
} // namespace

void ReadAlign::bamMappedAttributes(BamRecordContext &c, BamMateRecord &m, uint64 imate, const vector<int> &outSAMattrOrder)
{// SAM attributes of a mapped mate, in the requested order
    Transcript const &trOut=c.trOut;
    char *a=m.attrOutArray;
    uint32 &n=m.attrN;
    uint64 tagNM=(uint64) -1;
    string tagMD("");

    n=0;
    for (uint64 ii=0;ii<outSAMattrOrder.size();ii++) {
        switch (outSAMattrOrder[ii]) {
            case ATTR_NH:
                n+=bamAttrArrayWriteInt(c.nTrOut,"NH",a+n,P);
                break;
            case ATTR_HI:
                n+=bamAttrArrayWriteInt(c.iTrOut+P.outSAMattrIHstart,"HI",a+n,P);
                break;
            case ATTR_AS:
                n+=bamAttrArrayWriteInt(trOut.maxScore,"AS",a+n,P);
                break;
            case ATTR_nM:
                n+=bamAttrArrayWriteInt(trOut.nMM,"nM",a+n,P);
                break;
            case ATTR_jM:
                n+=bamAttrArrayWrite(m.SJmotif,"jM",a+n);
                break;
            case ATTR_jI:
                n+=bamAttrArrayWrite(m.SJintron,"jI",a+n);
                break;
            case ATTR_XS:
                if (trOut.sjMotifStrand==1) {
                    n+=bamAttrArrayWrite('+',"XS",a+n);
                } else if (trOut.sjMotifStrand==2) {
                    n+=bamAttrArrayWrite('-',"XS",a+n);
                };
                break;
            case ATTR_cN:
                {
                    vector <int32> v1={(int32)clipMates[imate][0].clippedN, (int32)clipMates[imate][1].clippedN};
                    n+=bamAttrArrayWrite(v1,"cN",a+n);
                    break;
                };
            case ATTR_NM:
                if ( tagNM == (uint64) -1 ) samAttrNM_MD (trOut, m.iEx1, m.iEx2, tagNM, tagMD);
                n+=bamAttrArrayWriteInt(tagNM,"NM",a+n,P);
                break;
            case ATTR_MD:
                if ( tagMD.size()==0 ) samAttrNM_MD (trOut, m.iEx1, m.iEx2, tagNM, tagMD);
                n+=bamAttrArrayWrite(tagMD,"MD",a+n);
                break;
            case ATTR_RG:
                n+=bamAttrArrayWrite(P.outSAMattrRG.at(readFilesIndex),"RG",a+n);
                break;
            case ATTR_rB:
                {
                    vector <int32> rb;
                    for (uint64 ie=m.iEx1;ie<=m.iEx2;ie++) {
                        rb.push_back( (int32) trOut.exons[ie][EX_R]+1 );
                        rb.push_back( (int32) trOut.exons[ie][EX_R]+trOut.exons[ie][EX_L]);
                        rb.push_back( (int32) (trOut.exons[ie][EX_G]-genOut.chrStart[trOut.Chr]+1) );
                        rb.push_back( (int32) (trOut.exons[ie][EX_G]-genOut.chrStart[trOut.Chr]+trOut.exons[ie][EX_L]) );
                    };
                    n+=bamAttrArrayWrite(rb,"rB",a+n);
                };
                break;
            case ATTR_vG:
                if (trOut.varGenCoord.size()>0)
                    n+=bamAttrArrayWrite(trOut.varGenCoord,"vG",a+n);
                break;
            case ATTR_vA:
                if (trOut.varAllele.size()>0)
                    n+=bamAttrArrayWrite(trOut.varAllele,"vA",a+n);
                break;
            case ATTR_vW:
                if (waspType!=-1)
                    n+=bamAttrArrayWrite( (int32) waspType, "vW", a+n );
                break;
            case ATTR_ha:
                if (mapGen.pGe.transform.type==2) {
                    n+=bamAttrArrayWrite( (int32) trOut.haploType, "ha", a+n );
                };
                break;
            case ATTR_ch:
                if (c.alignType<=-10) {//chimeric alignment
                    n+=bamAttrArrayWrite('1',"ch",a+n);
                };
                break;
            case ATTR_MC:
                if (c.nMates>1) {//mate CIGAR
                    n+=bamAttrArrayWrite(matesCIGAR[1-imate],"MC",a+n);
                };
                break;

            //solo
            case ATTR_CR:
                n+=bamAttrArrayWrite(soloRead->readBar->cbSeq,"CR",a+n);
                break;
            case ATTR_CY:
                n+=bamAttrArrayWrite(soloRead->readBar->cbQual,"CY",a+n);
                break;
            case ATTR_UR:
                n+=bamAttrArrayWrite(soloRead->readBar->umiSeq,"UR",a+n);
                break;
            case ATTR_UY:
                n+=bamAttrArrayWrite(soloRead->readBar->umiQual,"UY",a+n);
                break;
            case ATTR_GX:
                n+=bamAttrArrayWrite(c.gxgnGene<0 ? string("-") : chunkTr->geID[c.gxgnGene], "GX",a+n);
                break;
            case ATTR_GN:
                n+=bamAttrArrayWrite(c.gxgnGene<0 ? string("-") : chunkTr->geName[c.gxgnGene], "GN",a+n);
                break;
            case ATTR_gx:
                n+=bamAttrArrayWrite(joinGenes(chunkTr->geID, readAnnot.annotFeatures[P.pSolo.samAttrFeature].fAlign[c.iTrOut]),"gx",a+n);
                break;
            case ATTR_gn:
                n+=bamAttrArrayWrite(joinGenes(chunkTr->geName, readAnnot.annotFeatures[P.pSolo.samAttrFeature].fAlign[c.iTrOut]),"gn",a+n);
                break;
            case ATTR_sF:
                {
                    const auto &feat=readAnnot.annotFeatures[P.pSolo.samAttrFeature];
                    vector<int32_t> sf(2);
                    sf[0] = feat.ovType;
                    sf[1] = feat.fSet.size();
                    if ( (sf[0]==1 || sf[0]==3 || sf[0]==5) && feat.fAlign[c.iTrOut].size()==0) {
                        sf={-1,-1};
                    };
                    n += bamAttrArrayWrite(sf,"sF",a+n);
                };
                break;
            case ATTR_sM:
                n+=bamAttrArrayWrite(soloRead->readBar->cbMatch,"sM",a+n);
                break;
            case ATTR_sS:
                n+=bamAttrArrayWrite(soloRead->readBar->bSeq,"sS",a+n);
                break;
            case ATTR_sQ:
                n+=bamAttrArrayWrite(soloRead->readBar->bQual,"sQ",a+n);
                break;
            case ATTR_CB:
                if (soloRead->readBar->cbSeqCorrected!="") //only if corrected CB is defined (e.g. CB_samTagOut), otherwise it will be added at the BAM sorting stage
                    n+=bamAttrArrayWrite(soloRead->readBar->cbSeqCorrected,"CB",a+n);
                break;
            case ATTR_UB: //will be added at the BAM sorting stage
                break;

            default:
                ostringstream errOut;
                errOut <<"EXITING because of FATAL BUG: unknown/unimplemented SAM/BAM atrribute (tag): "<<outSAMattrOrder[ii] <<"\n";
                errOut <<"SOLUTION: please make sure that SAM attributes in --outSAMattributes are compatible with the --outSAMtype option\n";
                exitWithError(errOut.str(), std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
        };
    };
}

uint64 ReadAlign::bamWriteRecord(const BamRecordContext &c, BamMateRecord &m, uint64 imate, char *out)
{// pack sequence and qualities, write the BAM record; returns its size in bytes
    Transcript const &trOut=c.trOut;
    const uint64 Mate=m.Mate;
    char seqMate[DEF_readSeqLengthMax+1], qualMate[DEF_readSeqLengthMax+1];
    char *seqOut=NULL, *qualOut=NULL;

    if ( Mate==m.Str)  {//seq strand is correct, or mate is unmapped
        seqOut=Read0[Mate];
        qualOut=Qual0[Mate];
    } else {
        revComplementNucleotides(Read0[Mate], seqMate, readLengthOriginal[Mate]);
        seqMate[readLengthOriginal[Mate]]=0;
        for (uint64 ii=0;ii<readLengthOriginal[Mate]; ii++) qualMate[ii]=Qual0[Mate][readLengthOriginal[Mate]-1-ii];
        qualMate[readLengthOriginal[Mate]]=0;
        seqOut=&seqMate[0];
        qualOut=&qualMate[0];
    };

    uint64 seqMateLength=readLengthOriginal[Mate];
    if (c.alignType==-11) {//hard-clip on the left
        seqMateLength-=m.trimL1;
        seqOut+=m.trimL1;
        qualOut+=m.trimL1;
    } else if (c.alignType==-12) {
        seqMateLength-=m.trimR1;
    };

    nuclPackBAM(seqOut,seqMate,seqMateLength);

    uint32 *pBAM=(uint32*) out;
    const bool mapped=c.alignType<0;
    const uint64 trChrStart=c.trChrStart;
    //1: refID: Reference sequence ID, -1 <= refID <= n ref; -1 for a read without a mapping position.
    pBAM[1]= mapped ? trOut.Chr : (uint32) -1;
    //2: pos: 0-based leftmost coordinate (= POS - 1): int32_t
    pBAM[2]= mapped ? trOut.exons[m.iEx1][EX_G] - trChrStart : (uint32) -1;
    //3: bin<<16|MAPQ<<8|l_read_name; the name length includes the 0-char
    if (mapped) {
        pBAM[3]=( ( reg2bin(trOut.exons[m.iEx1][EX_G] - trChrStart,trOut.exons[m.iEx2][EX_G] + trOut.exons[m.iEx2][EX_L] - trChrStart) << 16 )
               |( m.MAPQ<<8 ) | ( strlen(readName) ) );
    } else {
        pBAM[3]=( reg2bin(-1,0) << 16 |  strlen(readName) );//4680=reg2bin(-1,0)
    };
    //4: FLAG<<16|n cigar op
    pBAM[4]=( ( ((m.samFLAG & P.outSAMflagAND) | P.outSAMflagOR) << 16 ) | (m.nCIGAR) );
    //5: l seq Length of SEQ
    pBAM[5]=seqMateLength;
    //6: next refID; 7: next pos (0-based)
    if (c.nMates>1) {
        pBAM[6]=trOut.Chr;
        pBAM[7]=trOut.exons[(imate==0 ? c.iExMate+1 : 0)][EX_G] - trChrStart;
    } else if (c.mateChr<genOut.nChrReal){
        pBAM[6]=c.mateChr;
        pBAM[7]=c.mateStart;
    } else {
        pBAM[6]=-1;
        pBAM[7]=-1;
    };
    //8: tlen Template length (= TLEN)
    if (c.nMates>1) {
        if (P.outSAMtlen==1) {
            int32 tlen=trOut.exons[trOut.nExons-1][EX_G]+trOut.exons[trOut.nExons-1][EX_L]-trOut.exons[0][EX_G];
            pBAM[8]=(imate==0 ? tlen : -tlen);
        } else if (P.outSAMtlen==2) {
            int32 tlen=(int32)c.tLen;
            pBAM[8]=(imate==c.leftMostMate ? tlen : -tlen);
        };
    } else {
        pBAM[8]=0;
    };

    uint32 recSize=9*sizeof(int32); //core record size

    //Read name1, NULL terminated (QNAME plus a tailing `\0')
    strcpy(out+recSize,readName+1);
    recSize+=strlen(readName);

    //CIGAR: op len<<4|op. `MIDNSHP=X'!`012345678'
    memcpy(out+recSize,m.packedCIGAR, m.nCIGAR*sizeof(int32));
    recSize+=m.nCIGAR*sizeof(int32);

    //4-bit encoded read: `=ACMGRSVTWYHKDBN'! [0; 15]; other characters mapped to `N'; high nybble first
    memcpy(out+recSize,seqMate,(seqMateLength+1)/2);
    recSize+=(seqMateLength+1)/2;

    //Phred base quality (a sequence of 0xFF if absent)
    if (readFileType==2 && P.outSAMmode != "NoQS") {//output quality
        for (uint32 ii=0; ii<seqMateLength; ii++) {
            out[recSize+ii]=qualOut[ii]-33;
        };
    } else {
        memset(out+recSize,0xFF,seqMateLength);
    };
    recSize+=seqMateLength;

    //attributes
    memcpy(out+recSize,m.attrOutArray,m.attrN);
    recSize+=m.attrN;

    //total size of the record
    pBAM[0]=recSize-sizeof(uint32);//record size excluding the size entry itself
    return recSize;
}
