#include "doctest/doctest.h"
#include "AlignmentFilter.h"

TEST_CASE("Alignment filter preserves threshold edges and reason precedence") {
    AlignFilterConfig config{2,10,3,0.1,0.1,0.5,0.5,10,1};
    AlignmentFilterInput read{1,10,1,20,20,3,2,10};
    CHECK(classifyAlignment(config,read)==-1);
    read.alignments=4; CHECK(classifyAlignment(config,read)==3);
    read.mismatches=3; CHECK(classifyAlignment(config,read)==2);
    read.score=9; CHECK(classifyAlignment(config,read)==1);
    read.windows=0; CHECK(classifyAlignment(config,read)==0);
    read={1,10,2,20,20,3,2,10};
    CHECK(classifyAlignment(config,read)==-1);
    read.alignedLength=19; CHECK(classifyAlignment(config,read)==2);
}
