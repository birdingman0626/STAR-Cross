#include "doctest/doctest.h"
#include "htslib/htslib/sam.h"
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

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
