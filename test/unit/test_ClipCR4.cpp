#include <doctest/doctest.h>
#include "ClipCR4.h"
#include <random>

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
