#include "StitchCoreBridge.h"
#include "Parameters.h"
#include "Genome.h"
#include "Transcript.h"
#include "StitchBatch.h"
#include <cstdlib>
#include <sstream>
#include <stdexcept>
#include <vector>
#include <iostream>

#ifdef COMPILE_FOR_LONG_READS
// STARlong: the stitching core and device stitching are short-read only; these stubs keep
// the shared call sites linking. Batched seeding and device stitching are rejected for STARlong.
namespace stitchcore {
[[noreturn]] static void unavailable() { throw std::logic_error("the stitching core is not available in STARlong"); }
Params paramsFrom(const Parameters&) { unavailable(); }
GenomeView genomeFrom(const Genome&) { unavailable(); }
void toCore(const Transcript&, Tr&) { unavailable(); }
void fromCore(const Tr&, Transcript&) { unavailable(); }
void fromPacked(const TrHead&, const TrRow*, Transcript&) { unavailable(); }
std::string compare(const Tr&, const Transcript&) { unavailable(); }
bool verifyEnabled() { return false; }
void verifyWindow(const Parameters&, const Genome&, const uint64 (*)[WA_SIZE], uint64, const Transcript&, const char*, uint64,
                  const uint64*, uint64, const intScore*, const intScore*, Transcript* const*, uint64, uint64) { unavailable(); }
void verifyDevice(const ::StitchWindowResult&, const ::StitchResults&, uint64, Transcript* const*, uint64, uint64) { unavailable(); }
void verifyMate(int, intScore, uint64, uint64) { unavailable(); }
} // namespace stitchcore
#else
static_assert(MAX_N_EXONS==stitchcore::maxExons, "StitchCore supports short-read builds only");
static_assert(EX_SIZE==stitchcore::exSize && WA_SIZE==stitchcore::waSize, "layout mismatch");
static_assert(MAX_N_MATES==stitchcore::maxMates && DEF_readSeqLengthMax==stitchcore::readSeqLengthMax, "limit mismatch");
static_assert(MARK_FRAG_SPACER_BASE==stitchcore::fragSpacerBase && MAX_SJ_REPEAT_SEARCH==stitchcore::maxSjRepeatSearch, "constant mismatch");
static_assert(EXTEND_ORDER==1, "StitchCore implements EXTEND_ORDER 1");

namespace stitchcore {
Params paramsFrom(const Parameters& P) {
    Params p;
    p.mismatchOverLengthMax=P.alignFilter.mismatchOverLengthMax;
    p.splicedMateMapLminOverLmate=P.alignSplicedMateMapLminOverLmate;
    p.genomicLengthLog2scale=P.scoreGenomicLengthLog2scale;
    p.sjdbOverhangMin=P.alignSJDBoverhangMin; p.sjOverhangMin=P.alignSJoverhangMin;
    p.splicedMateMapLmin=P.alignSplicedMateMapLmin;
    p.transcriptsPerWindowNmax=P.alignTranscriptsPerWindowNmax;
    p.transcriptsPerReadNmax=P.alignTranscriptsPerReadNmax;
    p.intronMin=P.alignIntronMin; p.intronMax=P.alignIntronMax; p.matesGapMax=P.alignMatesGapMax;
    p.endsProtrudeNbasesMax=P.alignEndsProtrude.nBasesMax;
    p.multimapScoreRange=P.alignFilter.multimapScoreRange;
    p.sjdbScore=P.pGe.sjdbScore;
    p.scoreStitchSJshift=P.scoreStitchSJshift; p.scoreGapGCAG=P.scoreGapGCAG; p.scoreGapATAC=P.scoreGapATAC;
    p.scoreGapNoncan=P.scoreGapNoncan; p.scoreGap=P.scoreGap;
    p.scoreDelBase=P.scoreDelBase; p.scoreDelOpen=P.scoreDelOpen; p.scoreInsBase=P.scoreInsBase; p.scoreInsOpen=P.scoreInsOpen;
    if(P.alignSJstitchMismatchNmax.size()!=4) throw std::invalid_argument("alignSJstitchMismatchNmax must have 4 values");
    for(int i=0;i<4;++i) p.sjStitchMismatchNmax[i]=P.alignSJstitchMismatchNmax[i];
    p.strandFieldType=P.outSAMstrandField.type;
    if(P.outFilterIntronMotifs=="None") p.intronMotifs=0;
    else if(P.outFilterIntronMotifs=="RemoveNoncanonical") p.intronMotifs=1;
    else if(P.outFilterIntronMotifs=="RemoveNoncanonicalUnannotated") p.intronMotifs=2;
    else throw std::invalid_argument("unsupported outFilterIntronMotifs for StitchCore");
    for(int i=0;i<2;++i) for(int j=0;j<2;++j) p.extEnds[i][j]=P.alignEndsType.ext[i][j];
    p.softClipAtReferenceEnds=P.alignSoftClipAtReferenceEnds.yes;
    p.removeInconsistentStrands=P.outFilterIntronStrands=="RemoveInconsistentStrands";
    p.flushRight=P.alignInsertionFlush.flushRight;
    return p;
}
GenomeView genomeFrom(const Genome& g) {
    GenomeView v;
    v.G=g.G; v.nGenome=g.nGenome;
    v.chrStart=reinterpret_cast<const u64*>(g.chrStart.data());
    v.chrLength=reinterpret_cast<const u64*>(g.chrLength.data());
    v.sjdbN=g.sjdbN;
    v.sjdbStart=reinterpret_cast<const u64*>(g.sjdbStart); v.sjdbEnd=reinterpret_cast<const u64*>(g.sjdbEnd);
    v.sjdbMotif=g.sjdbMotif; v.sjdbShiftLeft=g.sjdbShiftLeft; v.sjdbShiftRight=g.sjdbShiftRight; v.sjdbStrand=g.sjdbStrand;
    return v;
}
void toCore(const Transcript& in, Tr& t) {
    const u64 n=in.nExons<maxExons ? in.nExons : maxExons;
    for(u64 i=0;i<n;++i) {
        for(unsigned k=0;k<exSize;++k) t.exons[i][k]=in.exons[i][k];
        t.shiftSJ[i][0]=in.shiftSJ[i][0]; t.shiftSJ[i][1]=in.shiftSJ[i][1];
        t.canonSJ[i]=in.canonSJ[i]; t.sjAnnot[i]=in.sjAnnot[i]; t.sjStr[i]=in.sjStr[i];
    }
    for(int i=0;i<3;++i) t.intronMotifs[i]=in.intronMotifs[i];
    t.sjMotifStrand=in.sjMotifStrand; t.sjYes=in.sjYes; t.nExons=in.nExons; t.iFrag=in.iFrag;
    t.rStart=in.rStart; t.roStart=in.roStart; t.rLength=in.rLength; t.gStart=in.gStart; t.gLength=in.gLength;
    t.Chr=in.Chr; t.Str=in.Str; t.roStr=in.roStr;
    t.nMatch=in.nMatch; t.nMM=in.nMM; t.mappedLength=in.mappedLength; t.extendL=in.extendL; t.maxScore=in.maxScore;
    t.nGap=in.nGap; t.lGap=in.lGap; t.nDel=in.nDel; t.nIns=in.nIns; t.lDel=in.lDel; t.lIns=in.lIns;
    t.nUnique=in.nUnique; t.nAnchor=in.nAnchor;
}
void fromCore(const Tr& t, Transcript& out) {
    const u64 n=t.nExons<maxExons ? t.nExons : maxExons;
    for(u64 i=0;i<n;++i) {
        for(unsigned k=0;k<exSize;++k) out.exons[i][k]=t.exons[i][k];
        out.shiftSJ[i][0]=t.shiftSJ[i][0]; out.shiftSJ[i][1]=t.shiftSJ[i][1];
        out.canonSJ[i]=t.canonSJ[i]; out.sjAnnot[i]=t.sjAnnot[i]; out.sjStr[i]=t.sjStr[i];
    }
    for(int i=0;i<3;++i) out.intronMotifs[i]=t.intronMotifs[i];
    out.sjMotifStrand=t.sjMotifStrand; out.sjYes=t.sjYes; out.nExons=t.nExons; out.iFrag=t.iFrag;
    out.rStart=t.rStart; out.roStart=t.roStart; out.rLength=t.rLength; out.gStart=t.gStart; out.gLength=t.gLength;
    out.Chr=t.Chr; out.Str=t.Str; out.roStr=t.roStr;
    out.nMatch=t.nMatch; out.nMM=t.nMM; out.mappedLength=t.mappedLength; out.extendL=t.extendL; out.maxScore=t.maxScore;
    out.nGap=t.nGap; out.lGap=t.lGap; out.nDel=t.nDel; out.nIns=t.nIns; out.lDel=t.lDel; out.lIns=t.lIns;
    out.nUnique=t.nUnique; out.nAnchor=t.nAnchor;
}
void fromPacked(const TrHead& h, const TrRow* rowBase, Transcript& out) {
    const u64 n=h.nExons<maxExons ? h.nExons : maxExons;
    const TrRow* rows=rowBase+h.row;
    for(u64 i=0;i<n;++i) {
        const TrRow& r=rows[i];
        for(unsigned k=0;k<exSize;++k) out.exons[i][k]=r.exon[k];
        out.shiftSJ[i][0]=r.shiftSJ[0]; out.shiftSJ[i][1]=r.shiftSJ[1];
        out.canonSJ[i]=r.canonSJ; out.sjAnnot[i]=r.sjAnnot; out.sjStr[i]=r.sjStr;
    }
    for(int i=0;i<3;++i) out.intronMotifs[i]=h.intronMotifs[i];
    out.sjMotifStrand=h.sjMotifStrand; out.sjYes=h.sjYes; out.nExons=h.nExons; out.iFrag=h.iFrag;
    out.rStart=h.rStart; out.roStart=h.roStart; out.rLength=h.rLength; out.gStart=h.gStart; out.gLength=h.gLength;
    out.Chr=h.Chr; out.Str=h.Str; out.roStr=h.roStr;
    out.nMatch=h.nMatch; out.nMM=h.nMM; out.mappedLength=h.mappedLength; out.extendL=h.extendL; out.maxScore=h.maxScore;
    out.nGap=h.nGap; out.lGap=h.lGap; out.nDel=h.nDel; out.nIns=h.nIns; out.lDel=h.lDel; out.lIns=h.lIns;
    out.nUnique=h.nUnique; out.nAnchor=h.nAnchor;
}
std::string compare(const Tr& t, const Transcript& c) {
    std::ostringstream d;
#define STITCH_CMP(a,b,name) if((a)!=(b)) d<<name<<": core="<<(a)<<" cpu="<<(b)<<"; ";
    STITCH_CMP(t.nExons,c.nExons,"nExons")
    if(t.nExons==c.nExons) {
        const u64 n=t.nExons<maxExons ? t.nExons : maxExons;
        for(u64 i=0;i<n;++i) {
            for(unsigned k=0;k<exSize;++k) STITCH_CMP(t.exons[i][k],c.exons[i][k],"exons["<<i<<"]["<<k<<"]")
            if(i+1<n) {
                // shiftSJ is written only for junctions and deletions (canonSJ>=-1);
                // insertion (-2) and mate-gap (-3) rows keep unread, stale values.
                if(c.canonSJ[i]>=-1) {
                    STITCH_CMP(t.shiftSJ[i][0],c.shiftSJ[i][0],"shiftSJ0["<<i<<"]")
                    STITCH_CMP(t.shiftSJ[i][1],c.shiftSJ[i][1],"shiftSJ1["<<i<<"]")
                }
                STITCH_CMP(t.canonSJ[i],c.canonSJ[i],"canonSJ["<<i<<"]")
                STITCH_CMP(int(t.sjAnnot[i]),int(c.sjAnnot[i]),"sjAnnot["<<i<<"]")
                STITCH_CMP(int(t.sjStr[i]),int(c.sjStr[i]),"sjStr["<<i<<"]")
            }
        }
    }
    for(int i=0;i<3;++i) STITCH_CMP(t.intronMotifs[i],c.intronMotifs[i],"intronMotifs["<<i<<"]")
    STITCH_CMP(int(t.sjMotifStrand),int(c.sjMotifStrand),"sjMotifStrand") STITCH_CMP(t.sjYes,c.sjYes,"sjYes")
    STITCH_CMP(t.iFrag,c.iFrag,"iFrag") STITCH_CMP(t.rStart,c.rStart,"rStart") STITCH_CMP(t.roStart,c.roStart,"roStart")
    STITCH_CMP(t.rLength,c.rLength,"rLength") STITCH_CMP(t.gStart,c.gStart,"gStart") STITCH_CMP(t.gLength,c.gLength,"gLength")
    STITCH_CMP(t.Chr,c.Chr,"Chr") STITCH_CMP(t.Str,c.Str,"Str") STITCH_CMP(t.roStr,c.roStr,"roStr")
    STITCH_CMP(t.nMatch,c.nMatch,"nMatch") STITCH_CMP(t.nMM,c.nMM,"nMM") STITCH_CMP(t.mappedLength,c.mappedLength,"mappedLength")
    STITCH_CMP(t.extendL,c.extendL,"extendL") STITCH_CMP(t.maxScore,c.maxScore,"maxScore")
    STITCH_CMP(t.nGap,c.nGap,"nGap") STITCH_CMP(t.lGap,c.lGap,"lGap") STITCH_CMP(t.nDel,c.nDel,"nDel") STITCH_CMP(t.nIns,c.nIns,"nIns")
    STITCH_CMP(t.lDel,c.lDel,"lDel") STITCH_CMP(t.lIns,c.lIns,"lIns") STITCH_CMP(t.nUnique,c.nUnique,"nUnique") STITCH_CMP(t.nAnchor,c.nAnchor,"nAnchor")
#undef STITCH_CMP
    return d.str();
}
bool verifyEnabled() {
    static const bool enabled=[]{const char* v=std::getenv("STAR_STITCH_VERIFY"); return v && std::string(v)=="1";}();
    return enabled;
}
void verifyWindow(const Parameters& P, const Genome& g, const uint64 (*WA)[WA_SIZE], uint64 nA,
                  const Transcript& trA, const char* R, uint64 Lread, const uint64* readLength,
                  uint64 mismatchLimit, const intScore* mateBefore, const intScore* mateAfter,
                  Transcript* const* wTr, uint64 nWinTr, uint64 readId) {
    thread_local const Parameters* cachedP=nullptr; thread_local Params params;
    thread_local const Genome* cachedG=nullptr; thread_local GenomeView view;
    if(cachedP!=&P) {params=paramsFrom(P); cachedP=&P;}
    if(cachedG!=&g) {view=genomeFrom(g); cachedG=&g;}
    thread_local std::vector<Frame> stack; thread_local std::vector<Tr> slots; thread_local std::vector<Tr*> pointers;
    stack.resize(nA+1); slots.resize(params.transcriptsPerWindowNmax+1); pointers.resize(slots.size());
    for(size_t i=0;i<slots.size();++i) pointers[i]=&slots[i];
    std::vector<u64> rows(nA*waSize);
    for(u64 i=0;i<nA;++i) for(unsigned k=0;k<waSize;++k) rows[i*waSize+k]=WA[i][k];
    Tr init; toCore(trA,init);
    copyTr(slots[0],init);
    ReadView rv; rv.R=R; rv.Lread=Lread; rv.mismatchLimit=mismatchLimit;
    for(unsigned i=0;i<maxMates;++i) rv.readLength[i]=readLength[i];
    int mates[maxMates]; for(unsigned i=0;i<maxMates;++i) mates[i]=mateBefore[i];
    WindowOut w{pointers.data(),0,pointers.size(),false};
    std::ostringstream err;
    if(!stitchWindow(rows.data(),nA,init,params,rv,view,stack.data(),stack.size(),w,mates)) err<<"stack overflow; ";
    if(w.overflow) err<<"slot overflow; ";
    if(w.n!=nWinTr) err<<"nWinTr core="<<w.n<<" cpu="<<nWinTr<<"; ";
    for(unsigned i=0;i<maxMates;++i) if(mates[i]!=mateAfter[i]) err<<"maxScoreMate["<<i<<"] core="<<mates[i]<<" cpu="<<mateAfter[i]<<"; ";
    for(u64 i=0;i<w.n && i<nWinTr;++i) {
        const auto diff=compare(*w.slot[i],*wTr[i]);
        if(!diff.empty()) err<<"transcript "<<i<<": "<<diff;
    }
    if(!err.str().empty()) {
        std::ostringstream msg; msg<<"STAR_STITCH_VERIFY mismatch read="<<readId<<" nA="<<nA<<": "<<err.str();
        std::cerr<<msg.str()<<std::endl; std::abort();
    }
}
[[noreturn]] static void verifyFailed(const std::string& what) {
    std::cerr<<what<<std::endl; std::abort();
}
void verifyDevice(const ::StitchWindowResult& w, const ::StitchResults& results, uint64 resultBase, Transcript* const* wTr,
                  uint64 nWinTr, uint64 readId) {
    std::ostringstream err;
    if(w.count!=nWinTr) err<<"nWinTr device="<<w.count<<" cpu="<<nWinTr<<"; ";
    for(u64 i=0;i<w.count && i<nWinTr;++i) {
        Tr t; results.get(resultBase+w.first+i,t);
        const auto diff=compare(t,*wTr[i]);
        if(!diff.empty()) {
            err<<"transcript "<<i<<": "<<diff<<" nExons="<<t.nExons<<" span=";
            if(t.nExons>0) err<<(t.exons[t.nExons-1][eG]+t.exons[t.nExons-1][eL]-t.exons[0][eG]);
            err<<" exons=";
            for(u64 e=0;e<t.nExons && e<maxExons;++e) err<<"("<<t.exons[e][eR]<<","<<t.exons[e][eG]<<","<<t.exons[e][eL]<<","<<t.canonSJ[e]<<","<<int(t.sjAnnot[e])<<")";
            err<<"; ";
        }
    }
    if(!err.str().empty()) {
        std::ostringstream msg; msg<<"STAR_STITCH_VERIFY device mismatch read="<<readId<<": "<<err.str();
        verifyFailed(msg.str());
    }
}
void verifyMate(int device, intScore cpu, uint64 mate, uint64 readId) {
    if(device!=cpu) {
        std::ostringstream msg; msg<<"STAR_STITCH_VERIFY device maxScoreMate["<<mate<<"] mismatch read="<<readId
            <<": device="<<device<<" cpu="<<cpu;
        verifyFailed(msg.str());
    }
}
} // namespace stitchcore
#endif // COMPILE_FOR_LONG_READS
