#include "AsyncByteWriter.h"
#include <stdexcept>
#include <utility>

AsyncByteWriter::AsyncByteWriter(size_t maximum,std::function<void(char*,size_t)> output)
    :sink(std::move(output)),maximumPacketBytes(maximum) {
    if (!maximum || !sink) throw std::invalid_argument("invalid asynchronous byte writer");
    worker=std::thread([this]{consume();});
}
AsyncByteWriter::~AsyncByteWriter() {try {finish();} catch (...) {}}

void AsyncByteWriter::submit(const char* data,size_t size) {
    if (!size) return;
    if (!data || size>maximumPacketBytes) throw std::invalid_argument("output packet exceeds bounded writer capacity");
    std::unique_lock<std::mutex> lock(mutex);
    changed.wait(lock,[this]{return pending.empty() || closing || failure;});
    if (failure) std::rethrow_exception(failure);
    if (closing) throw std::logic_error("submit after writer finish");
    // Copy only after obtaining capacity: blocked producers own no extra packets.
    pending.emplace_back(data,data+size);
    changed.notify_all();
}

void AsyncByteWriter::consume() noexcept {
    try {
        while (true) {
            Packet packet;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock,[this]{return closing || !pending.empty();});
                if (pending.empty()) return;
                packet=std::move(pending.front());pending.pop_front();
                changed.notify_all();
            }
            sink(packet.data(),packet.size());
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex);
        failure=std::current_exception();closing=true;pending.clear();changed.notify_all();
    }
}

void AsyncByteWriter::finish() {
    {std::lock_guard<std::mutex> lock(mutex);closing=true;changed.notify_all();}
    if (worker.joinable()) worker.join();
    if (failure) std::rethrow_exception(failure);
}
