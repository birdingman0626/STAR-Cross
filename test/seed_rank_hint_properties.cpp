#include "SeedRankHint.h"
#include "SeedTestIndex.h"
#include <random>

namespace {
void require(bool condition,const char* message) {if(!condition) throw std::runtime_error(message);}
struct Fixture {
    SeedTestIndex g;std::vector<char> sequence;
    explicit Fixture(bool repeated=false):sequence(256) {
        std::mt19937_64 rng(1729);for(auto& code:sequence) code=repeated?0:rng()%4;
        g.G=sequence.data();g.nGenome=sequence.size();g.GstrandBit=10;g.GstrandMask=(1ULL<<10)-1;
        std::vector<uint> suffixes;
        for(uint i=0;i<sequence.size();++i) {suffixes.push_back(i);suffixes.push_back(i|(1ULL<<10));}
        auto base=[&](uint record,uint offset) {
            const uint position=record&g.GstrandMask;
            if(offset>=g.nGenome-position) return 5;
            return (record>>g.GstrandBit)?3-int(sequence[g.nGenome-1-position-offset]):int(sequence[position+offset]);
        };
        std::sort(suffixes.begin(),suffixes.end(),[&](uint a,uint b) {
            for(uint j=0;j<=g.nGenome;++j) {
                const int left=base(a,j),right=base(b,j);
                if(left!=right) return left<right;
                if(left==5) return a<b;
            }
            throw std::runtime_error("unterminated fixture suffix");
        });
        g.nSA=suffixes.size();g.SA.defineBits(11,g.nSA);g.SA.allocateArray();
        std::memset(g.SA.charArray,0,g.SA.lengthByte);
        for(uint i=0;i<g.nSA;++i) g.SA.writePacked(i,suffixes[i]);
    }
    std::vector<char> keySequence(uint rank,unsigned k) {
        const uint record=g.SA[rank],position=record&g.GstrandMask;
        if(k>g.nGenome-position) return {};
        std::vector<char> codes;
        for(unsigned j=0;j<k;++j) codes.push_back(record>>g.GstrandBit?3-sequence[g.nGenome-1-position-j]:sequence[position+j]);
        return codes;
    }
};

uint64_t check(Fixture& f) {
    uint64_t tests=0;
    for(const std::string& kind:{"pwl","pla"}) for(unsigned k:{14U,18U,21U}) for(uint64_t error:{16ULL,64ULL,256ULL}) {
        SeedRankHint model(f.g,kind,k,error);
        require(model.modelBytes()<=64*1024*1024,"predictor memory budget exceeded");
        require(model.validSamples==model.uniqueKeys()+model.duplicateSamples,"sample accounting mismatch");
        require(model.sampledRanks==f.g.nSA,"small fixture did not sample all ranks");
        uint64_t usable=0;
        for(uint index=0;index<f.g.nSA;++index) {
            const auto canonical=f.keySequence(index,k);if(canonical.empty()) continue;
            uint firstProbe=0;
            for(bool forward:{true,false}) {
                auto original=canonical;
                if(!forward) {std::reverse(original.begin(),original.end());for(auto& c:original) c=3-c;}
                std::vector<char> complement=original;for(auto& c:complement) c=3-c;
                char* reads[2]={original.data(),complement.data()};
                GpuSeedQuery query{0,k,forward?0:k-1,k,0,f.g.nSA-1,0,uint64_t(forward)};
                uint probe=0;
                const bool supported=model.probe(query,reads,probe);
                if(supported) {
                    require(probe>query.lower && probe<query.upper,"predictor did not preserve strict eligible bounds");
                    ++usable;
                    if(forward) firstProbe=probe;else require(probe==firstProbe,"read-strand key canonicalization differs");
                    query.lower=probe;
                    require(!model.probe(query,reads,probe),"boundary prediction did not fall back");
                }
                query={0,k,forward?0:k-1,k-1,0,f.g.nSA-1,0,uint64_t(forward)};
                require(!model.probe(query,reads,probe),"short-query fallback missing");
                query.length=k;original[forward?0:k-1]=4;complement[forward?0:k-1]=4;
                require(!model.probe(query,reads,probe),"ambiguous-query fallback missing");
                ++tests;
            }
        }
        // Repeated prefixes may predict a boundary for every key; safe fallback is permitted.
        if(model.uniqueKeys()>2) require(usable>0,"random fixture never exercises a usable rank hint");
    }
    return tests;
}

void rejectionChecks() {
    Fixture f;
    for(unsigned invalid:{0U,1U,32U,1000U}) {
        bool rejected=false;try {SeedRankHint model(f.g,"pla",invalid,64);}catch(const std::invalid_argument&) {rejected=true;}
        require(rejected,"invalid width was accepted");
    }
    uint first=0,last=f.g.nSA-1;
    while(f.keySequence(first,21).empty()) ++first;
    while(f.keySequence(last,21).empty()) --last;
    const uint a=f.g.SA[first],b=f.g.SA[last];f.g.SA.writePacked(first,b);f.g.SA.writePacked(last,a);
    bool rejected=false;try {SeedRankHint model(f.g,"pla",21,64);}catch(const std::runtime_error&) {rejected=true;}
    require(rejected,"nonmonotone sampled keys were accepted");
}
}
int main() try {
    Fixture random,repeated(true);const auto tests=check(random)+check(repeated);rejectionChecks();
    std::cout<<"{\"status\":\"PASS\",\"predictor_checks\":"<<tests<<"}\n";return 0;
} catch(const std::exception& error) {std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;}
