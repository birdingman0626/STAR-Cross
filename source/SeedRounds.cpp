#include "SeedRounds.h"
#include "SequenceFuns.h"
#include <array>

namespace {
struct Search {
    SeedQuery query;
    SeedRoundTask::promise_type* promise=nullptr;
    bool await_ready() const noexcept { return false; }
    void await_suspend(SeedRoundTask::Handle h) noexcept {
        promise=&h.promise(); promise->query=query;
    }
    SeedMatch await_resume() const noexcept { return promise->match; }
};
}

SeedRoundTask seedRounds(std::span<const char> read, uint64 offset,
                         const SeedRoundIndex& index, const SeedRoundConfig& config,
                         std::vector<SeedEmission>& output, size_t& batchEvents) {
    if(read.empty()) co_return;
    if(!config.maxSplits || !index.sparse || !index.prefixBases)
        throw std::invalid_argument("invalid adaptive seed geometry");
    std::array<std::vector<uint64>,3> splits;
    for(auto& v:splits) v.resize(config.maxSplits);
    uint64* split[3]={splits[0].data(),splits[1].data(),splits[2].data()};
    // qualitySplit only reads the sequence; the legacy API predates const views.
    const uint64 count=qualitySplit(const_cast<char*>(read.data()),read.size(),config.maxSplits,config.minSplit,split);
    if(!count) co_return;
    const uint64 startMax=std::min(config.startMax,uint64(config.startFraction*(read.size()-1)));
    if(config.startMax && !startMax) throw std::invalid_argument("seed start stride rounded to zero");
    for(uint64 ip=0;ip<count;++ip) {
        const uint64 starts=config.startMax>0 && startMax<split[1][ip] ? split[1][ip]/startMax+1 : 1;
        const uint64 step=split[1][ip]/starts;
        bool forwardNeeded=true;
        for(uint64 direction=0;direction<2;++direction) {
            for(uint64 attempt=0;attempt<starts;++attempt) {
                uint64 mapped=0;
                for(unsigned phase=0;phase<2;++phase) {
                    if((phase==0 && !forwardNeeded && attempt==0) || (phase==1 && !config.fixedLength)) continue;
                    while(phase==1 || attempt*step+mapped+config.minMap<split[1][ip]) {
                        const uint64 used=attempt*step+(phase==0 ? mapped : 0);
                        const uint64 start=direction==0 ? split[0][ip]+used : split[0][ip]+split[1][ip]-1-used;
                        const uint64 length=phase==0 ? split[1][ip]-used
                            : std::min(config.fixedLength,direction==0 ? split[0][ip]+split[1][ip]-start : start+1);
                        const uint64 distances=std::min(length,index.sparse);
                        std::vector<SeedMatch> matches;
                        if(distances>1) matches.resize(distances);
                        SeedMatch single{};
                        uint64 best=0;
                        for(uint64 distance=0;distance<distances;++distance) {
                            const uint64 position=direction==0 ? start+distance : start-distance;
                            const uint64 remaining=length-distance;
                            const uint64 prefixLength=std::min(index.prefixBases,remaining);
                            uint64 prefix=0;
                            for(uint64 j=0;j<prefixLength;++j)
                                prefix=(prefix<<2)+(direction==0 ? uint64(read[position+j]) : 3-uint64(read[position-j]));
                            uint64 matched=prefixLength,lower=0;
                            while(matched>0) {
                                lower=index.prefixes[index.starts[matched-1]+prefix];
                                if(!(lower & index.absentFlag)) break;
                                --matched; prefix>>=2;
                            }
                            // Absent first bases have no prefix interval. The exact full
                            // search is the safe definition; never index starts[-1].
                            bool goodUpper=false;
                            uint64 upper=index.search.nSA-1;
                            if(matched && index.starts[matched-1]+prefix+1<index.starts[matched]) {
                                upper=index.prefixes[index.starts[matched-1]+prefix+1];
                                goodUpper=!(upper & index.absentFlag);
                                upper=goodUpper ? (upper & index.nMask)-1 : index.search.nSA-1;
                            }
                            const bool noN=!(lower & index.nFlag);
                            SeedMatch match;
                            if(matched && matched<index.prefixBases && noN && goodUpper) {
                                match={matched,lower,upper,upper-lower+1};
                            } else {
                                match=co_await Search{{offset,read.size(),position,remaining,
                                    matched ? lower & index.nMask : 0,upper,
                                    matched && goodUpper && noN ? matched : 0,uint64(direction==0)}};
                            }
                            best=std::max(best,match.length+distance);
                            if(distances==1) single=match; else matches[distance]=match;
                        }
                        for(uint64 distance=0;distance<distances;++distance) {
                            const auto match=distances==1 ? single : matches[distance];
                            if(match.length+distance!=best) continue;
                            // Hard cap on auxiliary results, not a heuristic seed filter.
                            // Reject the experiment instead of silently dropping candidates.
                            if(batchEvents>=131072) throw std::runtime_error("seed batch event budget exceeded; reduce seedBatchSize");
                            ++batchEvents;
                            output.push_back({direction,direction==0 ? start+distance : start-distance,split[2][ip],match});
                        }
                        if(phase==1) break;
                        if(!best) throw std::runtime_error("adaptive seed search made no progress");
                        if(direction==0 && attempt==0 && mapped==0 && start+best==split[1][ip]) forwardNeeded=false;
                        mapped+=best;
                    }
                }
            }
        }
    }
}
