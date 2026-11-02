#include "ReadAlign.h"
#include <cstring>
#include "PreparedRead.h"
#include "SequenceFuns.h"
#include "SoloRead.h"

void ReadAlign::savePreparedRead(PreparedRead& read) const {
    read.id=iReadAll; read.fileIndex=readFilesIndex; read.filter=readFilter;
    read.fileType=readFileType; read.originalPairLength=readLengthPairOriginal;
    read.mismatchLimit=outFilterMismatchNmaxTotal;
    read.seeds.clear(); read.seedSummary=SeedSummary{}; read.projected=SeedHitBatch{}; read.deviceConsolidated=false; read.barcode=BarcodeHint{}; // slot may be reused
    read.sequence.assign(Read1[0],Read1[0]+Lread);
    read.ends.resize(P.readNends);
    read.text.clear();
    auto append=[&read](const char* p,size_t n) {
        PreparedRead::Field f{static_cast<uint32>(read.text.size()),static_cast<uint32>(n)};
        read.text.append(p,n);
        return f;
    };
    for(uint32 i=0;i<P.readNends;++i) {
        auto& e=read.ends[i];
        e.name=append(readNameMates[i],std::strlen(readNameMates[i]));
        e.sequence=append(Read0[i],readLengthOriginal[i]);
        e.quality=append(Qual0[i],readLengthOriginal[i]);
        e.extra=append(readNameExtra[i].data(),readNameExtra[i].size());
        e.length=readLength[i]; e.originalLength=readLengthOriginal[i];
        for(unsigned side=0;side<2;++side) e.clipped[side]=clipMates[i][side].clippedN;
    }
}

void ReadAlign::restorePreparedRead(const PreparedRead& read) {
    if(preparedSeeds || preparedSeedSummary || preparedHits) throw std::logic_error("unconsumed prepared seeds");
    iReadAll=read.id; readFilesIndex=read.fileIndex; readFilter=read.filter;
    readFileType=read.fileType; readLengthPairOriginal=read.originalPairLength;
    outFilterMismatchNmaxTotal=read.mismatchLimit; Lread=read.sequence.size();
    for(uint32 i=0;i<P.readNends;++i) {
        const auto& e=read.ends.at(i);
        auto copy=[&read](char* to,PreparedRead::Field f) {std::memcpy(to,read.text.data()+f.offset,f.size); to[f.size]=0;};
        copy(readNameMates[i],e.name); copy(Read0[i],e.sequence); copy(Qual0[i],e.quality);
        readNameExtra[i].assign(read.text.data()+e.extra.offset,e.extra.size);
        readLength[i]=e.length; readLengthOriginal[i]=e.originalLength;
        for(unsigned side=0;side<2;++side) clipMates[i][side].clippedN=e.clipped[side];
    }
    if(P.readNmates==1) readLength[1]=0;
    if(soloRead && soloRead->readBar) soloRead->readBar->hint=read.barcode;
    std::copy(read.sequence.begin(),read.sequence.end(),Read1[0]);
    complementSeqNumbers(Read1[0],Read1[1],Lread);
    for(uint64 i=0;i<Lread;++i) Read1[2][Lread-i-1]=Read1[1][i];
    preparedSeeds=&read.seeds;
    preparedSeedSummary=read.deviceConsolidated ? &read.seedSummary : nullptr;
    preparedHits=read.deviceConsolidated ? &read.projected : nullptr;
}
