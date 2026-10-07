#include <doctest/doctest.h>
#include "gpuSjdbRemap.h"
#include "PackedArray.h"
#include <algorithm>
#include <random>
#include <cstring>

TEST_CASE("GPU SJ remap request rejects unsupported geometry before execution") {
    char input[32]{}, output[32]{};
    uint32_t order[2]={0,1};
    GpuSjdbRemapRequest r{input,output,5,24,128,64,32,order,2,33};
    CHECK(validGpuSjdbRemap(r));
    auto bad=r;bad.bits=57;CHECK_FALSE(validGpuSjdbRemap(bad));
    bad=r;bad.output=input;CHECK_FALSE(validGpuSjdbRemap(bad));
    bad=r;bad.chunkBytes=7;CHECK_FALSE(validGpuSjdbRemap(bad));
    bad=r;bad.bytes--;CHECK_FALSE(validGpuSjdbRemap(bad));
    bad=r;bad.genomeBytes++;CHECK_FALSE(validGpuSjdbRemap(bad));
    order[0]=2;CHECK_FALSE(validGpuSjdbRemap(r));
}

#ifdef STAR_TEST_CUDA
TEST_CASE("CUDA packed SA remap matches sequential PackedArray on permutations and chunk edges") {
    std::mt19937 random(309);
    for(unsigned bits: {9u,17u,33u,40u,56u}) for(unsigned count: {1u,7u,8u,9u,65u,513u}) {
        PackedArray input, expected;
        input.defineBits(bits,count);input.allocateArray();
        expected.defineBits(bits,count);expected.allocateArray();
        std::vector<char> actual(input.lengthByte, char(0x5a));
        uint32_t order[4]={2,0,3,1};
        const uint64_t end=128, sjLength=16, genome=end+4*sjLength;
        const uint64_t strand=uint64_t(1)<<(bits-1);
        for(uint64_t i=0;i<count;++i) {
            bool reverse=random()%2;
            uint64_t position;
            if(i%3==0) position=1+random()%(end-1);
            else position=end+(random()%4)*sjLength+random()%sjLength;
            uint64_t value=reverse ? (genome-position)|strand : position;
            input.writePacked(i,value);
            // Independent sequential transformation from the original insertion loop.
            uint64_t transformed=value;
            if(reverse && position>=end) {
                uint64_t j=(position-end)/sjLength;
                position+=(uint64_t(order[j])-j)*sjLength;
                transformed=(genome-position)|strand;
            } else if(!reverse && position>=end) {
                uint64_t j=(position-end)/sjLength;
                transformed+=(uint64_t(order[j])-j)*sjLength;
            }
            expected.writePacked(i,transformed);
        }
        auto result=gpuSjdbRemap({input.charArray,actual.data(),count,input.lengthByte,
            genome,end,sjLength,order,4,bits,24}); // deliberately tiny chunks
        REQUIRE(result.status==GpuSjdbRemapResult::Complete);
        CHECK(result.chunks==(input.lengthByte+23)/24);
        CHECK(std::equal(actual.begin(),actual.end(),expected.charArray));
        // STAR's host SA reservations overlap: all input must reach the device
        // before any downloaded chunk may overwrite the original host bytes.
        std::vector<char> overlapping(input.lengthByte+8,char(0x5a));
        std::memcpy(overlapping.data()+8,input.charArray,input.lengthByte);
        result=gpuSjdbRemap({overlapping.data()+8,overlapping.data(),count,input.lengthByte,
            genome,end,sjLength,order,4,bits,24});
        REQUIRE(result.status==GpuSjdbRemapResult::Complete);
        CHECK(std::equal(overlapping.begin(),overlapping.begin()+input.lengthByte,expected.charArray));
        input.deallocateArray();expected.deallocateArray();
    }
}
#else
TEST_CASE("CPU-only GPU adapter refuses a required backend without writing output") {
    char input[24]{}, output[24];std::fill(output,output+24,char(0x5a));
    uint32_t order[2]={0,1};
    auto result=gpuSjdbRemap({input,output,5,24,128,64,32,order,2,33});
    CHECK(result.status==GpuSjdbRemapResult::Unavailable);
    CHECK(std::all_of(output,output+24,[](char c){return c==char(0x5a);}));
}
#endif
