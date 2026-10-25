#include <doctest/doctest.h>
#include "ClipCR4.h"
#include <stdexcept>
#include <random>
#include <limits>
using namespace std;
// Independent scalar affine overlap recurrence. Free leading ends; choose the
// first best target endpoint on the last query row, then the last target column
// only on a strictly larger score (Opal's endpoint policy).
static ClipAlignResult overlap(const std::vector<uint8>& query, std::string target) {
    target.resize(91, 'N');
    std::vector<int> h(query.size()+1, 0), e(query.size()+1, -100000);
    int best=-100000, endpoint=-1, lastColumn=-100000;
    for (int c=0;c<91;++c) {
        int diagonal=h[0], above=0, f=-100000;
        for (size_t r=1;r<=query.size();++r) {
            int previous=h[r];
            e[r]=std::max(previous-2,e[r]-2);
            f=std::max(above-2,f-2);
            size_t base=std::string("ACGTN").find(target[c]);
            if(base==std::string::npos) base=4;
            int substitution=query[r-1]==base ? (base==4 ? 0:1):-2;
            h[r]=std::max({e[r],f,diagonal+substitution});
            diagonal=previous;above=h[r];
            if(c==90) lastColumn=std::max(lastColumn,h[r]);
        }
        if(h.back()>best) {best=h.back();endpoint=c;}
    }
    if(lastColumn>best) {best=lastColumn;endpoint=90;}
    return {best,endpoint};
}

TEST_CASE("CellRanger4 clipping preserves scalar overlap scores and endpoints") {
    ClipCR4 clip;
    std::mt19937 random(194);
    std::vector<uint8> query;
    for(char c:std::string("AAGCAGTGGTATCAACGCAGAGTACATGGG"))
        query.push_back(std::string("ACGTN").find(c));
    for(int iteration=0;iteration<1000;++iteration) {
        std::string sequence(iteration%110,'A');
        for(char& c:sequence) c="ACGTNX"[random()%6];
        // Include genuine adapter-rich reads, not only low-score negatives.
        if(iteration%3==0) sequence="AAGCAGTGGTATCAACGCAGAGTACATGGG"+sequence;
        clip.fillOneSeq(0,sequence.data(),sequence.size());
        clip.align(query.data(),query.size(),1);
        auto expected=overlap(query,sequence);
        CHECK(clip.alignRes[0].score==expected.score);
        CHECK(clip.alignRes[0].endLocationTarget==expected.endLocationTarget);
    }
}

TEST_CASE("CellRanger4 custom adapter longer than 128 bases is not truncated") {
    ClipCR4 clip;
    std::vector<uint8> query(150,0);
    for(size_t i=128;i<query.size();++i) query[i]=1;
    std::string sequence(22,'C');sequence+=std::string(69,'T');
    clip.fillOneSeq(0,sequence.data(),sequence.size());
    clip.align(query.data(),query.size(),1);
    auto expected=overlap(query,sequence);
    CHECK(clip.alignRes[0].score==expected.score);
    CHECK(clip.alignRes[0].endLocationTarget==expected.endLocationTarget);
}

TEST_CASE("CellRanger4 invalid inputs are rejected and unknown query bases normalize") {
    ClipCR4 clip;
    char sequence[] = "ACGT";
    uint8 query[] = {0, 1, 2, 3, 255};
    CHECK_THROWS_AS(clip.fillOneSeq(64, sequence, 4), std::invalid_argument);
    CHECK_THROWS_AS(clip.fillOneSeq(0, nullptr, 4), std::invalid_argument);
    CHECK_THROWS_AS(clip.align(query, 0, 1), std::invalid_argument);
    CHECK_THROWS_AS(clip.align(query, 5, 65), std::invalid_argument);
    CHECK_THROWS_AS(clip.align(nullptr, 5, 1), std::invalid_argument);
    clip.fillOneSeq(0, sequence, 4);
    clip.align(query, 5, 1);
    const auto unknown = clip.alignRes[0];
    query[4] = 4;
    clip.align(query, 5, 1);
    CHECK(clip.alignRes[0].score == unknown.score);
    CHECK(clip.alignRes[0].endLocationTarget == unknown.endLocationTarget);
}

static std::string randomRead(std::mt19937& random, size_t maxLen) {
    std::string sequence(random()%(maxLen+1),'A');
    for(char& c:sequence) c="ACGTNXacgt"[random()%10];
    if(random()%3==0) sequence="AAGCAGTGGTATCAACGCAGAGTACATGGG"+sequence;
    return sequence;
}

TEST_CASE("CellRanger4 clipping aligns all 64 database sequences independently") {
    ClipCR4 clip;
    std::mt19937 random(2026);
    for(int round=0;round<40;++round) {
        std::vector<uint8> query(1+random()%60);
        for(auto& b:query) b=random()%6; // includes N (4) and an out-of-range code
        std::vector<std::string> reads(clip.dbN);
        int used=1+random()%clip.dbN; // partial batches leave stale lanes behind
        for(int i=0;i<used;++i) {
            reads[i]=randomRead(random,120);
            clip.fillOneSeq(i,reads[i].data(),reads[i].size());
        }
        clip.align(query.data(),query.size(),used);
        std::vector<uint8> normalized(query);
        for(auto& b:normalized) b=std::min<uint8>(b,4);
        for(int i=0;i<used;++i) {
            std::string target=reads[i];
            for(char& c:target) if(std::string("ACGTN").find(c)==std::string::npos) c='N';
            auto expected=overlap(normalized,target.substr(0,std::min<size_t>(target.size(),91)));
            CHECK(clip.alignRes[i].score==expected.score);
            CHECK(clip.alignRes[i].endLocationTarget==expected.endLocationTarget);
        }
    }
}

TEST_CASE("CellRanger4 clipping keeps exact scores for adapters too long for 16-bit lanes") {
    ClipCR4 clip;
    std::mt19937 random(9);
    std::vector<uint8> query(9000);
    for(auto& b:query) b=random()%5;
    std::string sequence=randomRead(random,91);
    clip.fillOneSeq(0,sequence.data(),sequence.size());
    clip.align(query.data(),query.size(),1);
    for(char& c:sequence) if(std::string("ACGTN").find(c)==std::string::npos) c='N';
    auto expected=overlap(query,sequence.substr(0,std::min<size_t>(sequence.size(),91)));
    CHECK(clip.alignRes[0].score==expected.score);
    CHECK(clip.alignRes[0].endLocationTarget==expected.endLocationTarget);
}
