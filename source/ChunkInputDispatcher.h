#ifndef STAR_CHUNK_INPUT_DISPATCHER_H
#define STAR_CHUNK_INPUT_DISPATCHER_H
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <stdexcept>

// One producer serves bounded requests from persistent logical mapping workers.
// Each caller waits for completion before reading/reusing its own chunk buffer.
class ChunkInputDispatcher {
    // Lives on the caller's stack until the producer marks it done.
    struct Request {
        std::function<void()> read;
        std::exception_ptr error;
        bool done=false;
    };
    size_t capacity;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Request*> requests;
    bool closing=false;
    std::thread producer;
    void run() {
        while (true) {
            Request* request;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock,[this]{return closing || !requests.empty();});
                if (requests.empty()) return;
                request=requests.front();requests.pop_front();changed.notify_all();
            }
            std::exception_ptr error;
            try {request->read();} catch (...) {error=std::current_exception();}
            // Hand the error over under the mutex, so the producer holds no
            // reference to it once the caller can see it. Releasing that last
            // reference later, from this thread, races (for ThreadSanitizer)
            // with the caller reading the exception.
            {
                std::lock_guard<std::mutex> lock(mutex);
                request->error=std::move(error);request->done=true;
            }
            changed.notify_all();
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
        Request request{std::move(readChunk),nullptr,false};
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock,[this]{return closing || requests.size()<capacity;});
            if (closing) throw std::logic_error("input dispatcher is closed");
            requests.push_back(&request);changed.notify_all();
            changed.wait(lock,[&request]{return request.done;}); // queued requests run even after finish()
        }
        if (request.error) std::rethrow_exception(request.error); // exceptions reach the requesting worker
    }
    void finish() {
        {std::lock_guard<std::mutex> lock(mutex);closing=true;changed.notify_all();}
        if(producer.joinable()) producer.join();
    }
};
#endif
