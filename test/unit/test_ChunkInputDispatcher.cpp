#include "doctest/doctest.h"
#include "ChunkInputDispatcher.h"
#include <atomic>
#include <vector>

TEST_CASE("Input dispatcher serializes concurrent requests and delivers errors") {
    ChunkInputDispatcher dispatcher(2);
    std::atomic<int> active{0},maximum{0},count{0};
    std::vector<std::thread> workers;
    for(int i=0;i<4;++i) workers.emplace_back([&]{
        for(int j=0;j<50;++j) dispatcher.invoke([&]{
            int n=++active;if(n>maximum) maximum=n;
            ++count;--active;
        });
    });
    for(auto& thread:workers) thread.join();
    CHECK(count==200);CHECK(maximum==1);
    CHECK_THROWS_AS(dispatcher.invoke([]{throw std::runtime_error("read failed");}),std::runtime_error);
    dispatcher.invoke([&]{++count;});CHECK(count==201);
    dispatcher.finish();dispatcher.finish();
    CHECK_THROWS_AS(dispatcher.invoke([]{}),std::logic_error);
}
