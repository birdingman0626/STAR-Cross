#ifndef STAR_SEED_TRACE_H
#define STAR_SEED_TRACE_H
#ifdef STAR_CAPTURE_SEEDS
#include "gpuSeedSearch.h"
#include "SeedDiagnostics.h"
#include <filesystem>
#include <fstream>
#include <mutex>
#include <cstdlib>
#include <stdexcept>

// Diagnostic build only. Dump the actual augmented index once, then bounded
// per-worker requests after clipping/prefix selection; never part of timed builds.
inline void captureSeed(Genome& g,char** reads,uint bytes,uint readId,uint start,uint length,
                        bool forward,uint lower,uint upper,uint initial,uint resultLength,
                        uint* range,uint count) {
    auto* worker=seedTrace::worker();
    if(!worker) return;
    seedTrace::IoTimer diagnostic(worker);
    const char* destination=std::getenv("STAR_SEED_TRACE_DIR");
    if(g.P.twoPass.yes)
        throw std::runtime_error("diagnostic seed capture supports one-pass indexes only");
    const std::filesystem::path folder(destination);
    static uint capturedGenomeBytes=0,capturedSaRecords=0,capturedSaBytes=0;
    static const char* capturedGenome=nullptr;
    static const char* capturedSa=nullptr;
    static std::once_flag indexOnce;
    std::call_once(indexOnce,[&] {
        auto dump=[&](const char* name,const char* data,uint size) {
            std::ofstream out(folder/name,std::ios::binary);
            out.write(data,size);if(!out) throw std::runtime_error("seed capture index write failed");
        };
        dump("Genome",g.G,g.nGenome);dump("SA",g.SA.charArray,g.SA.lengthByte);
        std::ofstream metadata(folder/"genomeParameters.txt");
        metadata<<"### GstrandBit "<<unsigned(g.GstrandBit)<<"\n### nSA "<<g.nSA<<"\n";
        const uint16_t endian=1;
        std::ofstream abi(folder/"capture_metadata.json");
        abi<<"{\"format\":1,\"native_uint_bytes\":"<<sizeof(uint)
           <<",\"query_bytes\":"<<sizeof(GpuSeedQuery)<<",\"match_bytes\":"<<sizeof(GpuSeedMatch)
           <<",\"byte_order\":\""<<(*reinterpret_cast<const char*>(&endian)?"little":"big")
           <<"\",\"sampling\":\"splitmix64(read_id)%modulus==0\",\"sample_modulus\":"<<seedTrace::modulus()
           <<",\"limit_per_worker\":5000,\"seed_row_limit_per_worker\":100000,\"mode\":\"one_pass\",\"index_mutation\":\"unsupported\"}";
        if(!metadata || !abi) throw std::runtime_error("seed capture metadata failed");
        capturedGenomeBytes=g.nGenome;capturedSaRecords=g.nSA;capturedSaBytes=g.SA.lengthByte;
        capturedGenome=g.G;capturedSa=g.SA.charArray;
    });
    if(g.G!=capturedGenome || g.SA.charArray!=capturedSa || g.nGenome!=capturedGenomeBytes
       || g.nSA!=capturedSaRecords || g.SA.lengthByte!=capturedSaBytes)
        throw std::runtime_error("diagnostic seed capture does not support changing index generations");
    ++worker->seen;++worker->widths[seedTrace::bucket(upper-lower+1)];
    ++worker->lengths[seedTrace::bucket(length)];++worker->multiplicities[seedTrace::bucket(count)];
    if(!worker->sampled) return;
    ++worker->selected;
    if(worker->recorded>=5000) return;
    ++worker->recorded;
    auto& out=worker->queries;
    GpuSeedQuery q{0,bytes,start,length,lower,upper,initial,uint64_t(forward)};
    GpuSeedMatch expected{resultLength,range[0],range[1],count};
    out.write(reinterpret_cast<const char*>(&readId),sizeof(readId));
    out.write(reinterpret_cast<const char*>(&q),sizeof(q));
    out.write(reinterpret_cast<const char*>(&expected),sizeof(expected));
    out.write(reads[0],bytes);
    if(!out) throw std::runtime_error("seed capture request write failed");
}
#endif
#endif
