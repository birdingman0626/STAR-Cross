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
