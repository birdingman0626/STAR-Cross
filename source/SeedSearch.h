#ifndef STAR_SEED_SEARCH_H
#define STAR_SEED_SEARCH_H
#include "SeedSearchTypes.h"
#include "SuffixArrayFuns.h"
#include <stdexcept>

// Synchronous CPU executor. No index ownership, read allocation or RNG state.
// Only independent requests from one fixed effective index may share a batch.
class SeedSearchEngine {
    SeedIndexView index;
    void validate(const SeedQuery& q, size_t bytes) const {
        if (!q.length || q.initialLength>q.length || q.forward>1 || q.lower>q.upper
            || q.upper>=index.nSA || q.offset>bytes || q.readBytes>bytes-q.offset
            || q.start>=q.readBytes
            || (q.forward ? q.length>q.readBytes-q.start : q.length>q.start+1))
            throw std::invalid_argument("invalid seed query or read-pool extent");
    }
    SeedMatch execute(const char* reads, const char* complement, const SeedQuery& q) const {
        const char* strands[2]={reads+q.offset,complement+q.offset};
        uint length=q.initialLength, range[2];
        const uint count=maxMappableLength(index,strands,q.start,q.length,q.lower,q.upper,
                                          q.forward,length,range);
        return {length,range[0],range[1],count};
    }
public:
    explicit SeedSearchEngine(SeedIndexView view):index(view) {
        if (!index.G || !index.SA.charArray || !index.nGenome || !index.nSA
            || index.nSA!=index.SA.length || index.GstrandBit>55 || index.GstrandBit<8
            || index.SA.wordLength!=index.GstrandBit+1
            || index.GstrandMask!=((static_cast<uint>(1)<<index.GstrandBit)-1))
            throw std::invalid_argument("invalid effective seed index geometry");
    }
    void searchBatch(const char* reads, const char* complement, size_t bytes,
                     const SeedQuery* queries, size_t count, SeedMatch* output, int threads=1) const {
        if (threads<1 || count>static_cast<size_t>(std::numeric_limits<long long>::max())
            || (count && (!reads || !complement || !queries || !output)))
            throw std::invalid_argument("invalid seed batch buffers or thread count");
        for (size_t i=0;i<count;++i) validate(queries[i],bytes);
        #pragma omp parallel for if(threads>1) num_threads(threads)
        for (long long i=0;i<static_cast<long long>(count);++i)
            output[i]=execute(reads,complement,queries[i]);
    }
};
#endif
