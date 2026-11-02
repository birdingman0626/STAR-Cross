#ifndef STAR_SEED_BATCH_H
#define STAR_SEED_BATCH_H
#include <memory>
#include <vector>
#include <cstddef>
class Parameters;
class Genome;
struct PreparedRead;
struct StitchBatch;
struct StitchResults;
struct ClipBatch;

// One owner per fixed-index mapping pass; one independent buffer slot per worker.
class SeedBatchExecutor {
public:
    SeedBatchExecutor(Parameters& parameters, Genome& genome);
    ~SeedBatchExecutor();
    void run(std::vector<PreparedRead>& reads, unsigned worker);
    std::size_t project(std::vector<PreparedRead>& reads,unsigned worker,std::size_t first);
    void report();
    bool stitchEnabled() const;
    // Device setup runs in the background. False while it is still running; workers map
    // with the original CPU path until then. Rethrows a setup failure in required mode.
    bool deviceSettled() const;
    void stitch(const StitchBatch& batch, StitchResults& results, unsigned slot); // slot < 2*runThreadN
    // CR4 adapter clipping of one chunk on the device; false when the device clipper is off.
    bool clip(const ClipBatch& batch, unsigned worker);
private:
    void barcodeSubmit(const std::vector<PreparedRead>& reads, unsigned worker); // queue the device lookup
    void barcodeCollect(std::vector<PreparedRead>& reads, unsigned worker);      // wait, store the hints
    struct State;
    std::unique_ptr<State> state;
};
#endif
