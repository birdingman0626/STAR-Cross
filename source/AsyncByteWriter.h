#ifndef STAR_ASYNC_BYTE_WRITER_H
#define STAR_ASYNC_BYTE_WRITER_H
#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Owns serialized batches, never borrowed mapping buffers. One pending packet
// plus one active packet bounds extra payload memory to 2*maximumPacketBytes.
class AsyncByteWriter {
    using Packet=std::vector<char>;
    std::function<void(char*,size_t)> sink;
    size_t maximumPacketBytes;
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<Packet> pending;
    bool closing=false;
    std::exception_ptr failure;
    std::thread worker;
    void consume() noexcept;
public:
    AsyncByteWriter(size_t maximumPacketBytes,std::function<void(char*,size_t)> sink);
    AsyncByteWriter(const AsyncByteWriter&)=delete;
    AsyncByteWriter& operator=(const AsyncByteWriter&)=delete;
    ~AsyncByteWriter();
    void submit(const char* data,size_t size);
    void finish(); // Called after producers join; drains, joins and rethrows errors.
};
#endif
