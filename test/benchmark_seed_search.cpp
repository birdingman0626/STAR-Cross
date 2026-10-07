#include "gpuSeedSearch.h"
#include "SuffixArrayFuns.h"
#include <filesystem>
#include <chrono>
#include <stdexcept>
#include <memory>

// This isolated executable only needs a valid holder for production search fields.
// It does not link STAR's configuration/IO constructors or replace search logic.
Parameters::Parameters() {}
Genome::Genome(Parameters& p,ParametersGenome& pg):P(p),pGe(pg),sharedMemory(nullptr) {}
namespace {
using Clock=std::chrono::steady_clock;
double elapsed(Clock::time_point t) {return std::chrono::duration<double>(Clock::now()-t).count();}
std::vector<char> load(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);if(!f) throw std::runtime_error("cannot open index file");
    std::vector<char> data(std::filesystem::file_size(path));
    f.read(data.data(),data.size());if(!f) throw std::runtime_error("incomplete index read");return data;
}
bool same(const GpuSeedMatch& a,const GpuSeedMatch& b) {
    return a.length==b.length && a.lower==b.lower && a.upper==b.upper && a.multiplicity==b.multiplicity;
}
}
int main(int argc,char** argv) try {
    if(argc<3 || argc>7) throw std::invalid_argument("usage: star_seed_benchmark genomeDir R2.fastq|captureDir [records=100000] [batch=65536] [repeats=3] [cpuThreads=8]");
    const size_t limit=argc>3?std::stoull(argv[3]):100000,batch=argc>4?std::stoull(argv[4]):65536;
    const int repeats=argc>5?std::stoi(argv[5]):3,threads=argc>6?std::stoi(argv[6]):8;
    if(!limit || !batch || repeats<1 || threads<1) throw std::invalid_argument("positive run parameters required");
    std::filesystem::path root(argv[1]);auto genome=load(root/"Genome"),sa=load(root/"SA");
    std::vector<char> guardedGenome(genome.size()+400,5);
    std::copy(genome.begin(),genome.end(),guardedGenome.begin()+200);
    Parameters p;Genome g(p,p.pGe);g.G=guardedGenome.data()+200;g.nGenome=genome.size();g.GstrandBit=0;
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
    size_t observed=0,skipped=0;
    if(std::filesystem::is_directory(argv[2])) {
        std::vector<std::filesystem::path> files;
        for(const auto& entry:std::filesystem::directory_iterator(argv[2]))
            if(entry.path().extension()==".bin") files.push_back(entry.path());
        std::sort(files.begin(),files.end());
        for(const auto& path:files) {
            std::ifstream input(path,std::ios::binary);uint readId;
            char magic[8];input.read(magic,8);
            if(!input || std::string(magic,8)!="STARSD01") throw std::runtime_error("unknown capture format");
            while(observed<limit && input.read(reinterpret_cast<char*>(&readId),sizeof(readId))) {
                GpuSeedQuery q;GpuSeedMatch match;
                input.read(reinterpret_cast<char*>(&q),sizeof(q));
                input.read(reinterpret_cast<char*>(&match),sizeof(match));
                if(!input || !q.readBytes || q.readBytes>16*1024*1024-reads.size())
                    throw std::runtime_error("malformed or over-capacity captured query");
                q.offset=reads.size();reads.resize(reads.size()+q.readBytes);
                input.read(reads.data()+q.offset,q.readBytes);
                if(!input) throw std::runtime_error("truncated captured read");
                queries.push_back(q);captured.push_back(match);++observed;
            }
            if(input.gcount()!=0 && observed<limit) throw std::runtime_error("truncated capture header");
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
    auto cpuRun=[&](std::vector<GpuSeedMatch>& out) {
        auto start=Clock::now();
        #pragma omp parallel for num_threads(threads)
        for(long long i=0;i<static_cast<long long>(queries.size());++i) {
            const auto& q=queries[i];char* s[2]={reads.data()+q.offset,complement.data()+q.offset};
            uint length=q.initialLength,range[2];uint count=maxMappableLength(g,s,q.start,q.length,q.lower,q.upper,q.forward,length,range);
            out[i]={length,range[0],range[1],count};
        }
        return elapsed(start);
    };
    cpuRun(expected);
    for(size_t i=0;i<captured.size();++i)
        if(!same(captured[i],expected[i])) throw std::runtime_error("captured production CPU result differs in replay");
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
    std::ostringstream output;
    output<<"{\"status\":\"PASS\",\"input_mode\":\""<<(captured.empty()?"FASTQ":"POST_CLIPPING_CAPTURE")
        <<"\",\"input_records\":"<<observed<<",\"queries\":"<<queries.size()<<",\"skipped_prefix_cases\":"<<skipped
        <<",\"cpu_threads\":"<<threads<<",\"batch\":"<<batch<<",\"device_buffer_bytes\":"<<device.deviceBytes
        <<",\"index_upload_seconds\":"<<device.indexUploadSeconds<<",\"index_setup_seconds\":"<<setup<<",\"warmup_seconds\":"<<warmSeconds<<",\"runs\":[";
    for(int repeat=0;repeat<repeats;++repeat) {
        GpuSeedTiming timing;double gpuSeconds=0,cpuSeconds=0;
        if(repeat%2) {gpuSeconds=gpuRun(true,timing);cpuSeconds=cpuRun(cpu);}
        else {cpuSeconds=cpuRun(cpu);gpuSeconds=gpuRun(true,timing);}
        for(size_t i=0;i<cpu.size();++i) if(!same(cpu[i],expected[i])) throw std::runtime_error("CPU nondeterminism");
        if(repeat) output<<",";
        output<<"{\"order\":\""<<(repeat%2?"GPU-CPU":"CPU-GPU")<<"\",\"cpu_seconds\":"<<cpuSeconds<<",\"gpu_seconds\":"<<gpuSeconds<<",\"upload_seconds\":"<<timing.upload
            <<",\"kernel_seconds\":"<<timing.kernel<<",\"download_seconds\":"<<timing.download<<"}";
    }
    output<<"]}\n";std::cout<<output.str();return 0;
} catch(const std::exception& e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}
