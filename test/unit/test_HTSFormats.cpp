#include "doctest/doctest.h"
#include "htslib/htslib/sam.h"
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <limits>
#include <cerrno>
#include "htslib/htslib/vcf.h"

namespace frequency_model {
#define NSYM 256
#include "htslib/htscodecs/htscodecs/c_simple_model.h"
#undef NSYM
}

TEST_CASE("HTSlib pileup formats many maximum-width ChEBI modifications without truncation") {
    const std::string headerText = "@SQ\tSN:chr1\tLN:1000\n";
    std::unique_ptr<sam_hdr_t, decltype(&sam_hdr_destroy)> header(
        sam_hdr_parse(headerText.size(), headerText.c_str()), sam_hdr_destroy);
    std::unique_ptr<bam1_t, decltype(&bam_destroy1)> record(bam_init1(), bam_destroy1);
    REQUIRE(header != nullptr);
    REQUIRE(record != nullptr);
    std::string line = "read\t0\tchr1\t1\t60\t1M1I1M\t*\t0\t0\tAAA\tIII\tMM:Z:";
    std::string expected = "A[";
    for (int i=0; i<32; ++i) {
        const auto code = std::to_string(std::numeric_limits<int>::max()-i);
        line += "A+"+code+",1;";
        expected += "+("+code+")255";
    }
    expected += "]";
    line += "\tML:B:C";
    for (int i=0; i<32; ++i) line += ",255";
    kstring_t input{line.size(), line.size()+1, line.data()};
    REQUIRE(sam_parse1(&input, header.get(), record.get()) >= 0);
    std::unique_ptr<hts_base_mod_state, decltype(&hts_base_mod_state_free)> state(
        hts_base_mod_state_alloc(), hts_base_mod_state_free);
    REQUIRE(state != nullptr);
    REQUIRE(bam_parse_basemod(record.get(), state.get()) == 0);
    bam_pileup1_t pileup{};
    pileup.b = record.get();
    pileup.indel = 1;
    pileup.qpos = 0;
    pileup.cigar_ind = 0;
    kstring_t insertion{0,0,nullptr};
    const auto result = bam_plp_insertion_mod(&pileup, state.get(), &insertion, nullptr);
    const std::string actual = insertion.s ? insertion.s : "";
    free(insertion.s);
    REQUIRE(result == 1);
    CHECK(actual == expected);
}

TEST_CASE("HTSlib errmod rejects invalid dimensions before touching the output") {
    float sentinel = 123;
    for (const int dimension : {0, -1, 17, std::numeric_limits<int>::max()}) {
        errno = 0;
        CHECK(errmod_cal(nullptr, 0, dimension, nullptr, &sentinel) == -1);
        CHECK(errno == EINVAL);
        CHECK(sentinel == 123);
    }
    CHECK(errmod_cal(nullptr, -1, 1, nullptr, &sentinel) == -1);
    CHECK(errmod_cal(nullptr, 1, 1, nullptr, &sentinel) == -1);
    CHECK(errmod_cal(nullptr, 0, 1, nullptr, nullptr) == -1);
    float empty[256];
    std::fill(std::begin(empty), std::end(empty), 123);
    CHECK(errmod_cal(nullptr, 0, 16, nullptr, empty) == 0);
    for (auto value : empty) CHECK(value == 0);
}

TEST_CASE("HTSlib VCF FORMAT rejects zero samples and non-divisible value layouts") {
    std::unique_ptr<bcf_hdr_t, decltype(&bcf_hdr_destroy)> header(bcf_hdr_init("w"), bcf_hdr_destroy);
    std::unique_ptr<bcf1_t, decltype(&bcf_destroy)> record(bcf_init(), bcf_destroy);
    REQUIRE(header != nullptr);
    REQUIRE(record != nullptr);
    REQUIRE(bcf_hdr_append(header.get(), "##FORMAT=<ID=XX,Number=1,Type=Float,Description=\"test\">") == 0);
    REQUIRE(bcf_hdr_sync(header.get()) == 0);
    float values[] = {1, 2, 3};
    CHECK(bcf_update_format_float(header.get(), record.get(), "XX", values, 1) == -1);
    REQUIRE(bcf_hdr_add_sample(header.get(), "one") == 0);
    REQUIRE(bcf_hdr_add_sample(header.get(), "two") == 0);
    REQUIRE(bcf_hdr_sync(header.get()) == 0);
    CHECK(bcf_update_format_float(header.get(), record.get(), "XX", values, 3) == -1);
    CHECK(bcf_update_format_float(header.get(), record.get(), "XX", values, 2) == 0);
}

TEST_CASE("HTScodecs frequency model preserves first-symbol and normalization round trips") {
    using namespace frequency_model;
    std::vector<uint16_t> symbols;
    for (unsigned i=0; i<20000; ++i)
        symbols.push_back(i < 4000 ? 0 : (i * 73u) % 256);
    std::vector<char> bytes(symbols.size() * 2 + 64);
    SIMPLE_MODEL256_ encoder{};
    SIMPLE_MODEL256_init(&encoder, 256);
    RangeCoder out{};
    RC_SetOutput(&out, bytes.data());
    RC_SetOutputEnd(&out, bytes.data()+bytes.size());
    RC_StartEncode(&out);
    for (auto symbol : symbols) SIMPLE_MODEL256_encodeSymbol(&encoder, &out, symbol);
    REQUIRE(RC_FinishEncode(&out) == 0);
    SIMPLE_MODEL256_ decoder{};
    SIMPLE_MODEL256_init(&decoder, 256);
    RangeCoder in{};
    RC_SetInput(&in, bytes.data(), RC_GetOutput(&out));
    RC_StartDecode(&in);
    for (auto symbol : symbols) CHECK(SIMPLE_MODEL256_decodeSymbol(&decoder, &in) == symbol);
    CHECK(RC_FinishDecode(&in) == 0);
}

TEST_CASE("HTSlib BAM and reference-free CRAM preserve records and auxiliary arrays") {
    const std::string text = "@HD\tVN:1.6\n@SQ\tSN:chr1\tLN:1000\n";
    std::unique_ptr<sam_hdr_t, decltype(&sam_hdr_destroy)> header(
        sam_hdr_parse(text.size(), text.c_str()), sam_hdr_destroy);
    REQUIRE(header != nullptr);
    std::unique_ptr<bam1_t, decltype(&bam_destroy1)> record(bam_init1(), bam_destroy1);
    REQUIRE(record != nullptr);
    std::string line = "read1\t0\tchr1\t11\t60\t4M3N4M\t*\t0\t0\tACGTACGT\tIIIIIIII\tNM:i:0\tXA:B:i,1,-2,3";
    kstring_t input{line.size(), line.size()+1, line.data()};
    REQUIRE(sam_parse1(&input, header.get(), record.get()) >= 0);
    for (const auto mode : {"wb", "wc"}) {
        const auto path = (std::filesystem::temp_directory_path() /
            ("star-hts-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
        struct Cleanup {
            std::string path;
            ~Cleanup() { std::error_code ec; std::filesystem::remove(path, ec); }
        } cleanup{path};
        auto close = [](samFile* file) { if (file) sam_close(file); };
        std::unique_ptr<samFile, decltype(close)> out(sam_open(path.c_str(), mode), close);
        REQUIRE(out != nullptr);
        REQUIRE(hts_set_threads(out.get(), 2) == 0);
        if (std::string(mode) == "wc")
            REQUIRE(hts_set_opt(out.get(), CRAM_OPT_NO_REF, 1) == 0);
        REQUIRE(sam_hdr_write(out.get(), header.get()) == 0);
        REQUIRE(sam_write1(out.get(), header.get(), record.get()) >= 0);
        REQUIRE(sam_close(out.release()) == 0);
        std::unique_ptr<samFile, decltype(close)> in(sam_open(path.c_str(), "r"), close);
        REQUIRE(in != nullptr);
        std::unique_ptr<sam_hdr_t, decltype(&sam_hdr_destroy)> decodedHeader(sam_hdr_read(in.get()), sam_hdr_destroy);
        REQUIRE(decodedHeader != nullptr);
        std::unique_ptr<bam1_t, decltype(&bam_destroy1)> decoded(bam_init1(), bam_destroy1);
        REQUIRE(sam_read1(in.get(), decodedHeader.get(), decoded.get()) >= 0);
        CHECK(std::string(bam_get_qname(decoded.get())) == "read1");
        CHECK(decoded->core.pos == 10);
        CHECK(decoded->core.n_cigar == 3);
        CHECK(decoded->core.l_qseq == 8);
        CHECK(bam_get_cigar(decoded.get())[1] == bam_cigar_gen(3, BAM_CREF_SKIP));
        REQUIRE(bam_aux_get(decoded.get(), "XA") != nullptr);
        CHECK(bam_auxB2i(bam_aux_get(decoded.get(), "XA"), 1) == -2);
        CHECK(sam_read1(in.get(), decodedHeader.get(), decoded.get()) == -1);
    }
}
