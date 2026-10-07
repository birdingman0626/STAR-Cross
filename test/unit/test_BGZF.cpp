#include "doctest/doctest.h"
#include <string>
#include "htslib/htslib/bgzf.h"
#include <chrono>
#include <filesystem>
#include <random>
#include <vector>

TEST_CASE("BGZF round trip, multithreaded blocks, EOF and random access index") {
    const auto path = (std::filesystem::temp_directory_path() /
        ("star-bgzf-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    struct Cleanup {
        std::string path;
        ~Cleanup() { std::filesystem::remove(path); std::filesystem::remove(path+".gzi"); }
    } cleanup{path};
    std::mt19937 random(1729);
    for (bool compressible : {false, true}) {
        std::vector<unsigned char> payload(200000);
        for (auto &byte : payload) byte = compressible ? 'A' : random() & 255;
        BGZF *output = bgzf_open(path.c_str(), "w6");
        REQUIRE(output != nullptr);
        // HTSlib requires index initialization before starting writer threads.
        REQUIRE(bgzf_index_build_init(output) == 0);
        REQUIRE(bgzf_mt(output, 2, 64) == 0);
        REQUIRE(bgzf_write(output, payload.data(), payload.size()) == static_cast<int64_t>(payload.size()));
        REQUIRE(bgzf_flush(output) == 0);
        REQUIRE(bgzf_index_dump(output, path.c_str(), ".gzi") == 0);
        REQUIRE(bgzf_close(output) == 0);
        BGZF *input = bgzf_open(path.c_str(), "r");
        REQUIRE(input != nullptr);
        CHECK(bgzf_check_EOF(input) == 1);
        std::vector<unsigned char> decoded(payload.size());
        REQUIRE(bgzf_read(input, decoded.data(), decoded.size()) == static_cast<int64_t>(decoded.size()));
        CHECK(decoded == payload);
        unsigned char byte = 0;
        CHECK(bgzf_read(input, &byte, 1) == 0);
        REQUIRE(bgzf_index_load(input, path.c_str(), ".gzi") == 0);
        REQUIRE(bgzf_useek(input, 70000, SEEK_SET) == 0);
        REQUIRE(bgzf_read(input, &byte, 1) == 1);
        CHECK(byte == payload[70000]);
        CHECK(bgzf_close(input) == 0);
    }
}
