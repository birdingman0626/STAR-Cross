#include "ParametersClip.h"
#include "Parameters.h"
#include "SequenceFuns.h"
using namespace std;
inline char* findChar(char *arr, char c);

void ClipMate::clipChunk(char *chArr, uint64 chSize)
{//clipping adapters from a chunk of reads
    if (type != 10) //type=10 for CellRanger4 5' clipping.
        return;
    
    char *chA1 = chArr;
    bool chGood = true; //=true after teh end of the chunk
    while (chGood) {//cycle over all
        
        int dbN1 = cr4->dbN; //maybe changed to a smaller value in the loop
        int idb = 0;
        for ( ; idb<cr4->dbN; idb++) {
            chA1 = findChar(chA1, '\n')+1; //skip read name
            
            char *chA2 = findChar(chA1, '\n');
            uint32 rL = (uint32) (chA2-chA1);
            cr4->fillOneSeq(idb, chA1, rL);
            
            cr4->storeClip[idb] = (uint8*) (chA2+1);//store the position of "+" character - we will record the clipped length there
            
            //before the next one    
            chA1 = chA2 + 3 + rL + 1; //start of the next read: skip \n+\n, quality=read lengt, \n
            if (chA1 > chArr+chSize) {
                chGood = false;
                dbN1 = idb+1;
                break;
            };
        };

        cr4->align((uint8_t*) adSeqNum.data(), adSeqNum.size(), dbN1);

        for (int idb=0; idb<dbN1; idb++) {//store results
            int L = cr4->alignRes[idb].endLocationTarget+1;
            int S = cr4->alignRes[idb].score;
            
            bool L0 = S<20 || (S==20 && L>26) || (S==21 && L>30);
            
            *cr4->storeClip[idb] = (uint8) (L0 ? 0 : L);
        };
    };
    
};

void ClipMate::clipChunk(char *chArr, uint64 chSize, const std::function<bool(const ClipBatch&)> &alignAll)
{
    if (type != 10)
        return;
    const uint32 L = cr4->targetLength();
    clipCodes.clear();
    clipStore.clear();
    char *chA1 = chArr;
    while (true) {//same read walk as clipChunk above
        chA1 = findChar(chA1, '\n')+1; //skip read name
        char *chA2 = findChar(chA1, '\n');
        uint32 rL = (uint32) (chA2-chA1);
        size_t at = clipCodes.size();
        clipCodes.resize(at+L);
        cr4->fillCodes(chA1, rL, clipCodes.data()+at);
        clipStore.push_back((uint8*) (chA2+1));
        chA1 = chA2 + 3 + rL + 1;
        if (chA1 > chArr+chSize)
            break;
    };
    clipResults.resize(2*clipStore.size());
    ClipBatch batch;
    cr4->describe(batch);
    batch.codes = clipCodes.data();
    batch.n = clipStore.size();
    batch.query = (const uint8*) adSeqNum.data();
    batch.queryLen = (uint32) adSeqNum.size();
    batch.out = clipResults.data();
    if (!alignAll(batch)) {
        clipChunk(chArr, chSize);
        return;
    };
    for (size_t i=0; i<clipStore.size(); i++) {//store results
        int L1 = clipResults[2*i+1]+1;
        int S = clipResults[2*i];
        bool L0 = S<20 || (S==20 && L1>26) || (S==21 && L1>30);
        *clipStore[i] = (uint8) (L0 ? 0 : L1);
    };
};

inline char* findChar(char *arr, char c)
{//find character in a character array. No check for out of boundary.
    char* cArr=arr;
    while (*cArr != c)
        ++cArr;
    
    return cArr;
};
