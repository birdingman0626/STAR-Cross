#ifndef STAR_CHUNK_INPUT_PREFETCHER_H
#define STAR_CHUNK_INPUT_PREFETCHER_H
#include "IncludeDefine.h"
#include "uninitArray.h"
#include <array>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

// One producer fills input chunks ahead of the mapping workers, in input order, so a
// worker that finishes a chunk swaps its buffers for a filled chunk instead of parsing
// the next one under the input lock. Chunks are handed out first come, first served,
// as the input lock did; the producer stops after the first empty (end-of-input) chunk.
class ChunkInputPrefetcher {
public:
    struct Chunk {
        std::vector<std::unique_ptr<char[]>> storage;
        std::array<uint64,MAX_N_MATES> sizes{};
        bool noReadsLeft=false;
        uint64 iChunkIn=0;
    };
    using Fill=std::function<void(Chunk&)>;
    // nextEmptyIndex: chunk number for an end-of-input chunk handed out after the producer stopped.
    ChunkInputPrefetcher(size_t depth,size_t ends,size_t bytes,Fill fillChunk,std::function<uint64()> nextEmptyIndex)
        :fill(std::move(fillChunk)),emptyIndex(std::move(nextEmptyIndex)) {
        for(size_t i=0;i<depth;++i) {
            auto c=std::make_unique<Chunk>();
            for(size_t e=0;e<ends;++e) {
                c->storage.emplace_back(makeUninitArray<char>(bytes));
                std::memset(c->storage.back().get(),'\n',bytes);
            }
            free.push_back(std::move(c));
        }
        producer=std::thread([this]{run();});
    }
    ChunkInputPrefetcher(const ChunkInputPrefetcher&)=delete;
    ChunkInputPrefetcher& operator=(const ChunkInputPrefetcher&)=delete;
    ~ChunkInputPrefetcher(){finish();}

    // Swap the caller's buffers for the next filled chunk; the caller's old buffers become free.
    void take(std::vector<std::unique_ptr<char[]>>& storage,std::array<uint64,MAX_N_MATES>& sizes,bool& noReadsLeft,uint64& iChunkIn) {
        std::unique_lock<std::mutex> lock(mutex);
        changed.wait(lock,[this]{return !ready.empty() || stopped;});
        if(ready.empty()) {
            if(error) std::rethrow_exception(error);
            sizes.fill(0); noReadsLeft=true; iChunkIn=emptyIndex();
            for(auto& s:storage) s[0]='\n';
            return;
        }
        auto c=std::move(ready.front()); ready.pop_front();
        storage.swap(c->storage);
        sizes=c->sizes; noReadsLeft=c->noReadsLeft; iChunkIn=c->iChunkIn;
        free.push_back(std::move(c));
        changed.notify_all();
    }
    void finish() {
        {std::lock_guard<std::mutex> lock(mutex); closing=true;}
        changed.notify_all();
        if(producer.joinable()) producer.join();
    }
private:
    Fill fill;
    std::function<uint64()> emptyIndex;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::unique_ptr<Chunk>> free, ready;
    bool closing=false, stopped=false;
    std::exception_ptr error;
    std::thread producer;
    void run() {
        while(true) {
            std::unique_ptr<Chunk> c;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock,[this]{return closing || !free.empty();});
                if(closing) {stopped=true; changed.notify_all(); return;}
                c=std::move(free.front()); free.pop_front();
            }
            std::exception_ptr failure;
            try {fill(*c);} catch(...) {failure=std::current_exception();}
            std::lock_guard<std::mutex> lock(mutex);
            if(failure) {error=failure; stopped=true; changed.notify_all(); return;}
            const bool last=c->noReadsLeft;
            ready.push_back(std::move(c));
            if(last) stopped=true;
            changed.notify_all();
            if(last) return;
        }
    }
};
#endif
