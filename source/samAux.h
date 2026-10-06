#ifndef STAR_SAM_AUX_H
#define STAR_SAM_AUX_H

#include "htslib/htslib/sam.h"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

// Delegate SAM auxiliary syntax, numeric ranges and BAM endian encoding to
// HTSlib instead of maintaining a second (incomplete) SAM parser.
inline std::vector<unsigned char> samAuxBytes(const std::string &tags, size_t capacity)
{
    if (tags.empty()) return {};
    std::string line = "aux\t4\t*\t0\t0\t*\t*\t0\t0\t*\t*\t" + tags;
    std::vector<char> storage(line.begin(), line.end());
    storage.push_back('\0');
    kstring_t input = {line.size(), storage.size(), storage.data()};
    std::unique_ptr<sam_hdr_t, decltype(&sam_hdr_destroy)> header(sam_hdr_init(), sam_hdr_destroy);
    std::unique_ptr<bam1_t, decltype(&bam_destroy1)> record(bam_init1(), bam_destroy1);
    if (!header || !record || sam_parse1(&input, header.get(), record.get()) < 0)
        throw std::runtime_error("Invalid SAM auxiliary fields");
    const unsigned char *aux = bam_get_aux(record.get());
    const size_t size = record->data + record->l_data - aux;
    if (size > capacity) throw std::runtime_error("SAM auxiliary fields exceed BAM attribute buffer");
    return {aux, aux + size};
}

#endif
