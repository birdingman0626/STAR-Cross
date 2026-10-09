#ifndef STAR_CHUNK_INPUT_DISPATCHER_H
#define STAR_CHUNK_INPUT_DISPATCHER_H
#include <condition_variable>
#include <deque>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <stdexcept>

// One producer serves bounded requests from persistent logical mapping workers.
// Each caller waits for completion before reading/reusing its own chunk buffer.
class ChunkInputDispatcher {
    size_t capacity;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<std::packaged_task<void()>> requests;
    bool closing=false;
    std::thread producer;
    void run() {
        while (true) {
            std::packaged_task<void()> request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock,[this]{return closing || !requests.empty();});
                if (requests.empty()) return;
                request=std::move(requests.front());requests.pop_front();changed.notify_all();
            }
            request(); // Exceptions are delivered to the requesting worker's future.
        }
    }
public:
    explicit ChunkInputDispatcher(size_t maximum):capacity(maximum) {
        if (!capacity) throw std::invalid_argument("zero input dispatch capacity");
        producer=std::thread([this]{run();});
    }
    ChunkInputDispatcher(const ChunkInputDispatcher&)=delete;
    ChunkInputDispatcher& operator=(const ChunkInputDispatcher&)=delete;
    ~ChunkInputDispatcher(){finish();}
    void invoke(std::function<void()> readChunk) {
        std::packaged_task<void()> request(std::move(readChunk));
        auto result=request.get_future();
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock,[this]{return closing || requests.size()<capacity;});
            if (closing) throw std::logic_error("input dispatcher is closed");
            requests.push_back(std::move(request));changed.notify_all();
        }
        result.get();
    }
    void finish() {
        {std::lock_guard<std::mutex> lock(mutex);closing=true;changed.notify_all();}
        if(producer.joinable()) producer.join();
    }
};
#endif
