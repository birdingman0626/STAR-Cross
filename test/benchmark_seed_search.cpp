#include "gpuSeedSearch.h"
#include "SuffixArrayFuns.h"
#include "SeedRankHint.h"
#include "SeedTestIndex.h"
#include "SeedSearch.h"
#include <filesystem>
#include <chrono>
#include <stdexcept>
#include <memory>
#include <nlohmann/json.hpp>
#ifdef _WIN32
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace {
using Clock=std::chrono::steady_clock;
using Json=nlohmann::json;
double elapsed(Clock::time_point t) {return std::chrono::duration<double>(Clock::now()-t).count();}
Json peakMemory() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX info{};info.cb=sizeof(info);
    if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&info),sizeof(info)))
        return {{"status","MEASURED"},{"method","Windows peak working set"},{"peak_rss_bytes",info.PeakWorkingSetSize},
                {"peak_commit_bytes",info.PeakPagefileUsage}};
#else
    rusage usage{};
    if(getrusage(RUSAGE_SELF,&usage)==0) {
#ifdef __APPLE__
        const uint64_t factor=1;
#else
        const uint64_t factor=1024;
#endif
        return {{"status","MEASURED"},{"method","getrusage process lifetime peak RSS"},
                {"peak_rss_bytes",uint64_t(usage.ru_maxrss)*factor}};
    }
#endif
    return {{"status","UNVERIFIED"}};
}
std::vector<char> load(const std::filesystem::path& path,size_t padding=0) {
    std::ifstream f(path,std::ios::binary);if(!f) throw std::runtime_error("cannot open index file");
    const auto bytes=std::filesystem::file_size(path);
    if(padding>std::numeric_limits<size_t>::max()/2 || bytes>std::numeric_limits<size_t>::max()-2*padding)
        throw std::runtime_error("index file exceeds addressable memory");
    std::vector<char> data(static_cast<size_t>(bytes)+2*padding,5);
    f.read(data.data()+padding,bytes);if(!f) throw std::runtime_error("incomplete index read");return data;
}
bool same(const GpuSeedMatch& a,const GpuSeedMatch& b) {
    return a.length==b.length && a.lower==b.lower && a.upper==b.upper && a.multiplicity==b.multiplicity;
}
}
int main(int argc,char** argv) try {
    if(argc<3 || argc>10) throw std::invalid_argument("usage: star_seed_benchmark genomeDir R2.fastq|captureDir [records=100000] [batch=65536] [repeats=3] [cpuThreads=8] [executor=cpu|cuda|pwl|pla|hint-sweep] [hintK=18] [hintError=64]");
    const size_t limit=argc>3?std::stoull(argv[3]):100000,batch=argc>4?std::stoull(argv[4]):65536;
    const int repeats=argc>5?std::stoi(argv[5]):3,threads=argc>6?std::stoi(argv[6]):8;
    const std::string executor=argc>7?argv[7]:"cpu";
    const unsigned hintK=argc>8?std::stoul(argv[8]):18;
    const uint64_t hintError=argc>9?std::stoull(argv[9]):64;
    if(executor!="cpu" && executor!="cuda" && executor!="pwl" && executor!="pla" && executor!="hint-sweep")
        throw std::invalid_argument("unsupported executor");
    if((hintK!=14 && hintK!=18 && hintK!=21) || (hintError!=16 && hintError!=64 && hintError!=256))
        throw std::invalid_argument("hint k/error outside declared screening grid");
#ifndef STAR_SEED_REPLAY_CUDA
    if(executor=="cuda") throw std::invalid_argument("CUDA executor was not built; use cpu or enable STAR_ENABLE_CUDA");
#endif
    if(!limit || !batch || repeats<1 || threads<1) throw std::invalid_argument("positive run parameters required");
    std::filesystem::path root(argv[1]);auto genome=load(root/"Genome",200),sa=load(root/"SA");
    SeedTestIndex g;g.G=genome.data()+200;g.nGenome=genome.size()-400;
    std::ifstream parameters(root/"genomeParameters.txt");std::string line;
    uint capturedRecords=0;
    while(std::getline(parameters,line)) {
        if(line.rfind("### GstrandBit ",0)==0) g.GstrandBit=std::stoi(line.substr(15));
        if(line.rfind("### nSA ",0)==0) capturedRecords=std::stoull(line.substr(8));
    }
    if(g.GstrandBit<8 || g.GstrandBit>55) throw std::runtime_error("unsupported strand bit");
    g.GstrandMask=(1ULL<<g.GstrandBit)-1;g.nSA=sa.size()*8/(g.GstrandBit+1);
    if(capturedRecords) g.nSA=capturedRecords;
    g.SA.defineBits(g.GstrandBit+1,g.nSA);g.SA.pointArray(sa.data());
    if(g.SA.lengthByte!=sa.size()) throw std::runtime_error("packed SA geometry mismatch");
    std::vector<char> reads,complement;std::vector<GpuSeedQuery> queries;
    std::vector<GpuSeedMatch> captured;
    std::vector<uint> readIds;
    std::vector<std::string> captureSources;
    std::string captureAbi="NOT_APPLICABLE";
    size_t observed=0,skipped=0,availableCaptureRecords=0;
    if(std::filesystem::is_directory(argv[2])) {
        const auto metadataPath=std::filesystem::path(argv[2])/"capture_metadata.json";
        captureAbi="UNVERIFIED_LEGACY_NATIVE_ABI";
        if(std::filesystem::exists(metadataPath)) {
            std::ifstream metadataFile(metadataPath);Json metadata;metadataFile>>metadata;
            const uint16_t endian=1;
            const std::string byteOrder=*reinterpret_cast<const unsigned char*>(&endian)?"little":"big";
            if(metadata.at("format")!=1 || metadata.at("native_uint_bytes")!=sizeof(uint)
               || metadata.at("query_bytes")!=sizeof(GpuSeedQuery) || metadata.at("match_bytes")!=sizeof(GpuSeedMatch)
               || metadata.at("byte_order")!=byteOrder) throw std::runtime_error("capture native ABI mismatch");
            captureAbi="VERIFIED_NATIVE_ABI";
        }
        std::vector<std::filesystem::path> files;
        for(const auto& entry:std::filesystem::directory_iterator(argv[2]))
            if(entry.path().extension()==".bin" && entry.path().filename().string().rfind("queries-",0)==0)
                files.push_back(entry.path());
        std::sort(files.begin(),files.end());
        for(const auto& path:files) {
            std::ifstream input(path,std::ios::binary);uint readId;
            char magic[8];input.read(magic,8);
            if(!input || std::string(magic,8)!="STARSD01") throw std::runtime_error("unknown capture format");
            uint fileRecords=0;
            for(;;) {
                input.read(reinterpret_cast<char*>(&readId),sizeof(readId));
                if(input.gcount()==0 && input.eof()) break;
                if(!input || input.gcount()!=sizeof(readId)) throw std::runtime_error("truncated capture header");
                GpuSeedQuery q;GpuSeedMatch match;
                input.read(reinterpret_cast<char*>(&q),sizeof(q));
                input.read(reinterpret_cast<char*>(&match),sizeof(match));
                if(!input || !q.readBytes || q.readBytes>16*1024*1024)
                    throw std::runtime_error("malformed or over-capacity captured query");
                std::vector<char> record(q.readBytes);
                input.read(record.data(),q.readBytes);
                if(!input) throw std::runtime_error("truncated captured read");
                if(!q.length || q.initialLength>q.length || q.forward>1 || q.lower>q.upper || q.upper>=g.nSA
                   || q.start>=q.readBytes || (q.forward?q.length>q.readBytes-q.start:q.length>q.start+1))
                    throw std::runtime_error("invalid captured replay query");
                ++fileRecords;
                ++availableCaptureRecords;
                if(observed>=limit) continue; // Still validate every trailing record, including capped inputs.
                if(q.readBytes>16*1024*1024-reads.size()) throw std::runtime_error("captured read pool exceeds 16 MiB; reduce read count");
                q.offset=reads.size();reads.insert(reads.end(),record.begin(),record.end());
                queries.push_back(q);captured.push_back(match);readIds.push_back(readId);
                captureSources.push_back(path.filename().string());++observed;
            }
            const auto statsPath=std::filesystem::path(path.string()+".json");
            if(std::filesystem::exists(statsPath)) {
                std::ifstream statsFile(statsPath);Json stats;statsFile>>stats;
                if(stats.at("recorded")!=fileRecords) throw std::runtime_error("capture recorded count does not match file");
            }
        }
        for(char code:reads) complement.push_back(code<4?3-code:code);
    } else {
    // Use the actual frozen prefix index to capture production-like search bounds.
    std::ifstream prefix(root/"SAindex",std::ios::binary);uint nbases=0;
    prefix.read(reinterpret_cast<char*>(&nbases),8);if(nbases<1 || nbases>16) throw std::runtime_error("invalid prefix width");
    std::vector<uint> starts(nbases+1);prefix.read(reinterpret_cast<char*>(starts.data()),starts.size()*8);
    PackedArray sai;sai.defineBits(g.GstrandBit+3,starts.back());std::vector<char> prefixBytes(sai.lengthByte);
    prefix.read(prefixBytes.data(),prefixBytes.size());if(!prefix) throw std::runtime_error("incomplete prefix index");sai.pointArray(prefixBytes.data());
    const uint nmark=1ULL<<(g.GstrandBit+1),absent=1ULL<<(g.GstrandBit+2);
    std::ifstream fastq(argv[2]);if(!fastq) throw std::runtime_error("cannot open FASTQ");
    std::string name,sequence,plus,quality;
    while(observed<limit && std::getline(fastq,name)) {
        if(!std::getline(fastq,sequence) || !std::getline(fastq,plus) || !std::getline(fastq,quality)) throw std::runtime_error("truncated FASTQ");
        for(auto* s:{&name,&sequence,&plus,&quality}) if(!s->empty() && s->back()=='\r') s->pop_back();
        if(name.empty() || name[0]!='@' || plus.empty() || plus[0]!='+' || sequence.size()!=quality.size()) throw std::runtime_error("invalid FASTQ");
        if(reads.size()+sequence.size()>16*1024*1024) throw std::runtime_error("read pool exceeds 16 MiB; reduce read count");
        const uint offset=reads.size();++observed;
        for(char base:sequence) {
            char code=base=='A'?0:base=='C'?1:base=='G'?2:base=='T'?3:4;
            reads.push_back(code);complement.push_back(code<4?3-code:code);
        }
        for(uint begin=0;begin<sequence.size();) {
            while(begin<sequence.size() && reads[offset+begin]>3) ++begin;
            uint end=begin;while(end<sequence.size() && reads[offset+end]<4) ++end;
            if(end-begin>=6) for(uint forward:{1ULL,0ULL}) {
                uint start=forward?begin:end-1,length=end-begin,lmax=std::min<uint>(nbases,length),key=0;
                for(uint i=0;i<lmax;++i) key=(key<<2)+(forward?reads[offset+start+i]:complement[offset+start-i]);
                uint lind=lmax,lo=0,hi=g.nSA-1;
                while(lind>0) {lo=sai[starts[lind-1]+key];if(!(lo&absent)) break;--lind;key>>=2;}
                if(!lind) {++skipped;continue;} // do not emulate the caller's Lind==0 boundary.
                bool upperGood=false;
                if(starts[lind-1]+key+1<starts[lind]) {
                    uint next=sai[starts[lind-1]+key+1];if(!(next&absent)) {hi=(next&~nmark)-1;upperGood=true;}
                }
                bool noN=!(lo&nmark);
                if(lind<nbases && noN && upperGood) {++skipped;continue;} // actual CPU caller's prefix-only shortcut.
                queries.push_back({offset,sequence.size(),start,length,lo&~nmark,hi,noN&&upperGood?lind:0,forward});
            }
            begin=end+1;
        }
    }
    }
    if(queries.empty()) throw std::runtime_error("no captured extension searches");
    for(const auto& q:queries)
        if(!q.length || q.initialLength>q.length || q.forward>1 || q.lower>q.upper || q.upper>=g.nSA
           || q.offset>reads.size() || q.readBytes>reads.size()-q.offset || q.start>=q.readBytes
           || (q.forward ? q.length>q.readBytes-q.start : q.length>q.start+1))
            throw std::runtime_error("invalid replay query");
    std::vector<GpuSeedMatch> expected(queries.size()),cpu(queries.size());
    const SeedSearchEngine engine(g);
    auto cpuRun=[&, threads](std::vector<GpuSeedMatch>& out) {
        auto start=Clock::now();
        engine.searchBatch(reads.data(),complement.data(),reads.size(),queries.data(),queries.size(),out.data(),threads);
        return elapsed(start);
    };
    const double cpuWarmup=cpuRun(expected);
    for(size_t i=0;i<captured.size();++i)
        if(!same(captured[i],expected[i])) throw std::runtime_error("captured production CPU result differs in replay");
    Json output={{"status","PASS"},{"executor",executor},{"scope","search length/lower/upper/multiplicity only; emitted seeds and full mapping require integration verification"},
                 {"input_mode",captured.empty()?"FASTQ":"POST_CLIPPING_CAPTURE"},{"capture_abi",captureAbi},
                 {"input_records",observed},{"queries",queries.size()},{"skipped_prefix_cases",skipped},
                 {"cpu_threads",threads},{"batch",batch},{"cpu_warmup_seconds",cpuWarmup},
                 {"cache","warmup performed; OS cache not flushed; no cold-cache claim"},{"runs",Json::array()}};
    if(!captured.empty()) {
        output["available_capture_records"]=availableCaptureRecords;
        output["replay_limit_omitted_records"]=availableCaptureRecords-observed;
        output["captured_read_ids"]=readIds;
        output["capture_sources"]=captureSources;
    }
    if(executor=="cpu") {
        for(int repeat=0;repeat<repeats;++repeat) {
            double seconds=cpuRun(cpu);
            for(size_t i=0;i<cpu.size();++i) if(!same(cpu[i],expected[i])) throw std::runtime_error("CPU nondeterminism");
            output["runs"].push_back({{"order","CPU"},{"cpu_seconds",seconds}});
        }
        output["memory"]=peakMemory();std::cout<<output.dump()<<"\n";return 0;
    }
    if(executor=="pwl" || executor=="pla" || executor=="hint-sweep") {
        output["hint_scope"]="reference-trained sampled rank hints only; no global rank certificate or predicted pruning; exact original eligible bounds";
        output["experiments"]=Json::array();
        auto experiment=[&, threads](const std::string& kind,unsigned width,uint64_t error) {
            auto building=Clock::now();SeedRankHint model(g,kind,width,error);
            const double buildSeconds=elapsed(building);
            std::vector<GpuSeedMatch> candidate(queries.size());
            // MSVC does not infer lambda captures from OpenMP pragma clauses.
            auto candidateRun=[&, threads]() {
                uint64_t used=0;auto start=Clock::now();
                #pragma omp parallel for num_threads(threads) reduction(+:used)
                for(long long i=0;i<static_cast<long long>(queries.size());++i) {
                    const auto& q=queries[i];char* s[2]={reads.data()+q.offset,complement.data()+q.offset};
                    uint length=q.initialLength,range[2],probe=0;
                    const bool supported=model.probe(q,s,probe);
                    const uint count=supported?maxMappableLengthHint(g,s,q.start,q.length,q.lower,q.upper,q.forward,length,range,probe)
                                              :maxMappableLength(g,s,q.start,q.length,q.lower,q.upper,q.forward,length,range);
                    if(supported) ++used;
                    candidate[i]={length,range[0],range[1],count};
                }
                return std::make_pair(elapsed(start),used);
            };
            auto verify=[&](const std::vector<GpuSeedMatch>& actual,const char* label) {
                for(size_t i=0;i<actual.size();++i) if(!same(actual[i],expected[i]))
                    throw std::runtime_error(std::string(label)+" search field mismatch at query "+std::to_string(i));
            };
            const auto warm=candidateRun();verify(candidate,"hint warmup");
            Json item={{"model",kind},{"k",width},{"error",error},{"error_role",kind=="pla"?"sampled slope-envelope target only; uncertified globally":"unused by fixed-bin PWL"},
                       {"sampled_sa_ranks",model.sampledRanks},{"valid_samples",model.validSamples},{"duplicate_samples",model.duplicateSamples},
                       {"unique_sampled_keys",model.uniqueKeys()},{"pieces",model.pieces()},{"model_bytes",model.modelBytes()},
                       {"construction_extra_bytes_upper_bound",2*model.modelBytes()},
                       {"build_seconds",buildSeconds},{"candidate_warmup_seconds",warm.first},{"hint_usable_queries",warm.second},
                       {"hint_usable_fraction",double(warm.second)/queries.size()},{"runs",Json::array()}};
            for(int repeat=0;repeat<repeats;++repeat) {
                double baselineSeconds;std::pair<double,uint64_t> measured;
                if(repeat%2) {measured=candidateRun();baselineSeconds=cpuRun(cpu);}
                else {baselineSeconds=cpuRun(cpu);measured=candidateRun();}
                verify(cpu,"CPU");verify(candidate,"hint");
                if(measured.second!=warm.second) throw std::runtime_error("rank hint usability is nondeterministic");
                item["runs"].push_back({{"order",repeat%2?"HINT-CPU":"CPU-HINT"},{"cpu_seconds",baselineSeconds},
                    {"candidate_seconds",measured.first},{"candidate_to_cpu_ratio",measured.first/baselineSeconds}});
            }
            item["memory"]=peakMemory();output["experiments"].push_back(std::move(item));
        };
        if(executor=="hint-sweep") {
            for(unsigned width:{14U,18U,21U}) {
                experiment("pwl",width,64);
                for(uint64_t error:{16ULL,64ULL,256ULL}) experiment("pla",width,error);
            }
        } else experiment(executor,hintK,hintError);
        output["memory"]=peakMemory();std::cout<<output.dump()<<"\n";return 0;
    }
#ifdef STAR_SEED_REPLAY_CUDA
    auto allocationStart=Clock::now();GpuSeedIndex device(g.G,g.nGenome,g.SA.charArray,g.SA.lengthByte,g.nSA,g.GstrandBit,batch);
    double setup=elapsed(allocationStart);
    auto gpuRun=[&](bool verify,GpuSeedTiming& totals) {
        auto start=Clock::now();double validation=0;
        for(size_t offset=0;offset<queries.size();offset+=batch) {
            std::vector<GpuSeedQuery> chunk(queries.begin()+offset,queries.begin()+std::min(offset+batch,queries.size()));
            GpuSeedTiming timing;auto actual=device.search(reads,chunk,timing);
            totals.upload+=timing.upload;totals.kernel+=timing.kernel;totals.download+=timing.download;
            auto checking=Clock::now();if(verify) for(size_t i=0;i<actual.size();++i)
                if(!same(actual[i],expected[offset+i])) {
                    const auto& q=queries[offset+i];const auto& e=expected[offset+i];const auto& a=actual[i];
                    throw std::runtime_error("CPU/CUDA seed field mismatch at query "+std::to_string(offset+i)
                        +" dir="+std::to_string(q.forward)+" N="+std::to_string(q.length)+" L="+std::to_string(q.initialLength)
                        +" bounds="+std::to_string(q.lower)+","+std::to_string(q.upper)
                        +" expected="+std::to_string(e.length)+","+std::to_string(e.lower)+","+std::to_string(e.upper)
                        +" actual="+std::to_string(a.length)+","+std::to_string(a.lower)+","+std::to_string(a.upper));
                }
            validation+=elapsed(checking);
        }
        return elapsed(start)-validation;
    };
    GpuSeedTiming warm;double warmSeconds=gpuRun(true,warm);
    output["device_buffer_bytes"]=device.deviceBytes;output["index_upload_seconds"]=device.indexUploadSeconds;
    output["index_setup_seconds"]=setup;output["warmup_seconds"]=warmSeconds;
    for(int repeat=0;repeat<repeats;++repeat) {
        GpuSeedTiming timing;double gpuSeconds=0,cpuSeconds=0;
        if(repeat%2) {gpuSeconds=gpuRun(true,timing);cpuSeconds=cpuRun(cpu);}
        else {cpuSeconds=cpuRun(cpu);gpuSeconds=gpuRun(true,timing);}
        for(size_t i=0;i<cpu.size();++i) if(!same(cpu[i],expected[i])) throw std::runtime_error("CPU nondeterminism");
        output["runs"].push_back({{"order",repeat%2?"GPU-CPU":"CPU-GPU"},{"cpu_seconds",cpuSeconds},
            {"gpu_seconds",gpuSeconds},{"upload_seconds",timing.upload},{"kernel_seconds",timing.kernel},{"download_seconds",timing.download}});
    }
    output["memory"]=peakMemory();std::cout<<output.dump()<<"\n";return 0;
#else
    throw std::runtime_error("unreachable unavailable executor");
#endif
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
