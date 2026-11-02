#ifndef STAR_SEED_ROUNDS_H
#define STAR_SEED_ROUNDS_H
#include "AdaptiveSeedTypes.h"
#include "SeedIndexView.h"
#include <coroutine>
#include <exception>
#include <span>
#include <stdexcept>
#include <utility>

// One suspended search per read. No ReadAlign, RNG, output, or index ownership.
struct SeedRoundIndex {
    SeedIndexView search;
    const PackedArray& prefixes;
    const uint64* starts;
    uint64 prefixBases, sparse, absentFlag, nFlag, nMask;
};
class SeedRoundTask {
public:
    struct promise_type {
        SeedQuery query{};
        SeedMatch match{};
        std::exception_ptr error;
        SeedRoundTask get_return_object() { return SeedRoundTask{Handle::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept { error=std::current_exception(); }
    };
    using Handle=std::coroutine_handle<promise_type>;
    explicit SeedRoundTask(Handle h): handle(h) {}
    SeedRoundTask(SeedRoundTask&& other) noexcept:handle(std::exchange(other.handle,{})) {}
    SeedRoundTask(const SeedRoundTask&)=delete;
    ~SeedRoundTask() { if(handle) handle.destroy(); }
    bool advance() {
        if(handle.done()) throw std::logic_error("completed seed task resumed");
        handle.resume();
        if(handle.promise().error) std::rethrow_exception(handle.promise().error);
        return !handle.done();
    }
    const SeedQuery& query() const { return handle.promise().query; }
    void complete(SeedMatch match) { handle.promise().match=match; }
private:
    Handle handle;
};

SeedRoundTask seedRounds(std::span<const char> read, uint64 offset,
                         const SeedRoundIndex& index, const SeedRoundConfig& config,
                         std::vector<SeedEmission>& output, size_t& batchEvents);
#endif
