#include "doctest/doctest.h"
#include "AsyncByteWriter.h"
#include <stdexcept>
#include <array>
#include <algorithm>

TEST_CASE("Async batches own bytes and preserve submission order through finish") {
    std::vector<char> output;
    AsyncByteWriter writer(10,[&](const char* data,size_t size){output.insert(output.end(),data,data+size);});
    char input[]={'a','b'};
    for(int i=0;i<100;++i) {input[0]='a';writer.submit(input,2);input[0]='x';}
    writer.finish();writer.finish();
    REQUIRE(output.size()==200);
    for(size_t i=0;i<output.size();i+=2) {CHECK(output[i]=='a');CHECK(output[i+1]=='b');}
    CHECK_THROWS_AS(writer.submit(input,2),std::logic_error);
}
TEST_CASE("Async writer propagates sink errors and rejects oversized batches") {
    AsyncByteWriter writer(2,[](const char*,size_t){throw std::runtime_error("sink failure");});
    CHECK_THROWS_AS(writer.submit("abc",3),std::invalid_argument);
    writer.submit("a",1);
    CHECK_THROWS_AS(writer.finish(),std::runtime_error);
    CHECK_THROWS_AS(writer.submit("a",1),std::runtime_error);
}
TEST_CASE("Concurrent output producers transfer every owned packet exactly once") {
    std::vector<char> output;
    AsyncByteWriter writer(1,[&](char* data,size_t size){output.insert(output.end(),data,data+size);});
    std::array<std::thread,4> producers;
    for(size_t i=0;i<producers.size();++i) producers[i]=std::thread([&,i]{
        char value=static_cast<char>('a'+i);
        for(int packet=0;packet<100;++packet) writer.submit(&value,1);
    });
    for(auto& producer:producers) producer.join();
    writer.finish();
    CHECK(output.size()==400);
    for(char value='a';value<='d';++value) CHECK(std::count(output.begin(),output.end(),value)==100);
}
TEST_CASE("Recycled packets never leak stale or sink-modified bytes and stay within two buffers") {
    std::vector<char> output;
    std::vector<const char*> buffers;
    AsyncByteWriter writer(64,[&](char* data,size_t size){
        output.insert(output.end(),data,data+size);
        if (std::find(buffers.begin(),buffers.end(),data)==buffers.end()) buffers.push_back(data);
        std::fill(data,data+size,'#'); // sinks may transform bytes in place
    });
    std::vector<char> expected;
    char input[64];
    for(int packet=0;packet<200;++packet) {
        const size_t size=1+(packet*37)%64; // alternate long and short packets
        for(size_t i=0;i<size;++i) input[i]=static_cast<char>('a'+(packet+i)%26);
        expected.insert(expected.end(),input,input+size);
        writer.submit(input,size);
    }
    writer.finish();
    CHECK(output==expected);
    // Growth may reallocate a recycled buffer, so check reuse with full-size packets.
    buffers.clear();
    AsyncByteWriter fixed(64,[&](char* data,size_t){
        if (std::find(buffers.begin(),buffers.end(),data)==buffers.end()) buffers.push_back(data);
    });
    for(int packet=0;packet<200;++packet) fixed.submit(input,64);
    fixed.finish();
    CHECK(buffers.size()<=2);
}
