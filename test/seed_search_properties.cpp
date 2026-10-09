#include "SuffixArrayFuns.h"
#include "SeedTestIndex.h"
#include "SeedSearch.h"
#include <random>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) {
    if(!condition) throw std::runtime_error(message);
}

struct Fixture {
    SeedTestIndex genome;
    std::vector<char> storage;
    Fixture(const std::vector<char>& sequence,unsigned strandBit,uint records):
        storage(sequence.size()+128,5) {
        std::copy(sequence.begin(),sequence.end(),storage.begin()+64);
        genome.G=storage.data()+64;genome.nGenome=sequence.size();
        genome.GstrandBit=static_cast<unsigned char>(strandBit);
        genome.GstrandMask=(1ULL<<strandBit)-1;genome.nSA=records;
        genome.SA.defineBits(strandBit+1,records);genome.SA.allocateArray();
        std::memset(genome.SA.charArray,0,genome.SA.lengthByte);
    }
};

struct Query {
    std::vector<char> sequence,complement;
    char* strands[2];
    explicit Query(const std::vector<char>& codes):sequence(codes.size()+64,5),complement(codes.size()+64,5) {
        std::copy(codes.begin(),codes.end(),sequence.begin()+32);
        for(size_t i=0;i<codes.size();++i) complement[32+i]=codes[i]<4?3-codes[i]:codes[i];
        strands[0]=sequence.data()+32;strands[1]=complement.data()+32;
    }
};

std::vector<std::vector<char>> queries() {
    std::vector<std::vector<char>> result{{}};
    for(unsigned length=1;length<=4;++length) {
        const uint64_t count=1ULL<<(2*length);
        for(uint64_t key=0;key<count;++key) {
            std::vector<char> codes(length);
            for(unsigned i=0;i<length;++i) codes[length-i-1]=(key>>(2*i))&3;
            result.push_back(std::move(codes));
        }
    }
    for(const auto& codes:std::vector<std::vector<char>>{{4},{5},{0,4,1},{3,5,2},{4,4,5,5},{0,1,2,3,4,5}})
        result.push_back(codes);
    result.push_back(std::vector<char>(32,0));result.push_back(std::vector<char>(32,5));
    return result;
}

uint64_t singletonProperties(const std::vector<std::vector<char>>& inputs) {
    std::mt19937_64 rng(1729);uint64_t checked=0;
    for(unsigned bit:{8U,9U,17U,31U,55U}) {
        std::vector<char> sequence(47);
        for(auto& code:sequence) code=rng()%6;
        Fixture f(sequence,bit,32);
        for(uint i=0;i<f.genome.nSA;++i) {
            const uint position=i<2?(i?sequence.size()-1:0):rng()%sequence.size();
            f.genome.SA.writePacked(i,position|((i%2)<<bit));
        }
        for(const auto& codes:inputs) {
            Query q(codes);
            for(bool forward:{true,false}) {
                const uint start=forward||codes.empty()?0:codes.size()-1;
                for(uint index=0;index<f.genome.nSA;++index) {
                    bool order=false;
                    const uint oracle=compareSeqToGenome(f.genome,q.strands,start,codes.size(),0,index,forward,order);
                    // Every valid known prefix, including N==L and zero-length tails.
                    for(uint initial=0;initial<=oracle;++initial) {
                        uint length=initial,range[2]={~0ULL,~0ULL};
                        const uint count=maxMappableLength(f.genome,q.strands,start,codes.size(),index,index,forward,length,range);
                        require(length==oracle,"singleton matched length changed");
                        require(range[0]==index && range[1]==index && count==1,"singleton interval or multiplicity changed");
                        ++checked;
                    }
                }
            }
        }
    }
    return checked;
}

// Independent literal LCP oracle for sorted forward suffixes; no binary search.
uint literalPrefix(const Fixture& f,const std::vector<char>& query,uint position) {
    uint length=0;
    while(length<query.size() && query[length]==f.genome.G[position+length]) ++length;
    return length;
}

uint64_t multiRecordProperties(const std::vector<std::vector<char>>& inputs) {
    uint64_t checked=0;std::mt19937_64 rng(816);
    for(unsigned trial=0;trial<12;++trial) {
        std::vector<char> sequence(15);
        for(auto& code:sequence) code=trial==0?0:rng()%4;
        Fixture f(sequence,8,sequence.size());
        std::vector<uint> suffixes(sequence.size());std::iota(suffixes.begin(),suffixes.end(),0);
        std::sort(suffixes.begin(),suffixes.end(),[&](uint a,uint b) {
            while(a<sequence.size() && b<sequence.size() && sequence[a]==sequence[b]) {++a;++b;}
            // STAR's genome spacer sorts after A/C/G/T, including the genome end.
            return (a<sequence.size()?sequence[a]:5)<(b<sequence.size()?sequence[b]:5);
        });
        for(uint i=0;i<suffixes.size();++i) f.genome.SA.writePacked(i,suffixes[i]);
        for(const auto& codes:inputs) {
            if(std::any_of(codes.begin(),codes.end(),[](char c){return c>3;})) continue;
            Query q(codes);std::vector<uint> lengths;
            for(uint suffix:suffixes) lengths.push_back(literalPrefix(f,codes,suffix));
            const uint best=*std::max_element(lengths.begin(),lengths.end());
            const uint first=std::find(lengths.begin(),lengths.end(),best)-lengths.begin();
            const uint last=lengths.size()-1-(std::find(lengths.rbegin(),lengths.rend(),best)-lengths.rbegin());
            require(std::all_of(lengths.begin()+first,lengths.begin()+last+1,[&](uint n){return n==best;}),"oracle's longest-prefix interval is not contiguous");
            const uint known=std::min(lengths.front(),lengths.back());
            for(uint initial=0;initial<=known;++initial) {
                uint length=initial,range[2]={~0ULL,~0ULL};
                const uint count=maxMappableLength(f.genome,q.strands,0,codes.size(),0,suffixes.size()-1,true,length,range);
                require(length==best && range[0]==first && range[1]==last && count==last-first+1,
                        "sorted forward search differs from exhaustive literal suffix oracle");
                ++checked;
                #ifdef STAR_SEED_HINT_REPLAY
                for(uint probe=0;probe<=suffixes.size();++probe) {
                    length=initial;
                    const uint hinted=maxMappableLengthHint(f.genome,q.strands,0,codes.size(),0,suffixes.size()-1,
                                                            true,length,range,probe);
                    require(length==best && range[0]==first && range[1]==last && hinted==count,
                            "preferred probe changed exact longest-match interval");
                    ++checked;
                }
                #endif
            }
        }
    }
    return checked;
}
}

int main() try {
    Fixture batchFixture({0,1,2,3,4,5,0,1},8,2);
    batchFixture.genome.SA.writePacked(0,0);
    batchFixture.genome.SA.writePacked(1,1ULL<<8);
    Query batchRead({0,1,2,3,4,5});
    std::vector<SeedQuery> batchQueries;
    std::vector<SeedMatch> expected;
    for (bool forward:{true,false}) for (uint rank=0;rank<2;++rank) {
        const uint start=forward?0:5;
        batchQueries.push_back({0,6,start,6,rank,rank,0,uint64_t(forward)});
        uint length=0,range[2];
        const uint count=maxMappableLength(batchFixture.genome,batchRead.strands,start,6,rank,rank,forward,length,range);
        expected.push_back({length,range[0],range[1],count});
    }
    const SeedSearchEngine engine(batchFixture.genome);
    std::vector<SeedMatch> output(expected.size());
    for (int threads:{1,2}) {
        engine.searchBatch(batchRead.strands[0],batchRead.strands[1],6,batchQueries.data(),batchQueries.size(),output.data(),threads);
        for (size_t i=0;i<expected.size();++i)
            require(output[i].length==expected[i].length && output[i].lower==expected[i].lower
                && output[i].upper==expected[i].upper && output[i].multiplicity==expected[i].multiplicity,
                "CPU batch changed order or exact search fields");
    }
    engine.searchBatch(nullptr,nullptr,0,nullptr,0,nullptr);
    batchQueries.back().offset=7;
    output.assign(output.size(),SeedMatch{99,99,99,99});
    bool rejected=false;
    try { engine.searchBatch(batchRead.strands[0],batchRead.strands[1],6,batchQueries.data(),batchQueries.size(),output.data(),2); }
    catch (const std::invalid_argument&) { rejected=true; }
    require(rejected && output.front().length==99,"invalid tail batch partially committed results");
    const auto inputs=queries();
    const auto singleton=singletonProperties(inputs),multiple=multiRecordProperties(inputs);
    std::cout<<"{\"status\":\"PASS\",\"singleton_checks\":"<<singleton<<",\"sorted_forward_checks\":"<<multiple<<"}\n";
    return 0;
} catch(const std::exception& error) {
    std::cerr<<"FAIL: "<<error.what()<<"\n";return 1;
}
