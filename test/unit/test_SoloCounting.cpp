#include "doctest/doctest.h"
#include "DirectionalUMI.h"
#include "SoloEM.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>

TEST_CASE("Directional UMI graph reaches singleton bridges in every input order") {
    std::array<uint32_t,3> order{0,1,4}; // AA is the bridge between AC and CA.
    do {
        uint32_t records[9];
        for (size_t i=0;i<3;++i) {records[3*i]=order[i];records[3*i+1]=1;}
        CHECK(collapseDirectionalUMIs(records,3,3)==1);
        for (size_t i=0;i<3;++i) CHECK(records[3*i+2]==0);
    } while (std::next_permutation(order.begin(),order.end()));
}

TEST_CASE("Directional graph agrees with independent transitive closure on all two-base triples") {
    for (uint32_t a=0;a<16;++a) for (uint32_t b=a+1;b<16;++b) for (uint32_t c=b+1;c<16;++c)
    for (uint32_t ca=1;ca<=3;++ca) for (uint32_t cb=1;cb<=3;++cb) for (uint32_t cc=1;cc<=3;++cc) {
        std::array<std::array<uint32_t,2>,3> nodes{{{a,ca},{b,cb},{c,cc}}};
        std::sort(nodes.begin(),nodes.end(),[](auto x,auto y){return x[1]!=y[1] ? x[1]>y[1]:x[0]<y[0];});
        bool reach[3][3]{};
        uint32_t records[9];
        for (size_t i=0;i<3;++i) {
            records[3*i]=nodes[i][0];records[3*i+1]=nodes[i][1];
            for (size_t j=0;j<3;++j) {
                const auto x=nodes[i][0]^nodes[j][0];
                const unsigned distance=(bool(x&3)+bool(x&12));
                reach[i][j]=i==j || (distance==1 && nodes[i][1]>=2*nodes[j][1]-1);
            }
        }
        for (size_t k=0;k<3;++k) for (size_t i=0;i<3;++i) for (size_t j=0;j<3;++j)
            reach[i][j]=reach[i][j] || (reach[i][k] && reach[k][j]);
        bool seen[3]{};uint32_t groups=0;
        for (size_t i=0;i<3;++i) if (!seen[i]) {
            ++groups;
            for (size_t j=0;j<3;++j) if (reach[i][j]) seen[j]=true;
        }
        CHECK(collapseDirectionalUMIs(records,3,3)==groups);
    }
}

TEST_CASE("Directional counts do not overflow at uint32 maximum") {
    uint32_t records[]{0,UINT32_MAX,0,1,UINT32_MAX,0};
    CHECK(collapseDirectionalUMIs(records,2,3)==2);
    CHECK(collapseDirectionalUMIs(nullptr,0,3)==0);
}

TEST_CASE("EM preserves a molecule when all gene weights are below threshold") {
    for (size_t count:{100u,101u,150u}) {
        std::vector<uint32_t> genes(count);std::iota(genes.begin(),genes.end(),0);
        std::vector<double> previous(count,1.0/count),next(count,0);
        soloEMDistribute(genes,previous,next);
        for (double value:next) {CHECK(std::isfinite(value));CHECK(value==doctest::Approx(1.0/count));}
        CHECK(std::accumulate(next.begin(),next.end(),0.0)==doctest::Approx(1));
    }
    std::vector<double> previous{.003,.006,2},next(3,0);
    soloEMDistribute(std::vector<unsigned>{0,1},previous,next);
    CHECK(next[0]==doctest::Approx(1.0/3));CHECK(next[1]==doctest::Approx(2.0/3));
    soloEMDistribute(std::vector<unsigned>{1,2},previous,next);
    CHECK(next[1]==doctest::Approx(2.0/3));CHECK(next[2]==1);
    previous.assign(3,0);next.assign(3,0);
    soloEMDistribute(std::vector<unsigned>{0,1},previous,next);
    CHECK(next[0]==.5);CHECK(next[1]==.5);
}

TEST_CASE("Finite ordinary EM updates remain bit-identical to the old truncation") {
    for (unsigned seed=1;seed<100;++seed) {
        std::vector<double> previous{seed*.001,.01,seed*.21,2},observed(4,0),expected(4,0);
        const std::vector<unsigned> genes{0,1,2,3};
        auto old=previous;
        for (auto& value:old) if(value<.01) value=0;
        const double inverse=1.0/std::accumulate(old.begin(),old.end(),0.0);
        for (const auto gene:genes) expected[gene]+=old[gene]*inverse;
        soloEMDistribute(genes,previous,observed);
        CHECK(observed==expected);
    }
}
