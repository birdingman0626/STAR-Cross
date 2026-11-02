// Implementation private to gpuSeedSearch.cu. A thread owns one read's state;
// only the bounded event arena and diagnostic counter are shared between reads.
struct AdaptiveIndex {
    const unsigned char* prefixes=nullptr;
    U starts[32]{};
    U sparse=0,absentFlag=0,nFlag=0,nMask=0;
    unsigned prefixBits=0,prefixBases=0;
};
struct SeedNode { SeedEmission emission; unsigned next; };
struct AdaptiveCounts { unsigned used,error; unsigned long long searches; };
constexpr unsigned seedListEnd=~0u;
__device__ U lesser(U a,U b) {return a<b ? a:b;}
__device__ U prefixWord(AdaptiveIndex index,U i) {
    U bit=i*index.prefixBits,value=0;
    for(unsigned k=0;k<8;++k) value|=U(index.prefixes[bit/8+k])<<(8*k);
    return (value>>(bit%8))&((U(1)<<index.prefixBits)-1);
}
__device__ SeedMatch adaptiveSearch(Index index,AdaptiveIndex prefix,const unsigned char* reads,
        SeedReadRange range,U direction,U start,U length,U distance,U& searches) {
    const U position=direction==0 ? start+distance : start-distance;
    const U remaining=length-distance;
    const auto* read=reads+range.offset;
    U matched=lesser(prefix.prefixBases,remaining),key=0;
    for(U j=0;j<matched;++j)
        key=(key<<2)+(direction==0 ? U(read[position+j]) : 3-U(read[position-j]));
    U lower=0;
    while(matched>0) {
        lower=prefixWord(prefix,prefix.starts[matched-1]+key);
        if(!(lower & prefix.absentFlag)) break;
        --matched;key>>=2;
    }
    bool goodUpper=false;
    U upper=index.records-1;
    if(matched && prefix.starts[matched-1]+key+1<prefix.starts[matched]) {
        upper=prefixWord(prefix,prefix.starts[matched-1]+key+1);
        goodUpper=!(upper & prefix.absentFlag);
        upper=goodUpper ? (upper & prefix.nMask)-1 : index.records-1;
    }
    const bool noN=!(lower & prefix.nFlag);
    if(matched && matched<prefix.prefixBases && noN && goodUpper)
        return {matched,lower,upper,upper-lower+1};
    ++searches;
    return searchOne(index,reads,{range.offset,range.length,position,remaining,
        matched ? lower & prefix.nMask : 0,upper,
        matched && goodUpper && noN ? matched : 0,U(direction==0)});
}
__device__ U seedStart(const SeedEmission& seed) {
    return seed.direction==0 ? seed.start : seed.start+1-seed.match.length;
}
__device__ bool appendSeed(SeedNode* nodes,AdaptiveCounts* counts,unsigned capacity,
        unsigned* head,SeedSummary& summary,SeedRoundConfig config,SeedEmission emission) {
    const U repeats=emission.match.multiplicity,length=emission.match.length;
    if(repeats>config.maxMultiplicity) {
        if(repeats<summary.multNmin || !summary.multNmin) {summary.multNmin=repeats;summary.multNminL=length;}
        return true;
    }
    summary.alignments+=repeats;
    if(repeats==1) summary.unique+=repeats;else summary.multiple+=repeats;
    const U start=seedStart(emission);
    unsigned previous=seedListEnd,next=*head;
    while(next!=seedListEnd) {
        const auto& stored=nodes[next].emission;
        const U storedStart=seedStart(stored);
        // storeAligns keys intentionally ignore strand/interval/fragment. Retain
        // its first winner and count duplicates before rejecting them.
        if(storedStart==start && stored.match.length==length) return true;
        if(storedStart>start || (storedStart==start && stored.match.length<length)) break;
        previous=next;next=nodes[next].next;
    }
    if(++summary.pieces>config.maxSeeds) {atomicOr(&counts->error,8u);return false;}
    const unsigned i=atomicAdd(&counts->used,1u);
    if(i>=capacity) {atomicOr(&counts->error,1u);return false;}
    nodes[i]={emission,next};
    if(previous==seedListEnd) *head=i;else nodes[previous].next=i;
    if(repeats==1) {
        if(length>summary.uniqLmax) {summary.uniqLmax=length;summary.uniqLmaxInd=summary.pieces-1;}
    } else {
        if(repeats<summary.multNmin || !summary.multNmin) {summary.multNmin=repeats;summary.multNminL=length;}
        if(length>summary.multLmax) {summary.multLmax=length;summary.multLmaxN=repeats;}
        if(repeats>summary.multNmax) {summary.multNmax=repeats;summary.multNmaxL=length;}
    }
    return true;
}
__device__ void adaptiveRead(Index index,AdaptiveIndex prefix,SeedRoundConfig config,
        const unsigned char* reads,SeedReadRange range,unsigned* head,
        SeedNode* nodes,AdaptiveCounts* counts,unsigned capacity,U& searches,SeedSummary& summary) {
    const auto* read=reads+range.offset;
    U cursor=0,splits=0,fragment=0;
    while(cursor<range.length && splits<config.maxSplits) {
        // Same ordered qualitySplit contract, without arrays or host resumes.
        while(cursor<range.length && read[cursor]>3) {
            if(read[cursor]==11) ++fragment; // MARK_FRAG_SPACER_BASE
            ++cursor;
        }
        if(cursor==range.length) break;
        const U splitStart=cursor;
        while(cursor<range.length && read[cursor]<=3) ++cursor;
        const U splitLength=cursor-splitStart;
        if(splitLength>summary.longestSplit) summary.longestSplit=splitLength;
        if(splitLength<config.minSplit) continue;
        summary.splits=++splits;
        const U startMax=lesser(config.startMax,U(config.startFraction*(range.length-1)));
        if(config.startMax && !startMax) {atomicOr(&counts->error,2u);return;}
        const U starts=config.startMax>0 && startMax<splitLength ? splitLength/startMax+1 : 1;
        const U step=splitLength/starts;
        bool forwardNeeded=true;
        for(U direction=0;direction<2;++direction) for(U attempt=0;attempt<starts;++attempt) {
            U mapped=0;
            for(unsigned phase=0;phase<2;++phase) {
                if((phase==0 && !forwardNeeded && attempt==0) || (phase==1 && !config.fixedLength)) continue;
                while(phase==1 || attempt*step+mapped+config.minMap<splitLength) {
                    const U used=attempt*step+(phase==0 ? mapped : 0);
                    const U start=direction==0 ? splitStart+used : splitStart+splitLength-1-used;
                    const U length=phase==0 ? splitLength-used : lesser(config.fixedLength,
                        direction==0 ? splitStart+splitLength-start : start+1);
                    const U distances=lesser(length,prefix.sparse);
                    U best=0;
                    SeedMatch single{};
                    for(U distance=0;distance<distances;++distance) {
                        const auto match=adaptiveSearch(index,prefix,reads,range,direction,start,length,distance,searches);
                        const U extent=match.length+distance;
                        if(extent>best) best=extent;
                        if(distances==1) single=match;
                    }
                    for(U distance=0;distance<distances;++distance) {
                        // Sparse indexes need tied best distances, in original order.
                        // Re-evaluation bounds scratch memory independently of sparse D.
                        const auto match=distances==1 ? single : adaptiveSearch(index,prefix,reads,range,direction,start,length,distance,searches);
                        if(match.length+distance!=best) continue;
                        if(!appendSeed(nodes,counts,capacity,head,summary,config,
                            {direction,direction==0 ? start+distance : start-distance,fragment,match})) return;
                    }
                    if(phase==1) break;
                    if(!best) {atomicOr(&counts->error,4u);return;}
                    if(direction==0 && attempt==0 && mapped==0 && start+best==splitLength) forwardNeeded=false;
                    mapped+=best;
                }
            }
        }
    }
}
__global__ void adaptiveKernel(Index index,AdaptiveIndex prefix,SeedRoundConfig config,
        const unsigned char* reads,const SeedReadRange* ranges,unsigned readCount,
        unsigned* heads,SeedNode* nodes,AdaptiveCounts* counts,unsigned capacity,SeedSummary* summaries) {
    const unsigned i=blockIdx.x*blockDim.x+threadIdx.x;
    if(i>=readCount) return;
    U searches=0;
    SeedSummary summary{};
    adaptiveRead(index,prefix,config,reads,ranges[i],heads+i,nodes,counts,capacity,searches,summary);
    summaries[i]=summary;
    atomicAdd(&counts->searches,static_cast<unsigned long long>(searches));
}
