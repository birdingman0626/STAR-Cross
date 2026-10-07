#include <doctest/doctest.h>
#include <chrono>
#include <filesystem>
#include "bamEndian.h"
#include "htslib/htslib/sam.h"

static std::vector<char> nativeRecord() {
    std::vector<char> bytes(59, 0);
    const uint32_t core[] = {55, 0, 0x10203, (4681u<<16)|(60u<<8)|3u,
                            (99u<<16)|2u, 3, 0, 0x20304, 301};
    std::memcpy(bytes.data(), core, sizeof(core));
    std::memcpy(bytes.data()+36, "r1", 3);
    storeUnaligned<uint32_t>(bytes.data()+39, 2u<<4); // 2M
    storeUnaligned<uint32_t>(bytes.data()+43, (1u<<4)|4u); // 1S
    bytes[47]=0x12; bytes[48]=0x40; // ACG
    bytes[49]=30; bytes[50]=31; bytes[51]=32;
    std::memcpy(bytes.data()+52, "NMi\x04\x03\x02\x01", 7);
    return bytes;
}

TEST_CASE("BAM serialization preserves little-endian core CIGAR and auxiliary bytes") {
    auto record=nativeRecord();
    REQUIRE(bamNativeRecordToLE(record.data(), record.size()));
    const auto* bytes=reinterpret_cast<const uint8_t*>(record.data());
    CHECK(bytes[0]==55);
    CHECK(bytes[1]==0);
    CHECK(bytes[8]==3); CHECK(bytes[9]==2); CHECK(bytes[10]==1);
    CHECK(le_to_u32(bytes+12)==((4681u<<16)|(60u<<8)|3u));
    CHECK(le_to_u32(bytes+16)==((99u<<16)|2u));
    CHECK(le_to_u32(bytes+39)==32);
    CHECK(le_to_u32(bytes+43)==20);
    CHECK(std::memcmp(bytes+52, "NMi\x04\x03\x02\x01", 7)==0);
    CHECK(bytes[47]==0x12); CHECK(bytes[51]==32);
}

TEST_CASE("BAM serializer rejects truncated core and oversized CIGAR") {
    auto record=nativeRecord();
    CHECK_FALSE(bamNativeRecordToLE(record.data(), 35));
    CHECK_FALSE(bamNativeRecordToLE(record.data(), record.size()-1));
    storeUnaligned<uint32_t>(record.data()+16, 0xffff);
    CHECK_FALSE(bamNativeRecordToLE(record.data(), record.size()));
}

TEST_CASE("BAM native output reads through HTSlib on either host byte order") {
    const auto path=(std::filesystem::temp_directory_path()/
        ("star-bam-endian-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))).string();
    struct Cleanup { std::string path; ~Cleanup() { std::filesystem::remove(path); } } cleanup{path};
    BGZF* out=bgzf_open(path.c_str(), "w");
    REQUIRE(out!=nullptr);
    REQUIRE(bgzf_write(out, "BAM\1", 4)==4);
    REQUIRE(bamWriteInt32LE(out, 0)==4); // no header text
    REQUIRE(bamWriteInt32LE(out, 1)==4);
    REQUIRE(bamWriteInt32LE(out, 5)==4);
    REQUIRE(bgzf_write(out, "chr1", 5)==5);
    REQUIRE(bamWriteInt32LE(out, 1000000)==4);
    auto records=nativeRecord();
    const auto second=nativeRecord();
    records.insert(records.end(), second.begin(), second.end());
    REQUIRE(bamWriteNativeRecords(out, records.data(), records.size())==118);
    REQUIRE(bgzf_close(out)==0);
    BGZF* in=bgzf_open(path.c_str(), "r");
    REQUIRE(in!=nullptr);
    bam_hdr_t* header=bam_hdr_read(in);
    REQUIRE(header!=nullptr);
    CHECK(header->target_len[0]==1000000);
    bam1_t* record=bam_init1();
    REQUIRE(record!=nullptr);
    for (int i=0; i<2; ++i) {
        REQUIRE(bam_read1(in, record)>0);
        CHECK(record->core.pos==0x10203);
        CHECK(record->core.flag==99);
        CHECK(record->core.isize==301);
        CHECK(bam_get_cigar(record)[0]==32);
        CHECK(bam_aux2i(bam_aux_get(record,"NM"))==0x01020304);
    }
    CHECK(bam_read1(in, record)==-1);
    bam_destroy1(record); bam_hdr_destroy(header);
    CHECK(bgzf_close(in)==0);
}
