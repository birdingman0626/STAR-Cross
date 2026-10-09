#ifndef STAR_SEED_DIAGNOSTICS_H
#define STAR_SEED_DIAGNOSTICS_H
#ifdef STAR_CAPTURE_SEEDS
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <stdexcept>
#include <chrono>
#include <array>
#include <memory>

// Diagnostic builds only. Nested worker intervals are not process wall time.
namespace seedTrace {
inline uint64_t mix(uint64_t x) {
    x+=0x9e3779b97f4a7c15ULL;
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;
    return x^(x>>31);
}
inline uint64_t modulus() {
    static const uint64_t value=[] {
        const char* text=std::getenv("STAR_SEED_TRACE_MODULUS");
        if(!text) return uint64_t(1);
        char* end=nullptr;const auto n=std::strtoull(text,&end,10);
        if(!n || *end || *text=='-') throw std::runtime_error("invalid seed trace sample modulus");
        return uint64_t(n);
    }();return value;
}
inline unsigned bucket(uint64_t n) {
    unsigned b=0;while(n>1 && b<63) {n>>=1;++b;}return b;
}
struct Worker {
    std::filesystem::path path;
    std::ofstream queries,seeds;
    uint64_t readId=0,readsSeen=0,readsSelected=0,seen=0,selected=0,recorded=0;
    uint64_t seedRows=0,seedReads=0,prefix=0,compares=0,bases=0,packed=0,ranges=0;
    uint64_t windows=0,windowSeeds=0,transitions=0,recursiveStates=0;
    uint64_t mapNs=0,seedNs=0,extensionNs=0,stitchNs=0,ioNs=0;
    uint64_t inputLockWaitNs=0,inputLockHeldNs=0,inputLockChunks=0;
    uint64_t firstRead=UINT64_MAX,lastRead=0;
    uint64_t firstObserved=UINT64_MAX,lastObserved=0;
    std::array<uint64_t,64> widths{},lengths{},multiplicities{};
    bool sampled=false;
    Worker(const std::filesystem::path& folder,int workerId):
        path(folder/("queries-"+std::to_string(workerId)+".bin")),
        queries(path,std::ios::binary),
        seeds(folder/("seeds-"+std::to_string(workerId)+".jsonl")) {
        queries.write("STARSD01",8);
        if(!queries || !seeds) throw std::runtime_error("seed trace output open failed");
    }
    ~Worker() {
        queries.close();seeds.close();
        // No completion statistics on a failed flush; the collector rejects
        // missing worker receipts without throwing from a destructor.
        if(!queries || !seeds) return;
        std::ofstream out(path.string()+".json");
        out<<"{\"format\":1,\"reads_seen\":"<<readsSeen<<",\"reads_selected\":"<<readsSelected
           <<",\"extension_requests_seen\":"<<seen<<",\"selected\":"<<selected
           <<",\"recorded\":"<<recorded<<",\"dropped\":"<<selected-recorded
           <<",\"limit_per_worker\":5000,\"seed_row_limit_per_worker\":100000,\"sample_modulus\":"<<modulus()
           <<",\"first_selected_read\":"<<(firstRead==UINT64_MAX?0:firstRead)<<",\"last_selected_read\":"<<lastRead
           <<",\"first_observed_read\":"<<(firstObserved==UINT64_MAX?0:firstObserved)<<",\"last_observed_read\":"<<lastObserved
           <<",\"seed_reads\":"<<seedReads<<",\"seed_rows\":"<<seedRows
           <<",\"seed_reads_dropped\":"<<readsSelected-seedReads
           <<",\"prefix_shortcuts\":"<<prefix<<",\"compare_operations\":"<<compares
           <<",\"base_inspections\":"<<bases<<",\"packed_sa_loads\":"<<packed
           <<",\"range_expansions\":"<<ranges<<",\"windows\":"<<windows
           <<",\"window_seeds\":"<<windowSeeds<<",\"transition_evaluations\":"<<transitions
           <<",\"recursive_stitch_states\":"<<recursiveStates
           <<",\"sampled_map_ns\":"<<mapNs<<",\"sampled_seed_ns\":"<<seedNs
           <<",\"sampled_extension_ns\":"<<extensionNs<<",\"sampled_stitch_ns\":"<<stitchNs
           <<",\"capture_io_ns\":"<<ioNs
           <<",\"input_lock_wait_ns\":"<<inputLockWaitNs
           <<",\"input_lock_held_ns\":"<<inputLockHeldNs
           <<",\"input_lock_chunks\":"<<inputLockChunks;
        auto histogram=[&](const char* key,const std::array<uint64_t,64>& h) {
            out<<",\""<<key<<"\":[";for(unsigned i=0;i<64;++i) {if(i) out<<',';out<<h[i];}out<<']';
        };
        histogram("interval_width_log2",widths);histogram("query_length_log2",lengths);
        histogram("multiplicity_log2",multiplicities);out<<'}';
    }
};
inline std::unique_ptr<Worker>& storage() {
    thread_local std::unique_ptr<Worker> value;return value;
}
inline Worker*& borrowedWorker() {thread_local Worker* value=nullptr;return value;}
inline Worker* worker() {return borrowedWorker() ? borrowedWorker() : storage().get();}
struct BorrowedScope {
    Worker* previous;
    explicit BorrowedScope(Worker* worker):previous(borrowedWorker()){borrowedWorker()=worker;}
    ~BorrowedScope(){borrowedWorker()=previous;}
};
struct Lifetime {
    explicit Lifetime(int id) {
        const char* destination=std::getenv("STAR_SEED_TRACE_DIR");
        if(destination) storage()=std::make_unique<Worker>(std::filesystem::path(destination),id);
    }
    ~Lifetime() {storage().reset();}
};
inline void beginRead(uint64_t id) {
    if(auto* w=worker()) {
        w->readId=id;++w->readsSeen;w->sampled=mix(id)%modulus()==0;
        w->firstObserved=std::min(w->firstObserved,id);w->lastObserved=std::max(w->lastObserved,id);
        if(w->sampled) {++w->readsSelected;w->firstRead=std::min(w->firstRead,id);w->lastRead=std::max(w->lastRead,id);}
    }
}
struct Timer {
    Worker* w;uint64_t Worker::*field;
    uint64_t initialIo=0;
    std::chrono::steady_clock::time_point start;
    explicit Timer(uint64_t Worker::*field):w(worker()),field(field) {
        if(w && w->sampled) {initialIo=w->ioNs;start=std::chrono::steady_clock::now();}else w=nullptr;
    }
    ~Timer() {if(w) w->*field+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count()-(w->ioNs-initialIo);}
};
struct IoTimer {
    Worker* w;std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    explicit IoTimer(Worker* w):w(w) {}
    ~IoTimer() {w->ioNs+=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-start).count();}
};
struct Comparison {
    Worker* w;const int64& inspected;uint64_t remaining;
    Comparison(const int64& inspected,uint64_t remaining):w(worker()),inspected(inspected),remaining(remaining) {
        if(w) {++w->compares;++w->packed;}
    }
    ~Comparison() {if(w) w->bases+=std::min<uint64_t>(remaining,uint64_t(inspected)+1);}
};
template<class Table> inline void finalSeeds(uint64_t count,const Table& pc) {
    auto* w=worker();if(!w || !w->sampled || w->seedReads>=5000 || count>100000-w->seedRows) return;
    IoTimer diagnostic(w);
    ++w->seedReads;w->seedRows+=count;
    auto& out=w->seeds;out<<"{\"read_id\":"<<w->readId<<",\"seeds\":[";
    for(uint64_t i=0;i<count;++i) {
        if(i) out<<',';out<<'[';
        // PC_Str is not assigned by storeAligns; record the seven semantic fields.
        for(unsigned j:{PC_rStart,PC_Length,PC_Dir,PC_Nrep,PC_SAstart,PC_SAend,PC_iFrag}) {
            if(j!=PC_rStart) out<<',';out<<pc[i][j];
        }
        out<<']';
    }
    out<<"]}\n";if(!out) throw std::runtime_error("seed table write failed");
}
}
#endif
#endif
