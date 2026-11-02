#ifndef STAR_STITCH_CORE_H
#define STAR_STITCH_CORE_H
// Plain-data port of stitchWindowAligns / stitchAlignToTranscript / extendAlign /
// blocksOverlap / binarySearch2 for one alignment window, shared by the CPU
// verifier and the CUDA stitcher. Expressions, integer types and evaluation
// order follow the originals line by line; only containers and Parameters
// lookups are replaced by plain values. The recursion becomes an explicit
// depth-first stack: the include subtree runs to completion before the exclude
// branch, which reuses the parent frame (it was the parent's tail call).
// Scope: fixed-index short-read mapping without variation, chimeric detection
// or BySJout filtering (the batched-seed preflight rejects those modes).
#include <cstdint>
#include <cmath>
#ifdef __CUDACC__
#define STAR_HD __host__ __device__
#else
#define STAR_HD
#endif

namespace stitchcore {
using u64=unsigned long long;
using i64=long long;
constexpr unsigned maxExons=20;      // MAX_N_EXONS (short reads)
constexpr unsigned exSize=5;         // EX_SIZE
constexpr unsigned waSize=7;         // WA_SIZE
enum {eR=0,eG=1,eL=2,eFrag=3,eSjA=4};
enum {wLen=0,wR=1,wG=2,wNrep=3,wAnchor=4,wFrag=5,wSjA=6};
constexpr int matchScore=1;
constexpr u64 maxSjRepeatSearch=255;  // MAX_SJ_REPEAT_SEARCH
constexpr u64 readSeqLengthMax=650;   // DEF_readSeqLengthMax (short reads)
constexpr char fragSpacerBase=11;     // MARK_FRAG_SPACER_BASE
constexpr unsigned maxMates=3;        // MAX_N_MATES

struct Params {
    double mismatchOverLengthMax=0, splicedMateMapLminOverLmate=0, genomicLengthLog2scale=0;
    u64 sjdbOverhangMin=0, sjOverhangMin=0, splicedMateMapLmin=0;
    u64 transcriptsPerWindowNmax=0, transcriptsPerReadNmax=0;
    u64 intronMin=0, intronMax=0, matesGapMax=0;
    int endsProtrudeNbasesMax=0, multimapScoreRange=0, sjdbScore=0;
    int scoreStitchSJshift=0, scoreGapGCAG=0, scoreGapATAC=0, scoreGapNoncan=0, scoreGap=0;
    int scoreDelBase=0, scoreDelOpen=0, scoreInsBase=0, scoreInsOpen=0;
    int sjStitchMismatchNmax[4]={0,0,0,0};
    unsigned strandFieldType=0;
    int intronMotifs=0;               // 0 None, 1 RemoveNoncanonical, 2 RemoveNoncanonicalUnannotated
    bool extEnds[2][2]={{false,false},{false,false}};
    bool softClipAtReferenceEnds=true, removeInconsistentStrands=false, flushRight=false;
};
struct GenomeView {
    const char* G=nullptr; u64 nGenome=0;
    const u64 *chrStart=nullptr,*chrLength=nullptr;
    u64 sjdbN=0;
    const u64 *sjdbStart=nullptr,*sjdbEnd=nullptr;
    const unsigned char *sjdbMotif=nullptr,*sjdbShiftLeft=nullptr,*sjdbShiftRight=nullptr,*sjdbStrand=nullptr;
};
// The genome is guarded with N-like padding (5) beyond both ends.
STAR_HD inline char gAt(const GenomeView& g,u64 i) {return i<g.nGenome ? g.G[i] : char(5);}
struct ReadView {
    const char* R=nullptr; u64 Lread=0;
    u64 readLength[maxMates]={0,0,0};
    u64 mismatchLimit=0;
};
STAR_HD inline char rAt(const ReadView& r,u64 i) {return i<r.Lread ? r.R[i] : fragSpacerBase;}

struct Tr {
    u64 exons[maxExons][exSize];
    u64 shiftSJ[maxExons][2];
    int canonSJ[maxExons];
    unsigned char sjAnnot[maxExons], sjStr[maxExons];
    u64 intronMotifs[3];
    unsigned char sjMotifStrand; bool sjYes;
    u64 nExons; int iFrag;
    u64 rStart, roStart, rLength, gStart, gLength, Chr, Str, roStr;
    u64 nMatch, nMM, mappedLength, extendL;
    int maxScore;
    u64 nGap, lGap, nDel, nIns, lDel, lIns, nUnique, nAnchor;
};
// Transcript::reset() fields; exon rows are not read before being written.
STAR_HD inline void resetTr(Tr& t) {
    t.extendL=0; t.rStart=0; t.roStart=0; t.rLength=0; t.gStart=0; t.gLength=0;
    t.maxScore=0; t.nMatch=0; t.nMM=0;
    t.nGap=0; t.lGap=0; t.lDel=0; t.lIns=0; t.nDel=0; t.nIns=0; t.nUnique=0; t.nAnchor=0;
}
STAR_HD inline void addTr(Tr& t,const Tr& in) {
    t.maxScore+=in.maxScore; t.nMatch+=in.nMatch; t.nMM+=in.nMM;
    t.nGap+=in.nGap; t.lGap+=in.lGap; t.lDel+=in.lDel; t.nDel+=in.nDel;
    t.lIns+=in.lIns; t.nIns+=in.nIns; t.nUnique+=in.nUnique;
}
// Copies the transcript fields stitching can change; rows beyond nExons are
// never read downstream, so only live rows move.
STAR_HD inline void copyTr(Tr& d,const Tr& s) {
    const u64 n=s.nExons<maxExons ? s.nExons : maxExons;
    for(u64 i=0;i<n;++i) {
        for(unsigned k=0;k<exSize;++k) d.exons[i][k]=s.exons[i][k];
        d.shiftSJ[i][0]=s.shiftSJ[i][0]; d.shiftSJ[i][1]=s.shiftSJ[i][1];
        d.canonSJ[i]=s.canonSJ[i]; d.sjAnnot[i]=s.sjAnnot[i]; d.sjStr[i]=s.sjStr[i];
    }
    d.intronMotifs[0]=s.intronMotifs[0]; d.intronMotifs[1]=s.intronMotifs[1]; d.intronMotifs[2]=s.intronMotifs[2];
    d.sjMotifStrand=s.sjMotifStrand; d.sjYes=s.sjYes; d.nExons=s.nExons; d.iFrag=s.iFrag;
    d.rStart=s.rStart; d.roStart=s.roStart; d.rLength=s.rLength; d.gStart=s.gStart; d.gLength=s.gLength;
    d.Chr=s.Chr; d.Str=s.Str; d.roStr=s.roStr;
    d.nMatch=s.nMatch; d.nMM=s.nMM; d.mappedLength=s.mappedLength; d.extendL=s.extendL; d.maxScore=s.maxScore;
    d.nGap=s.nGap; d.lGap=s.lGap; d.nDel=s.nDel; d.nIns=s.nIns; d.lDel=s.lDel; d.lIns=s.lIns;
    d.nUnique=s.nUnique; d.nAnchor=s.nAnchor;
}
// Packed transfer form: Tr is sized for maxExons rows, but most transcripts
// have one to three, so device results move as a header plus live rows.
struct TrRow {
    u64 exon[exSize]; u64 shiftSJ[2]; int canonSJ; unsigned char sjAnnot, sjStr;
};
struct TrHead {
    u64 row;  // first row in the row array
    u64 intronMotifs[3];
    u64 nExons, rStart, roStart, rLength, gStart, gLength, Chr, Str, roStr;
    u64 nMatch, nMM, mappedLength, extendL;
    u64 nGap, lGap, nDel, nIns, lDel, lIns, nUnique, nAnchor;
    int iFrag, maxScore;
    unsigned char sjMotifStrand; bool sjYes;
};
STAR_HD inline u64 liveRows(const Tr& s) {return s.nExons<maxExons ? s.nExons : maxExons;}
STAR_HD inline void packTr(const Tr& s,TrHead& h,TrRow* rows,u64 row) {
    const u64 n=liveRows(s);
    for(u64 i=0;i<n;++i) {
        TrRow& r=rows[i];
        for(unsigned k=0;k<exSize;++k) r.exon[k]=s.exons[i][k];
        r.shiftSJ[0]=s.shiftSJ[i][0]; r.shiftSJ[1]=s.shiftSJ[i][1];
        r.canonSJ=s.canonSJ[i]; r.sjAnnot=s.sjAnnot[i]; r.sjStr=s.sjStr[i];
    }
    h.row=row;
    h.intronMotifs[0]=s.intronMotifs[0]; h.intronMotifs[1]=s.intronMotifs[1]; h.intronMotifs[2]=s.intronMotifs[2];
    h.nExons=s.nExons; h.rStart=s.rStart; h.roStart=s.roStart; h.rLength=s.rLength; h.gStart=s.gStart; h.gLength=s.gLength;
    h.Chr=s.Chr; h.Str=s.Str; h.roStr=s.roStr;
    h.nMatch=s.nMatch; h.nMM=s.nMM; h.mappedLength=s.mappedLength; h.extendL=s.extendL;
    h.nGap=s.nGap; h.lGap=s.lGap; h.nDel=s.nDel; h.nIns=s.nIns; h.lDel=s.lDel; h.lIns=s.lIns;
    h.nUnique=s.nUnique; h.nAnchor=s.nAnchor; h.iFrag=s.iFrag; h.maxScore=s.maxScore;
    h.sjMotifStrand=s.sjMotifStrand; h.sjYes=s.sjYes;
}
STAR_HD inline void unpackTr(const TrHead& h,const TrRow* rowBase,Tr& d) {
    const u64 n=h.nExons<maxExons ? h.nExons : maxExons;
    const TrRow* rows=rowBase+h.row;
    for(u64 i=0;i<n;++i) {
        const TrRow& r=rows[i];
        for(unsigned k=0;k<exSize;++k) d.exons[i][k]=r.exon[k];
        d.shiftSJ[i][0]=r.shiftSJ[0]; d.shiftSJ[i][1]=r.shiftSJ[1];
        d.canonSJ[i]=r.canonSJ; d.sjAnnot[i]=r.sjAnnot; d.sjStr[i]=r.sjStr;
    }
    d.intronMotifs[0]=h.intronMotifs[0]; d.intronMotifs[1]=h.intronMotifs[1]; d.intronMotifs[2]=h.intronMotifs[2];
    d.nExons=h.nExons; d.rStart=h.rStart; d.roStart=h.roStart; d.rLength=h.rLength; d.gStart=h.gStart; d.gLength=h.gLength;
    d.Chr=h.Chr; d.Str=h.Str; d.roStr=h.roStr;
    d.nMatch=h.nMatch; d.nMM=h.nMM; d.mappedLength=h.mappedLength; d.extendL=h.extendL;
    d.nGap=h.nGap; d.lGap=h.lGap; d.nDel=h.nDel; d.nIns=h.nIns; d.lDel=h.lDel; d.lIns=h.lIns;
    d.nUnique=h.nUnique; d.nAnchor=h.nAnchor; d.iFrag=h.iFrag; d.maxScore=h.maxScore;
    d.sjMotifStrand=h.sjMotifStrand; d.sjYes=h.sjYes;
}
template<class T> STAR_HD inline T minOf(T a,T b) {return b<a ? b:a;}
template<class T> STAR_HD inline T maxOf(T a,T b) {return a<b ? b:a;}

STAR_HD inline int binarySearch2(u64 x,u64 y,const u64* X,const u64* Y,int N) {
    if (N==0 || x>X[N-1] || x<X[0]) return -1;
    int i1=0, i2=N-1, i3=N/2;
    while (i2>i1+1) {
        i3=(i1+i2)/2;
        if (X[i3]>x) i2=i3; else i1=i3;
    }
    if (x==X[i1]) i3=i1;
    else if (x==X[i2]) i3=i2;
    else return -1;
    for (int jj=i3;jj>=0;jj--) {
        if (x!=X[jj]) break;
        else if (y==Y[jj]) return jj;
    }
    for (int jj=i3;jj<N;jj++) {
        if (x!=X[jj]) return -1;
        else if (y==Y[jj]) return jj;
    }
    return -2;
}

STAR_HD inline u64 blocksOverlap(const Tr& t1,const Tr& t2) {
    u64 i1=0, i2=0, nOverlap=0;
    while (i1<t1.nExons && i2<t2.nExons) {
        u64 rs1=t1.exons[i1][eR], rs2=t2.exons[i2][eR];
        u64 re1=t1.exons[i1][eR]+t1.exons[i1][eL], re2=t2.exons[i2][eR]+t2.exons[i2][eL];
        u64 gs1=t1.exons[i1][eG], gs2=t2.exons[i2][eG];
        if (rs1>=re2) i2++;
        else if (rs2>=re1) i1++;
        else if (gs1-rs1 != gs2-rs2) {
            if (re1>=re2) i2++;
            if (re2>=re1) i1++;
        } else {
            nOverlap += minOf(re1,re2) - maxOf(rs1,rs2);
            if (re1>=re2) i2++;
            if (re2>=re1) i1++;
        }
    }
    return nOverlap;
}

// extendAlign: R/G accessed through signed offsets from rStart/gStart.
STAR_HD inline bool extendAlign(const ReadView& rv,const GenomeView& gv,u64 rStart,u64 gStart,int dR,int dG,
        u64 L,u64 Lprev,u64 nMMprev,u64 nMMmax,double pMMmax,bool extendToEnd,Tr* trA) {
    int iS,iG;
    int Score=0, nMatch=0, nMM=0;
    trA->maxScore=0;
    auto R=[&](int i){return rAt(rv,rStart+(u64)(i64)i);};
    auto G=[&](int i){return gAt(gv,gStart+(u64)(i64)i);};
    if (extendToEnd) {
        int iExt;
        for (iExt=0;iExt<(int) L;iExt++) {
            iS=dR*iExt; iG=dG*iExt;
            if ((gStart+iG)==(u64)(-1) || G(iG)==5) {
                trA->extendL=0; trA->maxScore=-999999999; trA->nMatch=0; trA->nMM=nMMmax+1;
                return true;
            }
            if (R(iS)==fragSpacerBase) break;
            if (R(iS)>3 || G(iG)>3) continue;
            if (G(iG)==R(iS)) {nMatch++; Score += matchScore;}
            else {nMM++; Score -= matchScore;}
        }
        if (iExt>0) {
            trA->extendL=iExt; trA->maxScore=Score; trA->nMatch=nMatch; trA->nMM=nMM;
            return true;
        }
        return false;
    }
    for (int i=0;i<(int) L;i++) {
        iS=dR*i; iG=dG*i;
        if ((gStart+iG)==(u64)(-1) || G(iG)==5 || R(iS)==fragSpacerBase) break;
        if (R(iS)>3 || G(iG)>3) continue;
        if (G(iG)==R(iS)) {
            nMatch++; Score += matchScore;
            if (Score>trA->maxScore) {
                if (nMM+nMMprev <= minOf(pMMmax*double(Lprev+i+1), double(nMMmax)) ) {
                    trA->extendL=i+1; trA->maxScore=Score; trA->nMatch=nMatch; trA->nMM=nMM;
                }
            }
        } else {
            if (nMM+nMMprev >= minOf(pMMmax*double(Lprev+L), double(nMMmax)) ) break;
            nMM++; Score -= matchScore;
        }
    }
    return trA->extendL > 0;
}

STAR_HD inline int stitchAlignToTranscript(u64 rAend,u64 gAend,u64 rBstart,u64 gBstart,u64 L,u64 iFragB,u64 sjAB,
        const Params& P,const ReadView& rv,const GenomeView& gv,Tr* trA,const u64 outFilterMismatchNmaxTotal) {
    if (trA->nExons>=maxExons) return -1000010;
    auto R=[&](u64 i){return rAt(rv,i);};
    auto G=[&](u64 i){return gAt(gv,i);};
    int Score=0;
    if (sjAB!=((u64) -1) && trA->exons[trA->nExons-1][eSjA]==sjAB
            && trA->exons[trA->nExons-1][eFrag]==iFragB && rBstart==rAend+1 && gAend+1<gBstart ) {
        if (gv.sjdbMotif[sjAB]==0 && (L<=gv.sjdbShiftRight[sjAB] || trA->exons[trA->nExons-1][eL]<=gv.sjdbShiftLeft[sjAB]) )
            return -1000006;
        trA->exons[trA->nExons][eL] = L;
        trA->exons[trA->nExons][eR] = rBstart;
        trA->exons[trA->nExons][eG] = gBstart;
        trA->canonSJ[trA->nExons-1]=gv.sjdbMotif[sjAB];
        trA->shiftSJ[trA->nExons-1][0]=gv.sjdbShiftLeft[sjAB];
        trA->shiftSJ[trA->nExons-1][1]=gv.sjdbShiftRight[sjAB];
        trA->sjAnnot[trA->nExons-1]=1;
        trA->sjStr[trA->nExons-1]=gv.sjdbStrand[sjAB];
        trA->nExons++;
        trA->nMatch+=L;
        for (u64 ii=rBstart;ii<rBstart+L;ii++) Score+=matchScore;
        Score+=P.sjdbScore;
    } else {
        trA->sjAnnot[trA->nExons-1]=0;
        trA->sjStr[trA->nExons-1]=0;
        if (trA->exons[trA->nExons-1][eFrag]==iFragB) {
            u64 gBend=gBstart+L-1;
            u64 rBend=rBstart+L-1;
            if (rBend<=rAend) return -1000001;
            if (gBend<=gAend && trA->exons[trA->nExons-1][eFrag]==iFragB) return -1000002;
            if (rBstart<=rAend) {
                gBstart+=rAend-rBstart+1;
                rBstart=rAend+1;
                L=rBend-rBstart+1;
            }
            for (u64 ii=rBstart;ii<=rBend;ii++) Score+=matchScore;
            int gGap=gBstart-gAend-1;
            int rGap=rBstart-rAend-1;
            u64 nMatch=L, nMM=0, Del=0, Ins=0, nIns=0, nDel=0;
            int jR=0, jCan=999;
            u64 gBstart1=gBstart-rGap-1;
            if ( gGap==0 && rGap==0 ) {
            } else if ( gGap>0 && rGap>0 && rGap==gGap ) {
                for (int ii=1;ii<=rGap;ii++) {
                    if (G(gAend+ii)<4 && R(rAend+ii)<4) {
                        if ( R(rAend+ii)==G(gAend+ii) ) {Score+=matchScore; nMatch++;}
                        else {Score-=matchScore; nMM++;}
                    }
                }
            } else if ( gGap>rGap ) {
                nDel=1;
                Del=gGap-rGap;
                if (Del>P.intronMax && P.intronMax>0) return -1000003;
                int Score1=0;
                int jR1=1;
                do {
                    jR1--;
                    if ( R(rAend+jR1)!=G(gBstart1+jR1) && G(gBstart1+jR1)<4 && R(rAend+jR1)==G(gAend+jR1)) Score1 -= matchScore;
                } while ( Score1+P.scoreStitchSJshift >= 0 && int(trA->exons[trA->nExons-1][eL]) + jR1 > 1);
                int maxScore2=-999999;
                Score1=0;
                int jPen=0;
                do {
                    if ( R(rAend+jR1)==G(gAend+jR1) && R(rAend+jR1)!=G(gBstart1+jR1) ) Score1+=matchScore;
                    if ( R(rAend+jR1)!=G(gAend+jR1) && R(rAend+jR1)==G(gBstart1+jR1) ) Score1-=matchScore;
                    int jCan1=-1, jPen1=0, Score2=Score1;
                    if (Del>=P.intronMin) {
                        if ( G(gAend+jR1+1)==2 && G(gAend+jR1+2)==3 && G(gBstart1+jR1-1)==0 && G(gBstart1+jR1)==2 ) {jCan1=1;}
                        else if ( G(gAend+jR1+1)==1 && G(gAend+jR1+2)==3 && G(gBstart1+jR1-1)==0 && G(gBstart1+jR1)==1 ) {jCan1=2;}
                        else if ( G(gAend+jR1+1)==2 && G(gAend+jR1+2)==1 && G(gBstart1+jR1-1)==0 && G(gBstart1+jR1)==2 ) {jCan1=3; jPen1=P.scoreGapGCAG;}
                        else if ( G(gAend+jR1+1)==1 && G(gAend+jR1+2)==3 && G(gBstart1+jR1-1)==2 && G(gBstart1+jR1)==1 ) {jCan1=4; jPen1=P.scoreGapGCAG;}
                        else if ( G(gAend+jR1+1)==0 && G(gAend+jR1+2)==3 && G(gBstart1+jR1-1)==0 && G(gBstart1+jR1)==1 ) {jCan1=5; jPen1=P.scoreGapATAC;}
                        else if ( G(gAend+jR1+1)==2 && G(gAend+jR1+2)==3 && G(gBstart1+jR1-1)==0 && G(gBstart1+jR1)==3 ) {jCan1=6; jPen1=P.scoreGapATAC;}
                        else {jCan1=0; jPen1=P.scoreGapNoncan;}
                        Score2 += jPen1;
                    }
                    if (maxScore2 < Score2 ) {maxScore2=Score2; jR=jR1; jCan=jCan1; jPen=jPen1;}
                    jR1++;
                } while ( jR1 < int(rBend) - int(rAend) );
                u64 jjL=0,jjR=0;
                while ( gAend+jR>=jjL && G(gAend-jjL+jR)==G(gBstart1-jjL+jR) && G(gAend-jjL+jR)<4 && jjL<=maxSjRepeatSearch) jjL++;
                while ( gAend+jjR+jR+1<gv.nGenome && G(gAend+jjR+jR+1)==G(gBstart1+jjR+jR+1) && G(gAend+jjR+jR+1)<4 && jjR<=maxSjRepeatSearch) jjR++;
                if (jCan<=0) {
                    jR-=jjL;
                    if (int(trA->exons[trA->nExons-1][eL])+jR<1) return -1000005;
                    jjR+=jjL;
                    jjL=0;
                }
                for (int ii=minOf(1,jR+1);ii<=maxOf(rGap,jR);ii++) {
                    u64 g1=(ii<=jR) ? (gAend+ii):(gBstart1+ii);
                    if (G(g1)<4 && R(rAend+ii)<4) {
                        if ( R(rAend+ii)==G(g1) ) {
                            if (ii>=1 && ii <=rGap) {Score+=matchScore; nMatch++;}
                        } else {
                            Score-=matchScore; nMM++;
                            if (ii<1 || ii>rGap) {Score-=matchScore; nMatch--;}
                        }
                    }
                }
                if (gv.sjdbN>0) {
                    u64 jS=gAend+jR+1, jE=gBstart1+jR;
                    int sjdbInd=binarySearch2(jS,jE,gv.sjdbStart,gv.sjdbEnd,(int)gv.sjdbN);
                    if (sjdbInd<0) {
                        if (Del>=P.intronMin) {
                            Score += P.scoreGap + jPen;
                        } else {
                            Score += Del*P.scoreDelBase + P.scoreDelOpen;
                            jCan=-1;
                            trA->sjAnnot[trA->nExons-1]=0;
                        }
                    } else {
                        jCan=gv.sjdbMotif[sjdbInd];
                        if (gv.sjdbMotif[sjdbInd]==0) {
                            if (L<=gv.sjdbShiftLeft[sjdbInd] || trA->exons[trA->nExons-1][eL]<=gv.sjdbShiftLeft[sjdbInd]) return -1000006;
                            jR += (int) gv.sjdbShiftLeft[sjdbInd];
                            if ( rAend+jR >= rBend ) return -1000006;
                            jjL=gv.sjdbShiftLeft[sjdbInd];
                            jjR=gv.sjdbShiftRight[sjdbInd];
                        }
                        trA->sjAnnot[trA->nExons-1]=1;
                        trA->sjStr[trA->nExons-1]=gv.sjdbStrand[sjdbInd];
                        Score += P.sjdbScore;
                    }
                } else {
                    if (Del>=P.intronMin) {
                        Score += P.scoreGap + jPen;
                    } else {
                        Score += Del*P.scoreDelBase + P.scoreDelOpen;
                        jCan=-1;
                        trA->sjAnnot[trA->nExons-1]=0;
                    }
                }
                trA->shiftSJ[trA->nExons-1][0]=jjL;
                trA->shiftSJ[trA->nExons-1][1]=jjR;
                trA->canonSJ[trA->nExons-1]=jCan;
                if (trA->sjAnnot[trA->nExons-1]==0) {
                    if (jCan>0) trA->sjStr[trA->nExons-1]=2-jCan%2;
                    else trA->sjStr[trA->nExons-1]=0;
                }
            } else if ( rGap>gGap ) {
                Ins=rGap-gGap;
                nIns=1;
                if (gGap==0) {
                    jR=0;
                } else if (gGap<0) {
                    jR=0;
                    for (int ii=0; ii<-gGap; ii++) Score -= matchScore;
                } else {
                    int Score1=0; int maxScore1=0;
                    for (int jR1=1;jR1<=gGap;jR1++) {
                        if (G(gAend+jR1)<4) {
                            Score1+=( R(rAend+jR1)==G(gAend+jR1) ) ? matchScore:-matchScore;
                            Score1+=( R(rAend+Ins+jR1)==G(gAend+jR1) ) ? -matchScore:+matchScore;
                        }
                        if (Score1>maxScore1 || (Score1==maxScore1 && P.flushRight)) {maxScore1=Score1; jR=jR1;}
                    }
                    for (int ii=1;ii<=gGap;ii++) {
                        u64 r1=rAend+ii+(ii<=jR ? 0:Ins);
                        if (G(gAend+ii)<4 && R(r1)<4) {
                            if ( R(r1)==G(gAend+ii) ) {Score+=matchScore; nMatch++;}
                            else {Score-=matchScore; nMM++;}
                        }
                    }
                }
                if (P.flushRight) {
                    for (; jR<(int)rBend-(int)rAend-(int)Ins; jR++ ) {
                        if (R(rAend+jR+1)!=G(gAend+jR+1) || G(gAend+jR+1)==4) break;
                    }
                    if (jR==(int)rBend-(int)rAend-(int)Ins) return -1000009;
                }
                Score += Ins*P.scoreInsBase + P.scoreInsOpen;
                jCan=-2;
            }
            if ( (trA->nMM + nMM)<=outFilterMismatchNmaxTotal
                 && ( jCan<0 || (jCan<7 && nMM<= (u64) P.sjStitchMismatchNmax[(jCan+1)/2]) ) ) {
                trA->nMM += nMM;
                trA->nMatch += nMatch;
                if (Del>=P.intronMin) {trA->nGap += nDel; trA->lGap += Del;}
                else {trA->nDel += nDel; trA->lDel += Del;}
                if (Del==0 && Ins==0) {
                    trA->exons[trA->nExons-1][eL] += rBend-rAend;
                } else if (Del>0) {
                    trA->exons[trA->nExons-1][eL] += jR;
                    trA->exons[trA->nExons][eL] = rBend-rAend-jR;
                    trA->exons[trA->nExons][eR] = rAend+jR+1;
                    trA->exons[trA->nExons][eG] = gBstart1+jR+1;
                    trA->nExons++;
                } else if (Ins>0) {
                    trA->nIns += nIns;
                    trA->lIns += Ins;
                    trA->exons[trA->nExons-1][eL] += jR;
                    trA->exons[trA->nExons][eL] = rBend-rAend-jR-Ins;
                    trA->exons[trA->nExons][eR] = rAend+jR+Ins+1;
                    trA->exons[trA->nExons][eG] = gAend+1+jR;
                    trA->canonSJ[trA->nExons-1]=-2;
                    trA->sjAnnot[trA->nExons-1]=0;
                    trA->nExons++;
                }
            } else {
                return -1000007;
            }
        } else if (gBstart+trA->exons[0][eR]+P.endsProtrudeNbasesMax >= trA->exons[0][eG] || trA->exons[0][eG] < trA->exons[0][eR]) {
            if (P.matesGapMax>0 && gBstart > trA->exons[trA->nExons-1][eG] + trA->exons[trA->nExons-1][eL] + P.matesGapMax)
                return -1000004;
            for (u64 ii=rBstart;ii<rBstart+L;ii++) Score+=matchScore;
            Tr trExtend;
            resetTr(trExtend);
            if ( extendAlign(rv, gv, rAend+1, gAend+1, 1, 1, readSeqLengthMax, trA->nMatch, trA->nMM, outFilterMismatchNmaxTotal, P.mismatchOverLengthMax,
                             P.extEnds[trA->exons[trA->nExons-1][eFrag]][1], &trExtend) ) {
                addTr(*trA,trExtend);
                Score += trExtend.maxScore;
                trA->exons[trA->nExons-1][eL] += trExtend.extendL;
            }
            trA->exons[trA->nExons][eR] = rBstart;
            trA->exons[trA->nExons][eG] = gBstart;
            trA->exons[trA->nExons][eL] = L;
            trA->nMatch += L;
            resetTr(trExtend);
            u64 extlen=P.extEnds[iFragB][1] ? readSeqLengthMax : gBstart-trA->exons[0][eG]+trA->exons[0][eR];
            if ( extendAlign(rv, gv, rBstart-1, gBstart-1, -1, -1, extlen, trA->nMatch, trA->nMM, outFilterMismatchNmaxTotal, P.mismatchOverLengthMax,
                             P.extEnds[iFragB][1], &trExtend) ) {
                addTr(*trA,trExtend);
                Score += trExtend.maxScore;
                trA->exons[trA->nExons][eR] -= trExtend.extendL;
                trA->exons[trA->nExons][eG] -= trExtend.extendL;
                trA->exons[trA->nExons][eL] += trExtend.extendL;
            }
            trA->canonSJ[trA->nExons-1]=-3;
            trA->sjAnnot[trA->nExons-1]=0;
            trA->nExons++;
        } else {
            return -1000008;
        }
    }
    trA->exons[trA->nExons-1][eFrag]=iFragB;
    trA->exons[trA->nExons-1][eSjA]=sjAB;
    return Score;
}

// Window recording state, equivalent to the CPU pointer array wTr[0..Nmax]:
// slot[k] holds the transcript that wTr[k] points to.
struct WindowOut {
    Tr** slot;      // cap distinct transcript pointers (CPU: Nmax+1)
    u64 n;          // *nWinTr
    u64 cap;        // usable slot pointers
    bool overflow;  // a recording needed slot[cap]; the result is not usable
};
// Score adjustment for genomic length. Host and device log2 must agree; the
// caller re-checks the recorded transcripts on the host (see GpuStitch).
STAR_HD inline int genomicLengthScore(int Score,const Params& P,const Tr& trA) {
    const u64 span=trA.exons[trA.nExons-1][eG]+trA.exons[trA.nExons-1][eL] - trA.exons[0][eG];
    const double x=(double) span;
#ifdef __CUDA_ARCH__
    // CUDA log2 is within 1 ulp, not exact: log2(64) can come back just under 6
    // and flip the ceil below. Powers of two are the only spans whose score
    // lands exactly on a rounding boundary, so take their exponent directly.
    const double lg = (span!=0 && (span&(span-1))==0) ? double(63-__clzll((long long)span)) : ::log2(x);
    // Explicit rounding steps: no fused multiply-add, matching the host expression.
    Score += int(::ceil(__dsub_rn(__dmul_rn(lg,P.genomicLengthLog2scale),0.5)));
#else
    Score += int(std::ceil( std::log2(x) * P.genomicLengthLog2scale - 0.5));
#endif
    return maxOf(0,Score);
}

// Finalization branch (iA>=nA). trA is the frame's private copy.
STAR_HD inline void finalizeTr(int Score,u64 tR2,u64 tG2,Tr& trA,const Params& P,const ReadView& rv,const GenomeView& gv,
        WindowOut& w,int* maxScoreMate) {
    const u64 Lread=rv.Lread;
    Tr trAstep1;
    int vOrder[2];
    if ( trA.roStr==0 ) {vOrder[0]=0; vOrder[1]=1;} else {vOrder[0]=1; vOrder[1]=0;}
    for (int iOrd=0;iOrd<2;iOrd++) {
        switch (vOrder[iOrd]) {
        case 0:
            if (trA.rStart>0) {
                resetTr(trAstep1);
                u64 imate=trA.exons[0][eFrag];
                if ( extendAlign(rv, gv, trA.rStart-1, trA.gStart-1, -1, -1, trA.rStart, tR2-trA.rStart+1,
                                 trA.nMM, rv.mismatchLimit, P.mismatchOverLengthMax,
                                 P.extEnds[imate][(int)(trA.Str!=imate)], &trAstep1) ) {
                    addTr(trA,trAstep1);
                    Score += trAstep1.maxScore;
                    trA.exons[0][eR] = trA.rStart = trA.rStart - trAstep1.extendL;
                    trA.exons[0][eG] = trA.gStart = trA.gStart - trAstep1.extendL;
                    trA.exons[0][eL] += trAstep1.extendL;
                }
            }
            break;
        case 1:
            if ( tR2<Lread ) {
                resetTr(trAstep1);
                u64 imate=trA.exons[trA.nExons-1][eFrag];
                if ( extendAlign(rv, gv, tR2+1, tG2+1, +1, +1, Lread-tR2-1, tR2-trA.rStart+1,
                                 trA.nMM, rv.mismatchLimit, P.mismatchOverLengthMax,
                                 P.extEnds[imate][(int)(imate==trA.Str)], &trAstep1) ) {
                    addTr(trA,trAstep1);
                    Score += trAstep1.maxScore;
                    tR2 += trAstep1.extendL;
                    tG2 += trAstep1.extendL;
                    trA.exons[trA.nExons-1][eL] += trAstep1.extendL;
                }
            }
        }
    }
    if (!P.softClipAtReferenceEnds &&
            ( (trA.exons[trA.nExons-1][eG] + Lread-trA.exons[trA.nExons-1][eR]) > (gv.chrStart[trA.Chr]+gv.chrLength[trA.Chr]) ||
               trA.exons[0][eG]<(gv.chrStart[trA.Chr]+trA.exons[0][eR]) ) ) return;
    trA.rLength = 0;
    for (u64 isj=0;isj<trA.nExons;isj++) trA.rLength += trA.exons[isj][eL];
    trA.gLength = tG2+1-trA.gStart;
    for (u64 isj=0;isj<trA.nExons-1;isj++) {
        if ( trA.canonSJ[isj]>=0 ) {
            if (trA.sjAnnot[isj]==1) {
                if (  ( trA.exons[isj][eL]   < P.sjdbOverhangMin && (isj==0            || trA.canonSJ[isj-1]==-3 || (trA.sjAnnot[isj-1]==0 && trA.canonSJ[isj-1]>=0) ) )
                   || ( trA.exons[isj+1][eL] < P.sjdbOverhangMin && (isj==trA.nExons-2 || trA.canonSJ[isj+1]==-3 || (trA.sjAnnot[isj+1]==0 && trA.canonSJ[isj+1]>=0) ) ) ) return;
            } else {
                if (  trA.exons[isj][eL] < P.sjOverhangMin + trA.shiftSJ[isj][0]
                   || trA.exons[isj+1][eL] < P.sjOverhangMin + trA.shiftSJ[isj][1]   ) return;
            }
        }
    }
    if (trA.nExons>1 && trA.sjAnnot[trA.nExons-2]==1 && trA.exons[trA.nExons-1][eL] < P.sjdbOverhangMin) return;
    u64 sjN=0;
    trA.intronMotifs[0]=0; trA.intronMotifs[1]=0; trA.intronMotifs[2]=0;
    trA.sjYes=false;
    for (u64 iex=0;iex<trA.nExons-1;iex++) {
        if (trA.canonSJ[iex]>=0) {sjN++; trA.intronMotifs[trA.sjStr[iex]]++; trA.sjYes=true;}
    }
    if (trA.intronMotifs[1]>0 && trA.intronMotifs[2]==0) trA.sjMotifStrand=1;
    else if (trA.intronMotifs[1]==0 && trA.intronMotifs[2]>0) trA.sjMotifStrand=2;
    else trA.sjMotifStrand=0;
    if (trA.intronMotifs[1]>0 && trA.intronMotifs[2]>0 && P.removeInconsistentStrands) return;
    if (sjN>0 && trA.sjMotifStrand==0 && P.strandFieldType==1) return;
    if (P.intronMotifs==1) {
        for (u64 iex=0;iex<trA.nExons-1;iex++) if (trA.canonSJ[iex]==0) return;
    } else if (P.intronMotifs==2) {
        for (u64 iex=0;iex<trA.nExons-1;iex++) if (trA.canonSJ[iex]==0 && trA.sjAnnot[iex]==0) return;
    }
    {
        u64 nsj=0,exl=0;
        for (u64 iex=0;iex<trA.nExons;iex++) {
            exl+=trA.exons[iex][eL];
            if (iex==trA.nExons-1 || trA.canonSJ[iex]==-3) {
                if (nsj>0 && (exl<P.splicedMateMapLmin || exl < (u64) (P.splicedMateMapLminOverLmate*rv.readLength[trA.exons[iex][eFrag]])) ) return;
                exl=0;nsj=0;
            } else if (trA.canonSJ[iex]>=0) {
                nsj++;
            }
        }
    }
    if ( trA.exons[0][eFrag]!=trA.exons[trA.nExons-1][eFrag] ) {
        if (trA.exons[trA.nExons-1][eG]+trA.exons[trA.nExons-1][eL] <= trA.exons[0][eG]) return;
        u64 iexM2=trA.nExons;
        for (u64 iex=0;iex<trA.nExons-1;iex++) {
            if (trA.canonSJ[iex]==-3) {iexM2=iex+1; break;}
        }
        if ( trA.exons[iexM2-1][eG] + trA.exons[iexM2-1][eL] > trA.exons[iexM2][eG] ) {
            if (trA.exons[0][eG] > trA.exons[iexM2][eG]+trA.exons[0][eR]+P.endsProtrudeNbasesMax) return;
            if (trA.exons[iexM2-1][eG]+trA.exons[iexM2-1][eL] >
               trA.exons[trA.nExons-1][eG]+Lread-trA.exons[trA.nExons-1][eR]+P.endsProtrudeNbasesMax) return;
            u64 iex1=1, iex2=iexM2+1;
            for  (; iex1<iexM2; iex1++) {
                if (trA.exons[iex1][eG] >= trA.exons[iex2-1][eG] + trA.exons[iex2-1][eL]) break;
            }
            while (iex1<iexM2 && iex2<trA.nExons) {
                if (trA.canonSJ[iex1-1]<0) {iex1++; continue;}
                if (trA.canonSJ[iex2-1]<0) {iex2++; continue;}
                if ( ( trA.exons[iex1][eG]!=trA.exons[iex2][eG] ) || ( (trA.exons[iex1-1][eG]+trA.exons[iex1-1][eL]) != (trA.exons[iex2-1][eG]+trA.exons[iex2-1][eL]) ) ) return;
                iex1++; iex2++;
            }
        }
    }
    if (P.genomicLengthLog2scale!=0) Score=genomicLengthScore(Score,P,trA);
    trA.roStart = (trA.roStr == 0) ? trA.rStart : Lread - trA.rStart - trA.rLength;
    trA.maxScore=Score;
    if (trA.exons[0][eFrag]==trA.exons[trA.nExons-1][eFrag]) {
        trA.iFrag=trA.exons[0][eFrag];
        maxScoreMate[trA.iFrag] = maxOf(maxScoreMate[trA.iFrag] , Score);
    } else {
        trA.iFrag=-1;
    }
    // variationAdjust() is 0 without a variation database.
    trA.maxScore=Score;
    Tr** wTr=w.slot;
    if ( Score+P.multimapScoreRange >= wTr[0]->maxScore
         || ( trA.iFrag>=0 && Score+P.multimapScoreRange >= maxScoreMate[trA.iFrag] ) ) {
        u64 iTr=0;
        trA.mappedLength=0;
        for (u64 iex=0;iex<trA.nExons;iex++) trA.mappedLength += trA.exons[iex][eL];
        while (iTr < w.n) {
            u64 nOverlap=blocksOverlap(trA,*wTr[iTr]);
            u64 uNew=trA.mappedLength-nOverlap;
            u64 uOld=wTr[iTr]->mappedLength-nOverlap;
            if (uNew==0 && Score < wTr[iTr]->maxScore) {
                break;
            } else if (uOld==0) {
                Tr *pTr=wTr[iTr];
                for (u64 ii=iTr+1;ii<w.n;ii++) wTr[ii-1]=wTr[ii];
                w.n--;
                wTr[w.n]=pTr;
            } else if (uOld>0 && (uNew>0 || Score >= wTr[iTr]->maxScore) ) {
                iTr++;
            }
        }
        if (iTr==w.n) {
            for (iTr=0;iTr<w.n;iTr++) {
                if (Score>wTr[iTr]->maxScore || (Score==wTr[iTr]->maxScore && trA.gLength<wTr[iTr]->gLength) ) break;
            }
            if (w.n>=w.cap) {w.overflow=true; return;}
            Tr *pTr=wTr[w.n];
            for (int ii=(int)w.n; ii> int(iTr); ii--) wTr[ii]=wTr[ii-1];
            wTr[iTr]=pTr;
            copyTr(*wTr[iTr],trA);
            if (w.n<P.transcriptsPerWindowNmax) w.n++;
        }
    }
}

struct Frame {
    u64 iA, tR2, tG2;
    int Score, state;
    Tr tr;
};
// Equivalent of stitchWindowAligns(0,nA,0,..,0,0,trInit,...). `stack` holds
// nA+1 frames. WA rows are row-major WA_SIZE values. Returns false only when
// the stack would overflow (caller falls back to the CPU path).
STAR_HD inline bool stitchWindow(const u64* WA,u64 nA,const Tr& trInit,const Params& P,const ReadView& rv,
        const GenomeView& gv,Frame* stack,u64 stackCapacity,WindowOut& w,int* maxScoreMate) {
    if (stackCapacity<1) return false;
    u64 sp=0;
    stack[0].iA=0; stack[0].tR2=0; stack[0].tG2=0; stack[0].Score=0; stack[0].state=0;
    stack[0].tr=trInit;
    for (;;) {
        Frame& F=stack[sp];
        bool popFrame=false;
        if (F.state==0) {
            if (F.iA>=nA) {
                if (F.tR2!=0) finalizeTr(F.Score,F.tR2,F.tG2,F.tr,P,rv,gv,w,maxScoreMate);
                popFrame=true;
            } else {
                F.state=1;
                const u64* A=WA+F.iA*waSize;
                const Tr& trA=F.tr;
                bool skipInclude=false;
                if (trA.nExons>0) {
                    if (trA.nExons>=maxExons) skipInclude=true;
                    else if (A[wFrag]==trA.exons[trA.nExons-1][eFrag]) {
                        u64 rBend=A[wR]+A[wLen]-1, gBend=A[wG]+A[wLen]-1;
                        if (rBend<=F.tR2 || gBend<=F.tG2) skipInclude=true;
                    }
                }
                if (!skipInclude) {
                    if (sp+1>=stackCapacity) return false;
                    Frame& C=stack[sp+1];
                    copyTr(C.tr,trA);
                    int dScore=0;
                    if (trA.nExons>0) {
                        dScore=stitchAlignToTranscript(F.tR2,F.tG2,A[wR],A[wG],A[wLen],A[wFrag],A[wSjA],P,rv,gv,&C.tr,rv.mismatchLimit);
                    } else {
                        C.tr.exons[0][eR]=C.tr.rStart=A[wR];
                        C.tr.exons[0][eG]=C.tr.gStart=A[wG];
                        C.tr.exons[0][eL]=A[wLen];
                        C.tr.exons[0][eFrag]=A[wFrag];
                        C.tr.exons[0][eSjA]=A[wSjA];
                        C.tr.nExons=1;
                        for (u64 ii=0;ii<A[wLen];ii++) dScore+=matchScore;
                        C.tr.nMatch=A[wLen];
                    }
                    if (dScore>-1000000) {
                        if ( A[wNrep]==1 ) C.tr.nUnique++;
                        if ( A[wAnchor]>0 ) C.tr.nAnchor++;
                        C.iA=F.iA+1; C.Score=F.Score+dScore;
                        C.tR2=A[wR]+A[wLen]-1; C.tG2=A[wG]+A[wLen]-1;
                        C.state=0;
                        ++sp;
                        continue;
                    }
                }
            }
        }
        if (!popFrame && F.state==1) {
            const u64* A=WA+F.iA*waSize;
            if (A[wAnchor]!=2 || F.tr.nAnchor>0) {
                F.iA++; F.state=0;   // exclude branch: tail call reusing this frame
                continue;
            }
            popFrame=true;
        }
        if (popFrame) {
            if (sp==0) return true;
            --sp;
        }
    }
}
} // namespace stitchcore
#endif
