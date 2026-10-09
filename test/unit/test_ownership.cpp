#include <doctest/doctest.h>
#include "Quantifications.h"
#include "ClipMate.h"
#include "Genome.h"
#include "ReadAlign.h"
#include "Solo.h"
#include <type_traits>

static_assert(!std::is_copy_assignable<Genome>::value, "Genome snapshots cannot replace an owner");
static_assert(!std::is_copy_constructible<Genome>::value, "Genome borrowing must be explicit");
static_assert(!std::is_copy_constructible<ReadAlign>::value, "ReadAlign owns thread arenas");
static_assert(!std::is_copy_constructible<Quantifications>::value, "Quantifications owns counts");
static_assert(!std::is_copy_constructible<ClipMate>::value, "ClipMate owns its CR4 scorer");
static_assert(std::is_copy_constructible<Transcriptome>::value, "Chunks share metadata storage");
static_assert(!std::is_copy_assignable<Transcriptome>::value, "Do not rebind loaded metadata views");
static_assert(!std::is_copy_constructible<Solo>::value, "Solo owns aggregate feature storage");
static_assert(!std::is_copy_constructible<SoloFeature>::value, "Feature summaries have a unique owner");

TEST_CASE("Genome parameter readers release registrations without closing borrowed streams") {
    InOutStreams streams;
    for (int iteration=0; iteration<32; ++iteration) {
        Parameters parameters(streams);
        for (const auto pid : parameters.readFilesCommandPID) CHECK(pid == 0);
        REQUIRE(parameters.inOut == &streams);
        REQUIRE(!parameters.parArray.empty());
        auto registration = std::find_if(parameters.parArray.begin(), parameters.parArray.end(),
            [](const ParameterInfoBase* info) { return info->nameString == "runThreadN"; });
        REQUIRE(registration != parameters.parArray.end());
        std::istringstream input("7");
        (*registration)->inputValues(input);
        CHECK(parameters.runThreadN == 7);
    }
}

TEST_CASE("Novel junction arrays retain their owners across Parameters pass copies") {
    InOutStreams streams;
    Parameters parameters(streams);
    parameters.sjNovelStartStorage.reset(new uint64[1]{17});
    parameters.sjNovelEndStorage.reset(new uint64[1]{29});
    parameters.sjNovelStart=parameters.sjNovelStartStorage.get();
    parameters.sjNovelEnd=parameters.sjNovelEndStorage.get();
    Parameters pass(parameters);
    parameters.sjNovelStartStorage.reset();parameters.sjNovelEndStorage.reset();
    CHECK(pass.sjNovelStart[0]==17);
    CHECK(pass.sjNovelEnd[0]==29);
}

TEST_CASE("Quantifications owns independent count arrays and releases repeatedly") {
    for (int iteration=0; iteration<32; ++iteration) {
        Quantifications total(17), incoming(17);
        for (int strand=0; strand<3; ++strand) {
            REQUIRE(total.geneCounts.gCount[strand] != incoming.geneCounts.gCount[strand]);
            CHECK(total.geneCounts.cAmbig[strand] == 0);
            CHECK(total.geneCounts.cNone[strand] == 0);
            for (int gene=0; gene<17; ++gene) CHECK(total.geneCounts.gCount[strand][gene] == 0);
            incoming.geneCounts.gCount[strand][4]=strand+1;
            incoming.geneCounts.cAmbig[strand]=2;
            incoming.geneCounts.cNone[strand]=3;
        }
        incoming.geneCounts.cMulti=7;
        total.addQuants(incoming);
        CHECK(total.geneCounts.cMulti == 7);
        for (int strand=0; strand<3; ++strand) {
            CHECK(total.geneCounts.gCount[strand][4] == strand+1);
            CHECK(total.geneCounts.cAmbig[strand] == 2);
            CHECK(total.geneCounts.cNone[strand] == 3);
        }
    }
    Quantifications empty(0);
    CHECK(empty.geneCounts.nGe == 0);
}

TEST_CASE("ClipMate moves its CR4 allocation without copying release authority") {
    ClipMate original{};
    original.cr4.reset(new ClipCR4);
    const auto* scorer = original.cr4.get();
    ClipMate moved(std::move(original));
    CHECK(original.cr4 == nullptr);
    CHECK(moved.cr4.get() == scorer);
}
