#include "gpuSeedSearch.h"
#include <future>
#include <stdexcept>
#include <iostream>
#include <string>
#include <array>

void require(bool value,const char* message) {if(!value) throw std::runtime_error(message);}
void adaptiveSlots() {
    const char genome[]={0,1,2,3,4,5,0,1},sa[8]={};
    std::array<char,12> prefixes{};
    for(unsigned word=1;word<4;++word) { // Only A has a prefix interval.
        const unsigned bit=word*11+10;
        prefixes[bit/8]|=char(1u<<(bit%8));
    }
    const unsigned long long starts[]={0,4};
    const SeedRoundConfig config{10,1,1,0,0,1};
    GpuSeedIndex index(genome,sizeof(genome),sa,sizeof(sa),1,8,7,32,2);
    index.configureAdaptive(prefixes.data(),prefixes.size(),4,11,starts,1,1,1024,512,~uint64_t(512),config,7);
    const auto worker=[&](size_t slot) {
        for(unsigned iteration=0;iteration<64;++iteration) {
            const auto output=index.seedReads({0,1,2,3,0,1},{{0,4},{4,2},{6,0}},slot);
            require(output.reads.size()==3 && output.searches==2,"adaptive read count/search mismatch");
            for(size_t i=0;i<2;++i) {
                require(output.reads[i].size()==1,"adaptive event count mismatch");
                const auto& seed=output.reads[i][0];
                require(seed.direction==0 && seed.start==0 && seed.fragment==0 && seed.match.length==(i==0?4:2)
                    && seed.match.lower==0 && seed.match.upper==0 && seed.match.multiplicity==1,"adaptive event mismatch");
            }
            require(output.reads[2].empty(),"empty read produced seeds");
            for(size_t i=0;i<2;++i) {
                const auto& projected=output.projected[i];
                require(projected.offsets.size()==1 && projected.offsets[0]==0 && projected.hits.size()==1,
                    "projected hit cardinality mismatch");
                require(projected.hits[0].genome==0 && projected.hits[0].strand==0 && projected.hits[0].readStart==0,
                    "projected hit coordinates mismatch");
            }
            require(output.projected[2].hits.empty(),"empty read produced projected hits");
            require(output.summaries[0].splits==1 && output.summaries[0].longestSplit==4
                && output.summaries[0].pieces==1 && output.summaries[0].alignments==1
                && output.summaries[0].unique==1 && output.summaries[0].uniqLmax==4
                && output.summaries[2].pieces==0 && output.summaries[2].splits==0,"adaptive summary mismatch");
        }
    };
    auto first=std::async(std::launch::async,worker,0);
    auto second=std::async(std::launch::async,worker,1);
    first.get();second.get();
    require(index.seedReads({}, {},0).reads.empty(),"empty batch produced reads");
    bool rejected=false;
    try {index.seedReads({0},{{0,2}},0);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"invalid adaptive read range accepted");
    rejected=false;
    try {index.seedReads({1,2},{{0,2}},0);} catch(const std::runtime_error&) {rejected=true;}
    require(rejected,"non-progressing adaptive search accepted");
    GpuSeedIndex bounded(genome,sizeof(genome),sa,sizeof(sa),1,8,7,32);
    bounded.configureAdaptive(prefixes.data(),prefixes.size(),4,11,starts,1,1,1024,512,~uint64_t(512),config,1);
    rejected=false;
    try {bounded.seedReads({0,1},{{0,2},{0,2}},0);} catch(const std::runtime_error&) {rejected=true;}
    require(rejected,"event overflow silently truncated results");
    require(bounded.seedReads({0,1},{{0,2}},0).reads[0].size()==1,"overflow corrupted next batch");
    GpuSeedIndex boundedHits(genome,sizeof(genome),sa,sizeof(sa),1,8,7,32);
    boundedHits.configureAdaptive(prefixes.data(),prefixes.size(),4,11,starts,1,1,1024,512,~uint64_t(512),config,7,1);
    rejected=false;
    try {boundedHits.seedReads({0,1},{{0,2},{0,2}},0);} catch(const std::runtime_error& error) {
        rejected=std::string(error.what()).find("mask=16")!=std::string::npos;
    }
    require(rejected,"hit overflow silently truncated results");
    require(boundedHits.seedReads({0,1},{{0,2}},0).projected[0].hits.size()==1,"hit overflow corrupted next batch");
    const auto deferred=boundedHits.seedReads({0,1},{{0,2},{0,2}},0,true);
    const auto firstHits=boundedHits.projectPrepared(0,0,deferred.generation);
    const auto secondHits=boundedHits.projectPrepared(1,0,deferred.generation);
    require(firstHits.size()==1 && secondHits.size()==1 && firstHits[0].hits.size()==1 && secondHits[0].hits.size()==1,
        "bounded hit chunks lost reads");
    boundedHits.seedReads({0,1},{{0,2}},0);
    rejected=false;
    try {boundedHits.projectPrepared(1,0,deferred.generation);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"stale projection generation accepted");
    auto duplicateConfig=config;duplicateConfig.fixedLength=4;
    GpuSeedIndex duplicates(genome,sizeof(genome),sa,sizeof(sa),1,8,7,32);
    duplicates.configureAdaptive(prefixes.data(),prefixes.size(),4,11,starts,1,1,1024,512,~uint64_t(512),duplicateConfig,1);
    const auto duplicate=duplicates.seedReads({0,1,2,3},{{0,4}},0);
    require(duplicate.reads[0].size()==1 && duplicate.reads[0][0].direction==0
        && duplicate.summaries[0].alignments==3 && duplicate.summaries[0].unique==3
        && duplicate.summaries[0].uniqLmaxInd==0,"duplicate winner/count-before-rejection changed");
    auto rejectConfig=config;rejectConfig.maxMultiplicity=0;rejectConfig.minSplit=3;
    GpuSeedIndex rejectedSeeds(genome,sizeof(genome),sa,sizeof(sa),1,8,7,32);
    rejectedSeeds.configureAdaptive(prefixes.data(),prefixes.size(),4,11,starts,1,1,1024,512,~uint64_t(512),rejectConfig,1);
    const auto rejectedOutput=rejectedSeeds.seedReads({0,1,2,3,4,0,4,0,1,4},{{0,4},{4,6}},0);
    require(rejectedOutput.reads[0].empty() && rejectedOutput.summaries[0].alignments==0
        && rejectedOutput.summaries[0].multNmin==1 && rejectedOutput.summaries[0].multNminL==4,
        "multiplicity filtering lost rejected-seed statistics");
    require(rejectedOutput.summaries[1].splits==0 && rejectedOutput.summaries[1].longestSplit==2,
        "short split diagnostic changed");
    auto noSeedsConfig=config;noSeedsConfig.maxSeeds=0;
    GpuSeedIndex noSeeds(genome,sizeof(genome),sa,sizeof(sa),1,8,7,32);
    noSeeds.configureAdaptive(prefixes.data(),prefixes.size(),4,11,starts,1,1,1024,512,~uint64_t(512),noSeedsConfig,1);
    rejected=false;
    try {noSeeds.seedReads({0,1,2,3},{{0,4}},0);}
    catch(const std::runtime_error& error) {rejected=std::string(error.what()).find("mask=8")!=std::string::npos;}
    require(rejected,"per-read seed limit silently truncated results");
}
int main() try {
    adaptiveSlots();
    const char genome[]={0,1,2,3,4,5,0,1};
    const char sa[8]={}; // one nine-bit packed record, forward coordinate zero
    GpuSeedIndex index(genome,sizeof(genome),sa,sizeof(sa),1,8,7,32,2);
    const auto worker=[&](size_t slot) {
        for(unsigned iteration=0;iteration<64;++iteration) {
            // Different read pools on concurrent streams must never cross slots.
            std::vector<char> reads=slot ? std::vector<char>{0,1,3,3} : std::vector<char>{0,1,2,3};
            auto generation=index.uploadReads(reads,slot);
            std::vector<GpuSeedQuery> queries{{0,reads.size(),0,reads.size(),0,0,0,1}};
            GpuSeedTiming timing;
            auto matches=index.searchPrepared(queries,timing,slot,generation);
            require(matches.size()==1 && matches[0].length==(slot?2:4)
                && matches[0].lower==0 && matches[0].upper==0 && matches[0].multiplicity==1,"cross-slot seed mismatch");
            require(index.searchPrepared({},timing,slot,generation).empty(),"empty batch changed");
            index.uploadReads(reads,slot);
            bool rejected=false;
            try {index.searchPrepared(queries,timing,slot,generation);} catch(const std::invalid_argument&) {rejected=true;}
            require(rejected,"stale read pool generation accepted");
        }
    };
    auto first=std::async(std::launch::async,worker,0);
    auto second=std::async(std::launch::async,worker,1);
    first.get();second.get();
    bool rejected=false;
    try {index.uploadReads(std::vector<char>(33),0);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"oversized read pool accepted");
    rejected=false;
    try {index.uploadReads({0},2);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"missing slot accepted");
    std::cout<<"PASS: adaptive/query slots, consolidation, duplicate/filter/split counters, no progress, generation and capacity rejection\n";
    return 0;
} catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
