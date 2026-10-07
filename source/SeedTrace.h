#ifndef STAR_SEED_TRACE_H
#define STAR_SEED_TRACE_H
#ifdef STAR_CAPTURE_SEEDS
#include "gpuSeedSearch.h"
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
    const char* destination=std::getenv("STAR_SEED_TRACE_DIR");
    if(!destination) return;
    if(g.P.twoPass.yes)
        throw std::runtime_error("diagnostic seed capture supports one-pass indexes only");
    const std::filesystem::path folder(destination);
    static uint capturedGenomeBytes=0,capturedSaRecords=0;
    static std::once_flag indexOnce;
    std::call_once(indexOnce,[&] {
        auto dump=[&](const char* name,const char* data,uint size) {
            std::ofstream out(folder/name,std::ios::binary);
            out.write(data,size);if(!out) throw std::runtime_error("seed capture index write failed");
        };
        dump("Genome",g.G,g.nGenome);dump("SA",g.SA.charArray,g.SA.lengthByte);
        std::ofstream metadata(folder/"genomeParameters.txt");
        metadata<<"### GstrandBit "<<unsigned(g.GstrandBit)<<"\n### nSA "<<g.nSA<<"\n";
        if(!metadata) throw std::runtime_error("seed capture metadata failed");
        capturedGenomeBytes=g.nGenome;capturedSaRecords=g.nSA;
    });
    if(g.nGenome!=capturedGenomeBytes || g.nSA!=capturedSaRecords)
        throw std::runtime_error("diagnostic seed capture does not support changing index generations");
    struct Worker {
        std::filesystem::path path;
        std::ofstream out;
        uint seen=0,recorded=0;
        explicit Worker(const std::filesystem::path& path):path(path),out(path,std::ios::binary) {
            out.write("STARSD01",8);
        }
        ~Worker() {
            out.close();std::ofstream stats(path.string()+".json");
            stats<<"{\"format\":1,\"extension_requests_seen\":"<<seen
                 <<",\"recorded\":"<<recorded<<",\"dropped\":"<<seen-recorded
                 <<",\"limit_per_worker\":5000}";
        }
    };
    thread_local Worker worker(folder/("queries-"+std::to_string(omp_get_thread_num())+".bin"));
    ++worker.seen;
    if(worker.recorded>=5000) return;
    ++worker.recorded;
    auto& out=worker.out;
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
