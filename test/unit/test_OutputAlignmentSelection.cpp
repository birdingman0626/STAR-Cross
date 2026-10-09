#include "doctest/doctest.h"
#include "OutputAlignmentSelection.h"

namespace {struct Candidate {uint Chr; bool primaryFlag;};}

TEST_CASE("Output reference filtering preserves order and primary semantics") {
    Candidate a{1,true}, b{5,false}, c{7,true};
    Candidate* candidates[]={&a,&b,&c};
    CHECK(selectOutputAlignments({false,true,true,5},candidates,3)==3);
    CHECK(candidates[0]==&a);
    CHECK(selectOutputAlignments({true,true,false,5},candidates,3)==0);
    CHECK(candidates[0]==&a);
    CHECK(a.primaryFlag);
    CHECK(selectOutputAlignments({true,false,true,5},candidates,3)==2);
    CHECK(candidates[0]==&b); CHECK(candidates[1]==&c);
    CHECK(b.primaryFlag); CHECK_FALSE(c.primaryFlag);
    CHECK(a.primaryFlag); // Removed candidates retain their original state.
    CHECK(selectOutputAlignments({true,true,false,5},candidates,2)==2);
}

TEST_CASE("Output filter no-selection, disabled and empty boundaries") {
    Candidate a{0,false}; Candidate* candidates[]={&a};
    CHECK(selectOutputAlignments({true,false,true,1},candidates,1)==0);
    CHECK_FALSE(a.primaryFlag);
    CHECK(selectOutputAlignments({true,false,false,1},candidates,1)==1);
    CHECK(selectOutputAlignments<Candidate>({true,false,true,1},nullptr,0)==0);
    // KeepOnly's precedence is deliberate if both switches are set.
    CHECK(selectOutputAlignments({true,true,true,0},candidates,1)==1);
    CHECK_FALSE(a.primaryFlag);
}
