// Device-side bridge from ordered seeds to alignment windows. Expansion retains
// SA order; the CPU consumes these coordinates twice, without re-reading the SA.
struct HitTask { unsigned node,offset; U readLength; };
__global__ void expandSeedHits(Index index,const SeedNode* nodes,const HitTask* tasks,
        unsigned taskCount,SeedProjectedHit* hits) {
    const unsigned taskId=blockIdx.x;
    if(taskId>=taskCount) return;
    const auto task=tasks[taskId];const auto& seed=nodes[task.node].emission;
    const U length=seed.match.length;
    const U start=seedStart(seed);
    for(U offset=threadIdx.x;offset<=seed.match.upper-seed.match.lower;offset+=blockDim.x) {
        const U packedHit=packed(index,seed.match.lower+offset);
        U genome=packedHit & ((U(1)<<index.strand)-1);
        U strand=packedHit>>index.strand,readStart=start;
        if(seed.direction==1 && strand==0) {
            strand=1;readStart=task.readLength-(length+start);
        } else if(seed.direction==0 && strand==1) {
            readStart=task.readLength-(length+start);genome=index.bytes-(length+genome);
        } else if(seed.direction==1 && strand==1) {
            strand=0;genome=index.bytes-(length+genome);
        }
        hits[task.offset+offset]={genome,static_cast<uint32_t>(strand),static_cast<uint32_t>(readStart)};
    }
}

// Conservative window-reachability filter (exact superset of the CPU result).
// stitchPieces only records a hit when winBin[strand][a1>>winBinNbits] names a
// window, and assignAlignToWindow returns without side effects otherwise. Every
// bin that createExtendWindowsWithAlign or the flank extension can mark lies
// within max(winAnchorDistNbins,winFlankNbins) bins of an anchor bin on the same
// strand: new windows start at an anchor bin, merges only fill bins between an
// anchor and a window bin at most winAnchorDistNbins away, window ends remain
// anchor bins, and flanks extend at most winFlankNbins past them. Dropping hits
// farther than that from every anchor of the read therefore removes only
// no-op iterations; the CPU still performs the exact window test on the rest.
struct WindowFilter {
    U sjGstart=0,sjdbLength=0,sjdbOverhang=0,sjdbN=0;
    const U *sjD=nullptr,*sjA=nullptr;
    unsigned binBits=0;
    U reach=0,anchorMax=0;
};
constexpr unsigned filterKeyCapacity=4096;
constexpr unsigned filterThreads=256;
// Bins examined by stitchPieces for one hit: 1 genomic bin, 2 for a junction-
// crossing sjdb hit (donor and acceptor), 0 when the CPU skips it, -1 unknown.
__device__ int hitBins(const WindowFilter& f,U genome,U length,U* bins) {
    if(genome>=f.sjGstart) {
        if(!f.sjdbLength) return -1;
        const U sj1=(genome-f.sjGstart)%f.sjdbLength;
        if(sj1<f.sjdbOverhang && sj1+length>f.sjdbOverhang) {
            const U isj=(genome-f.sjGstart)/f.sjdbLength;
            if(isj>=f.sjdbN) return -1;
            bins[0]=(f.sjD[isj]+sj1)>>f.binBits;bins[1]=f.sjA[isj]>>f.binBits;
            return 2;
        }
        return 0;
    }
    bins[0]=genome>>f.binBits;
    return 1;
}
__device__ U binKey(U strand,U bin) {return (strand<<62)|bin;}
__device__ bool nearAnchor(const U* keys,unsigned n,U strand,U bin,U reach) {
    const U low=binKey(strand,bin>reach ? bin-reach : 0),high=binKey(strand,bin+reach);
    unsigned a=0,b=n;
    while(a<b) {const unsigned m=(a+b)/2; if(keys[m]<low) a=m+1; else b=m;}
    return a<n && keys[a]<=high;
}
// One block per read. Flags each hit and counts kept hits per task (piece).
__global__ void filterSeedHits(WindowFilter f,const SeedNode* nodes,const HitTask* tasks,
        const unsigned* readTasks,unsigned readCount,const SeedProjectedHit* hits,
        unsigned char* flags,unsigned* taskKept) {
    __shared__ U keys[filterKeyCapacity];
    __shared__ unsigned keyCount;
    __shared__ int keepAll;
    const unsigned read=blockIdx.x;
    if(read>=readCount) return;
    const unsigned t0=readTasks[read],t1=readTasks[read+1];
    if(threadIdx.x==0) {keyCount=0;keepAll=0;}
    __syncthreads();
    for(unsigned t=t0;t<t1;++t) {
        const auto& match=nodes[tasks[t].node].emission.match;
        if(match.multiplicity>f.anchorMax) continue;
        const U count=match.upper-match.lower+1;
        for(U i=threadIdx.x;i<count;i+=blockDim.x) {
            const auto hit=hits[tasks[t].offset+i];
            U bins[2];const int k=hitBins(f,hit.genome,match.length,bins);
            if(k<0) {keepAll=1;continue;}
            for(int j=0;j<k;++j) {
                const unsigned slot=atomicAdd(&keyCount,1u);
                if(slot<filterKeyCapacity) keys[slot]=binKey(hit.strand,bins[j]);
                else keepAll=1;
            }
        }
    }
    __syncthreads();
    const bool all=keepAll!=0;
    const unsigned n=keyCount<filterKeyCapacity ? keyCount : filterKeyCapacity;
    if(!all && n>1) {
        unsigned size=1;while(size<n) size<<=1;
        for(unsigned i=n+threadIdx.x;i<size;i+=blockDim.x) keys[i]=~U(0);
        __syncthreads();
        for(unsigned k=2;k<=size;k<<=1) for(unsigned j=k>>1;j>0;j>>=1) {
            for(unsigned i=threadIdx.x;i<size;i+=blockDim.x) {
                const unsigned l=i^j;
                if(l>i) {
                    const bool ascending=(i&k)==0;const U a=keys[i],b=keys[l];
                    if((a>b)==ascending) {keys[i]=b;keys[l]=a;}
                }
            }
            __syncthreads();
        }
    }
    for(unsigned t=t0;t<t1;++t) {
        const auto& match=nodes[tasks[t].node].emission.match;
        const U count=match.upper-match.lower+1;
        unsigned kept=0;
        for(U base=0;base<count;base+=blockDim.x) {
            const U i=base+threadIdx.x;
            int keep=0;
            if(i<count) {
                if(all) keep=1;
                else {
                    const auto hit=hits[tasks[t].offset+i];
                    U bins[2];const int k=hitBins(f,hit.genome,match.length,bins);
                    if(k<0) keep=1;
                    else for(int j=0;j<k && !keep;++j) keep=nearAnchor(keys,n,hit.strand,bins[j],f.reach);
                }
                flags[tasks[t].offset+i]=static_cast<unsigned char>(keep);
            }
            kept+=__syncthreads_count(keep);
        }
        if(threadIdx.x==0) taskKept[t]=kept;
    }
}
