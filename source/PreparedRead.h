#ifndef STAR_PREPARED_READ_H
#define STAR_PREPARED_READ_H
#include "SeedRounds.h"
#include "SeedHitBatch.h"
#include "BarcodeHint.h"
#include <array>
#include <string>
#include <string_view>

// Only input state crosses the batch boundary; alignment scratch stays per worker.
struct PreparedRead {
    // Text fields of all ends live in one buffer (one allocation, read and written sequentially).
    struct Field { uint32 offset=0, size=0; };
    struct End {
        Field name, sequence, quality, extra;
        uint64 length=0, originalLength=0;
        std::array<uint32,2> clipped{};
    };
    std::vector<End> ends;
    std::string text;
    std::string_view view(Field f) const {return std::string_view(text.data()+f.offset,f.size);}
    std::vector<char> sequence;
    std::vector<SeedEmission> seeds;
    SeedSummary seedSummary;
    SeedHitBatch projected;
    bool deviceConsolidated=false;
    BarcodeHint barcode;
    uint64 id=0, originalPairLength=0, mismatchLimit=0;
    uint32 fileIndex=0;
    char filter=0;
    int fileType=0;
    size_t bytes() const {
        return sizeof(*this)+sequence.capacity()+ends.capacity()*sizeof(End)+text.capacity();
    }
};
#endif
