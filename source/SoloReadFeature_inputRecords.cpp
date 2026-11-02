#include <cmath>
#include "SoloReadFeature.h"
#include "SoloCommon.h"
#include "SoloFeature.h"
#include "soloInputFeatureUMI.h"
#include "SoloTextIO.h"
#include "serviceFuns.cpp"
using namespace std;
//one record of the temporary per-read feature file, and how it was resolved to a CB
struct SoloInputRecord {
    uint32 feature;
    uint64 umi, iread;
    int32 cbmatch;
    int64 cb;
    bool readIsCounted, featGood, noMMtoWLwithoutExact, noTooManyWLmatches;
};

void SoloReadFeature::inputRecords(uint32 **cbP, uint32 cbPstride, vector<uint32> &cbReadCountTotal, vector<readInfoStruct> &readInfo, SoloReadFlagClass &readFlagCounts,
                                   vector<uint32> &nReadPerCBunique1, vector<uint32> &nReadPerCBmulti1)
{   
    streamReads->flush();
    streamReads->seekg(0,std::ios::beg);

    //////////////////////////////////////////// standard features
    SoloInputRecord r;
    r.cb=0;
    uint64 prevIread=(uint64)-1;
    vector<uint32> trIdDist;
    
    while (soloInputFeatureUMI(streamReads, featureType, readIndexYes, P.sjAll, r.iread, r.cbmatch, r.feature, r.umi, trIdDist, readFlagCounts)) {
        if (r.feature == (uint32)(-1) && !readIndexYes) {//no feature => no record, this can happen for SJs
            streamReads->ignore((uint32)-1, '\n');
            //stats.V[stats.noNoFeature]++; //need separate category for this
            continue;
        };

        r.readIsCounted = false;
        r.featGood = ( r.feature != (uint32)(-1) );
        r.noMMtoWLwithoutExact = false;
        r.noTooManyWLmatches = false;

        if (r.cbmatch<=1) {//single match
            if (!inputSingleCB(r, cbP, cbPstride, cbReadCountTotal, readInfo))
                continue; //this cb was not in the tentative WL
        } else {//multiple matches
            inputMultipleCB(r, cbP, cbPstride, cbReadCountTotal, readInfo);
        };

        if ( !readIndexYes || r.iread != prevIread ) {//no readindex (then only unique-gene reads are recorded) OR only for one align of each read, in case of multimappers
            prevIread = r.iread;
            countInputRead(r, readFlagCounts, nReadPerCBunique1, nReadPerCBmulti1);
        };
    };
};

//single CB match; false if the CB is not in the tentative whitelist (no-WL), i.e. the record is skipped
bool SoloReadFeature::inputSingleCB(SoloInputRecord &r, uint32 **cbP, uint32 cbPstride, vector<uint32> &cbReadCountTotal, vector<readInfoStruct> &readInfo)
{
    readNumber(*streamReads, r.cb);

    if ( pSolo.CBmatchWL.oneExact && r.cbmatch==1 && cbReadCountTotal[r.cb]==0 ) {//single 1MM match, no exact matches to this CB
        r.noMMtoWLwithoutExact = true;

    } else {

        if (!pSolo.cbWLyes) {//if no-WL, the full cbInteger was recorded - now has to be placed in order
            r.cb=binarySearchExact<uintCB>(r.cb, pSolo.cbWL.data(), pSolo.cbWLsize);
            if (r.cb+1 == 0)
                return false; //this cb was not in the tentative WL
        };

        //record feature
        storeInputRecord(r, cbP, cbPstride, readInfo);
    };
    return true;
};

//multiple CB matches: the CB with the highest posterior, if it is above cbMinP
void SoloReadFeature::inputMultipleCB(SoloInputRecord &r, uint32 **cbP, uint32 cbPstride, vector<uint32> &cbReadCountTotal, vector<readInfoStruct> &readInfo)
{
    #ifdef MATCH_CellRanger
    double ptot=0.0, pmax=0.0, pin;
    #else
    float ptot=0.0, pmax=0.0, pin;
    #endif

    for (uint32 ii=0; ii<(uint32)r.cbmatch; ii++) {
        uint32 cbin=0;
        char  qin=0;
        readNumber(*streamReads, cbin); readChar(*streamReads, qin);
        if (cbReadCountTotal[cbin]>0) {//otherwise this cbin does not work
            qin -= pSolo.QSbase;
            qin = qin < pSolo.QSmax ? qin : pSolo.QSmax;
            pin=cbReadCountTotal[cbin]*std::pow(10.0,-qin/10.0);
            ptot+=pin;
            if (pin>pmax) {
                r.cb=cbin;
                pmax=pin;
            };
        };
    };
    if (ptot>0.0 && pmax>=pSolo.cbMinP*ptot) {
        //record feature single-number feature
        storeInputRecord(r, cbP, cbPstride, readInfo);
    } else {
        r.noTooManyWLmatches = true;
    };
};

//record the feature in the CB array, or the CB/UMI of a read without feature in readInfo
void SoloReadFeature::storeInputRecord(SoloInputRecord &r, uint32 **cbP, uint32 cbPstride, vector<readInfoStruct> &readInfo)
{
    if (r.featGood) {//good feature, will be counted
        r.readIsCounted = true;

        cbP[r.cb][0]=r.feature;
        cbP[r.cb][1]=r.umi;

        if (readIndexYes) {
            cbP[r.cb][2]=r.iread;
        };

        cbP[r.cb]+=cbPstride;

    } else if (readInfoYes) {//no feature - record readInfo
        readInfo[r.iread].cb=r.cb;
        readInfo[r.iread].umi=r.umi;
    };
};

//per-read statistics, once per read (first align of multimappers)
void SoloReadFeature::countInputRead(const SoloInputRecord &r, SoloReadFlagClass &readFlagCounts, vector<uint32> &nReadPerCBunique1, vector<uint32> &nReadPerCBmulti1)
{
    if (r.featGood) {
        if (r.cbmatch==0) {
            stats.V[stats.yessubWLmatchExact]++;
        } else if (r.noMMtoWLwithoutExact) {
            stats.V[stats.noMMtoWLwithoutExact]++;
        } else if (r.noTooManyWLmatches) {
            stats.V[stats.noTooManyWLmatches]++;
        };
    };

    if (r.readIsCounted) {
        if (r.feature<geneMultMark) {
            nReadPerCBunique1[r.cb]++;
        } else {
            nReadPerCBmulti1[r.cb]++;
        };
    };
    
    if ( pSolo.readStatsYes[featureType] ) {//has to be new iread to avoid muti-counting multi-gene reads
        
        //readIsCounted flag was defined above
        if ( r.readIsCounted ) {
            if ( readFlagCounts.checkBit(readFlagCounts.featureU) )
                readFlagCounts.setBit(readFlagCounts.countedU);
            if ( readFlagCounts.checkBit(readFlagCounts.featureM) )
                readFlagCounts.setBit(readFlagCounts.countedM);    
        };

        readFlagCounts.setBit(readFlag.cbMatch); //set this flag for both CB and no-CB, but no-CB will be counted separately below
        //only record this read in readFlagCounts if its CB was defined
        if (r.cbmatch==0) {//perfect CB match to WL
            readFlagCounts.setBit(readFlagCounts.cbPerfect);
            readFlagCounts.countsAdd(r.cb);
        } else if (r.cbmatch==1 && !r.noMMtoWLwithoutExact) {
            readFlagCounts.setBit(readFlagCounts.cbMMunique);
            readFlagCounts.countsAdd(r.cb);
        } else if (r.cbmatch>1 && !r.noTooManyWLmatches) {
            readFlagCounts.setBit(readFlagCounts.cbMMmultiple);
            readFlagCounts.countsAdd(r.cb);
        } else {//no CB match
            readFlagCounts.countsAddNoCB();
        };

        // debug
        /*nReadsIn++;
        uint64 ntot=0;
        for (auto &ii: readFlagCounts.flagCounts)
            ntot += ii.second[0];

        if (nReadsIn!=ntot)
            cout << nReadsIn <<" "<< ntot << endl;
        */
        /*//if (featureType==SoloFeatureTypes::Gene && readFlagCounts.flagCounts[cb][readFlagCounts.featureM]+readFlagCounts.flagCounts[cb][readFlagCounts.featureU] != readFlagCounts.flagCounts[cb][readFlagCounts.exonic])
        //    cout << cb <<' '<< iread << endl;
        //if (readFlagCounts.checkBit(readFlagCounts.featureM))
        //    cout << iread <<' '<< readFlagCounts.flagCounts[cb][readFlagCounts.featureM] << endl;
        */
    };
};
