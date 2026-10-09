#ifndef STAR_TEST_SEED_RANK_HINT_H
#define STAR_TEST_SEED_RANK_HINT_H
#include "SuffixArrayFuns.h"
#include "gpuSeedSearch.h"
#include <cmath>
#include <stdexcept>

// Sampled predictor feasibility only: neither model certifies a rank bound.
// Every usable prediction is one probe inside the original exact search domain.
class SeedRankHint {
    struct Point {uint64_t key,rank;};
    struct Segment {uint64_t key,rank;double slope;};
    std::vector<Point> points;
    std::vector<double> bins;
    std::vector<Segment> segments;
    uint64_t lastRank=0,keySpace=0;
    unsigned k=0;
    static constexpr size_t memoryLimit=64*1024*1024;
    static double middle(double low,double high) {return std::isfinite(high)?low+(high-low)/2:low;}
    double interpolate(uint64_t key) const {
        auto upper=std::lower_bound(points.begin(),points.end(),key,[](const Point& p,uint64_t x){return p.key<x;});
        if(upper==points.begin()) return double(upper->rank);
        if(upper==points.end()) return double(points.back().rank);
        const auto& lower=*(upper-1);
        return double(lower.rank)+double(upper->rank-lower.rank)*double(key-lower.key)/double(upper->key-lower.key);
    }
public:
    uint64_t sampledRanks=0,validSamples=0,duplicateSamples=0;
    SeedRankHint(Genome& g,const std::string& kind,unsigned width,uint64_t error,size_t sampleCount=65536,size_t binCount=4096):
        lastRank(g.nSA?g.nSA-1:0),k(width) {
        if((kind!="pwl" && kind!="pla") || (k!=14 && k!=18 && k!=21) || !error || !g.nSA
           || sampleCount<2 || sampleCount>65536 || !binCount || binCount>65536)
            throw std::invalid_argument("unsupported bounded rank-hint parameters");
        keySpace=1ULL<<(2*k);
        const size_t count=static_cast<size_t>(std::min<uint64_t>(sampleCount,g.nSA));
        points.reserve(count);
        for(size_t i=0;i<count;++i) {
            // Uniform SA-rank sampling without a rank*sampleCount overflow.
            const uint64_t rank=count==1?0:(lastRank/(count-1))*i+(lastRank%(count-1))*i/(count-1);
            ++sampledRanks;
            const uint64_t packed=g.SA[rank],position=packed&g.GstrandMask;
            const bool reverse=(packed>>g.GstrandBit)!=0;
            if(position>=g.nGenome) throw std::runtime_error("sampled packed suffix lies outside genome");
            if(k>g.nGenome-position) continue;
            uint64_t key=0;bool valid=true;
            for(unsigned j=0;j<k;++j) {
                const unsigned char code=g.G[reverse?g.nGenome-1-position-j:position+j];
                if(code>3) {valid=false;break;}
                key=(key<<2)|(reverse?3-code:code);
            }
            if(!valid) continue;
            ++validSamples;
            if(!points.empty() && key<points.back().key) throw std::runtime_error("sampled valid kmer keys are not monotone in packed suffix order");
            if(!points.empty() && key==points.back().key) {++duplicateSamples;continue;}
            points.push_back({key,rank});
        }
        if(points.empty()) throw std::runtime_error("no valid sampled kmer keys for rank predictor");
        if(kind=="pwl") {
            bins.reserve(binCount+1);
            for(size_t i=0;i<=binCount;++i) bins.push_back(interpolate(uint64_t(i)*keySpace/binCount));
        } else {
            size_t anchor=0;double low=0,high=std::numeric_limits<double>::infinity();
            for(size_t i=1;i<points.size();++i) {
                const double dx=double(points[i].key-points[anchor].key),dy=double(points[i].rank-points[anchor].rank);
                const double nextLow=std::max(low,(dy-double(error))/dx),nextHigh=std::min(high,(dy+double(error))/dx);
                if(nextLow>nextHigh) {
                    segments.push_back({points[anchor].key,points[anchor].rank,middle(low,high)});
                    anchor=i-1;
                    const double localDx=double(points[i].key-points[anchor].key),localDy=double(points[i].rank-points[anchor].rank);
                    low=std::max(0.0,(localDy-double(error))/localDx);high=(localDy+double(error))/localDx;
                } else {low=nextLow;high=nextHigh;}
            }
            segments.push_back({points[anchor].key,points[anchor].rank,middle(low,high)});
        }
        if(modelBytes()>memoryLimit) throw std::runtime_error("rank predictor exceeds model memory budget");
    }
    size_t modelBytes() const {return points.capacity()*sizeof(Point)+bins.capacity()*sizeof(double)+segments.capacity()*sizeof(Segment);}
    size_t uniqueKeys() const {return points.size();}
    size_t pieces() const {return bins.empty()?segments.size():bins.size()-1;}
    bool probe(const GpuSeedQuery& query,char* const reads[2],uint& rank) const {
        if(query.length<k) return false;
        uint64_t key=0;
        for(unsigned j=0;j<k;++j) {
            const unsigned char code=reads[query.forward?0:1][query.forward?query.start+j:query.start-j];
            if(code>3) return false;
            key=(key<<2)|code;
        }
        if(key<points.front().key || key>points.back().key) return false;
        double prediction;
        if(!bins.empty()) {
            const uint64_t scaled=key*(bins.size()-1),bin=scaled/keySpace;
            prediction=bins[bin]+(bins[bin+1]-bins[bin])*double(scaled%keySpace)/double(keySpace);
        } else {
            auto segment=std::upper_bound(segments.begin(),segments.end(),key,[](uint64_t x,const Segment& s){return x<s.key;});
            --segment;prediction=double(segment->rank)+segment->slope*double(key-segment->key);
        }
        if(!std::isfinite(prediction) || prediction<0) return false;
        rank=prediction>=double(lastRank)?lastRank:static_cast<uint64_t>(prediction);
        return rank>query.lower && rank<query.upper;
    }
};
#endif
