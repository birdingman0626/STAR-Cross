#include "SoloFeature.h"
#include "serviceFuns.cpp"
#include "SimpleGoodTuring/sgt.h"
#include "ErrorWarning.h"
#include <cmath>
#include <numeric>
#include <unordered_set>
#include <unordered_map>
#include <set>
#include <map>
#include <random>
using namespace std;
double logMultinomialPDFsparse(const vector<double> &ambProfileLogP, const vector<uint32> &countCellGeneUMI, const uint32 stride, const uint32 shift, const int64 start, const uint32 nGenes, const vector<double> &logFactorial);

namespace {

typedef struct {uint32 index, count;} IndCount;
typedef struct{uint32 index; double p; double padj;} IndPPadj;

struct AmbientProfile {
    vector<double> logP;//logarithm, all features
    vector<double> pNon0, logPnon0;//only non-0 genes
};

//find genes that were detected in all cells
unordered_set<uint32> detectedFeatures(const SoloFeature &sf)
{
    unordered_set<uint32> featDet;
    for (uint32 icb=0; icb<sf.nCB; icb++) {
        for (uint32 ig=0; ig<sf.nGenePerCB[icb]; ig++) {
            uint32 irec=sf.countCellGeneUMIindex[icb]+ig*sf.countMatStride;
            if (sf.countCellGeneUMI[irec + sf.pSolo.umiDedup.countInd.main] > 0)
                featDet.insert(sf.countCellGeneUMI[irec]); //gene is present if it's count > 0 for
        };
    };
    return featDet;
};

//indCount - total UMI per cell sorted descending
vector<IndCount> cellsByCountDescending(const SoloFeature &sf)
{
    vector<IndCount> indCount(sf.nCB);
    for (uint32 ii=0; ii<sf.nCB; ii++) {
        indCount[ii].index=ii;
        indCount[ii].count=sf.nUMIperCB[ii];
    };
    std::sort(indCount.begin(), indCount.end(), [](const IndCount &ic1, const IndCount &ic2) {
                                                    return (ic1.count>ic2.count) || (ic1.count==ic2.count && ic1.index<ic2.index); //descending order by count, ascending by index
                                                });
    return indCount;
};

//ambient gene counts: sum gene expression over the collection of empty cells
vector<uint32> ambientCounts(const SoloFeature &sf, const vector<IndCount> &indCount, Parameters &P)
{
    const auto &pSolo=sf.pSolo;
    vector<uint32> ambCount(sf.featuresNumber,0);
    for (auto icb=pSolo.cellFilter.eDcr.indMin; icb<min(sf.nCB,pSolo.cellFilter.eDcr.indMax); icb++) {
        auto icb1 = indCount[icb].index;
        for (uint32 ig=0; ig<sf.nGenePerCB[icb1]; ig++) {
            auto irec = sf.countCellGeneUMIindex[icb1]+ig*sf.countMatStride;
            auto &ambient=ambCount[sf.countCellGeneUMI[irec+0]];
            const auto count=sf.countCellGeneUMI[irec + pSolo.umiDedup.countInd.main];
            if (count>numeric_limits<uint32>::max()-ambient)
                exitWithError("EXITING because of fatal INPUT FILE error: EmptyDrops ambient count exceeds supported range.\n",
                              std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
            ambient += count;
        };
    };
    return ambCount;
};

//frequencies and their SGT estimates; false if the ambient frequencies cannot be estimated
bool ambientFrequenciesSGT(const vector<uint32> &ambCount, uint32 featuresNumber, uint32 featDetN, map<uint32,double> &ambCountFreqSGT, ostream &logMain)
{
    map<uint32,uint32> ambCountFreq; //ordered map is not really needed
    for (auto &ac: ambCount) {
        ambCountFreq[ac]++;
    };
    if (ambCountFreq.size()<=1) {
        logMain << "emptyDrops_CR: insufficient ambient frequency diversity; retaining knee-filtered cells only\n";
        return false;
    };
    ambCountFreq[0] -= (featuresNumber-featDetN); //subtract genes that were not detected in *any* cells

    //SGT estimate of ambient profile
    SGT<uint32> sgt;
    for (auto &cf: ambCountFreq) {
        if (cf.first != 0)
            sgt.add(cf.first, cf.second);
    };
    if (!sgt.analyse()) {
        logMain << "emptyDrops_CR: insufficient ambient frequency categories for SGT; retaining knee-filtered cells only\n";
        return false;
    }

    for (const auto &frequency: ambCountFreq) {
        sgt.estimate(frequency.first, ambCountFreqSGT[frequency.first]);
    };
    if (ambCountFreq[0]>0) ambCountFreqSGT[0] /= ambCountFreq[0];
    else ambCountFreqSGT[0]=0; // no unseen genes to receive probability
    return true;
};

//ambient profile for all features; false if it cannot be normalized
bool ambientProfile(const unordered_set<uint32> &featDet, const vector<uint32> &ambCount, map<uint32,double> &ambCountFreqSGT, uint32 featuresNumber, AmbientProfile &amb, ostream &logMain)
{
    amb.logP.assign(featuresNumber, 0.0);
    for (uint32 ig=0; ig<featuresNumber; ig++) {
        if (featDet.count(ig)>0) {//this is only needed if normalization below is performed
            amb.logP[ig]=ambCountFreqSGT[ambCount[ig]];
        };
    };

    double norm1 = accumulate(amb.logP.begin(), amb.logP.end(), 0.0);
    if (!std::isfinite(norm1) || norm1<=0) {
        logMain << "emptyDrops_CR: unusable ambient probability profile; retaining knee-filtered cells only\n";
        return false;
    }
    amb.logPnon0.reserve(amb.logP.size());
    amb.pNon0.reserve(amb.logP.size());
    for (auto &cf: amb.logP) {
        if (cf>0) {
            cf /= norm1;//normalization is just in case
            amb.pNon0.push_back(cf);
            cf = std::log(cf);
            amb.logPnon0.push_back(cf);
        };
    };
    return true;
};

//select candidate cells: first/last candidate cell in the descending sorted indCount; false if there are none
bool selectCandidates(const SoloFeature &sf, const vector<IndCount> &indCount, uint32 &iCandFirst, uint32 &iCandLast, uint32 &minUMI, ostream &logMain)
{
    const auto &eDcr=sf.pSolo.cellFilter.eDcr;
    iCandFirst=sf.filteredCells.nCellsSimple;//candidates start right after the cutoff for the simple filtering
    const double fractionMinimum=eDcr.umiMinFracMedian * sf.nUMIperCBsorted[sf.filteredCells.nCellsSimple/2];
    if (!std::isfinite(fractionMinimum) || fractionMinimum>numeric_limits<uint32>::max()) {
        logMain << "emptyDrops_CR: fractional threshold exceeds all supported counts; no candidate cells\n";
        return false;
    }
    minUMI=static_cast<uint32>(fractionMinimum);//this is not exactly median
    minUMI = max(eDcr.umiMin, minUMI);
    const uint64 candidateEnd=min<uint64>(sf.nCB, uint64(iCandFirst)+eDcr.candMaxN);
    for (iCandLast=iCandFirst; iCandLast<candidateEnd; iCandLast++) {
        if (indCount[iCandLast].count<minUMI)
            break;
    };
    if (iCandLast==iCandFirst) {
        logMain << "emptyDrops_CR: no candidate cells\n";
        return false;
    }
    --iCandLast;
    return true;
};

//calculate observed probability for each candidate
vector<double> observedLogProb(const SoloFeature &sf, const vector<IndCount> &indCount, uint32 iCandFirst, uint32 iCandLast, const vector<double> &ambProfileLogP)
{
    vector<double> obsLogProb(iCandLast-iCandFirst+1);
    vector<double> logFactorial; //tabulate log-factorial
    logFactorial.resize(uint64(indCount[iCandFirst].count)+1);
    logFactorial[1]=0;
    for (uint64 cc=2; cc<logFactorial.size(); cc++)
        logFactorial[cc]=logFactorial[cc-1]+std::log(cc);

    for (uint32 icand=0; icand<obsLogProb.size(); icand++) {
        auto icell=indCount[icand+iCandFirst].index;
        obsLogProb[icand]=logMultinomialPDFsparse(ambProfileLogP, sf.countCellGeneUMI, sf.countMatStride, sf.pSolo.umiDedup.countInd.main, sf.countCellGeneUMIindex[icell], sf.nGenePerCB[icell], logFactorial);
    };
    return obsLogProb;
};

// Simulate and collect p-values with reduced memory
// Instead of storing simLogProb[nSim][maxCount+1] (can be GBs),
// only checkpoint at UMI counts that candidates actually need.
// Returns sorted simulated log-probabilities for each candidate UMI count.
unordered_map<uint32, vector<double>> simulateAmbient(const vector<IndCount> &indCount, uint32 iCandFirst, uint64 nCand, uint64 nSim, const AmbientProfile &amb)
{
    auto maxCount=indCount[iCandFirst].count;

    // Collect unique candidate UMI counts (sorted ascending)
    set<uint32> neededCountsSet;
    for (uint32 icand=0; icand<nCand; icand++)
        neededCountsSet.insert(indCount[icand+iCandFirst].count);
    vector<uint32> neededCounts(neededCountsSet.begin(), neededCountsSet.end());

    // Build simByCount directly during simulation — O(nSim × nUniqueCounts) memory
    unordered_map<uint32, vector<double>> simByCount;
    for (auto c : neededCounts)
        simByCount[c].reserve(nSim);

    std::discrete_distribution<uint32> distrAmb ( amb.pNon0.begin(), amb.pNon0.end() );

    for (uint64 isim=0; isim<nSim; isim++) {
        // Same RNG seeding and iteration order as original
        std::mt19937 rngGen(19760110LLU*(isim+1));

        vector<uint32> currCounts(amb.pNon0.size(), 0);
        double logProb = 0; // running accumulation, same as simLogProb[isim][ic-1] + ...
        uint32 nextIdx = 0; // index into neededCounts

        for (uint64 ic=1; ic<=maxCount; ic++) {
            uint32 ig1 = distrAmb(rngGen);
            currCounts[ig1]++;
            // Identical accumulation: logProb at ic == simLogProb[isim][ic] in the original
            logProb += amb.logPnon0[ig1] + std::log(ic) - std::log(currCounts[ig1]);

            // Checkpoint only at needed counts
            while (nextIdx < neededCounts.size() && neededCounts[nextIdx] == ic) {
                simByCount[ic].push_back(logProb);
                nextIdx++;
            }
        };
    };

    // Sort each count's simulation values for binary search
    for (auto &kv : simByCount)
        sort(kv.second.begin(), kv.second.end());
    return simByCount;
};

//p-values with Benjamini-Hochberg adjustment, sorted by p
vector<IndPPadj> adjustedPValues(const vector<IndCount> &indCount, uint32 iCandFirst, const vector<double> &obsLogProb, unordered_map<uint32, vector<double>> &simByCount, uint64 nSim)
{
    vector<IndPPadj> pValues(obsLogProb.size());
    for (uint32 icand=0; icand<obsLogProb.size(); icand++) {
        pValues[icand].index=indCount[icand+iCandFirst].index;
        auto count1=indCount[icand+iCandFirst].count;

        // Binary search: count simulations with lower log-probability
        auto &sorted = simByCount[count1];
        uint32 nLowerP = (uint32)(lower_bound(sorted.begin(), sorted.end(), obsLogProb[icand]) - sorted.begin());

        pValues[icand].p=double(1+nLowerP)/(1+nSim);
    };
    //BH
    std::sort(pValues.begin(), pValues.end(), [](const IndPPadj &ip1, const IndPPadj &ip2) {return (ip1.p < ip2.p);} );
    uint32 rank=0;
    for (auto &ip: pValues) {
        rank++;
        ip.padj=ip.p*pValues.size()/rank;
    };
    for (auto ip=pValues.rbegin()+1; ip!=pValues.rend(); ++ip)
        ip->padj = min(ip->padj, (ip-1)->padj); //make it non-decreasing
    return pValues;
};

} // namespace

void SoloFeature::emptyDrops_CR()
{
    if (nCB<=pSolo.cellFilter.eDcr.indMin) {
        P.inOut->logMain << "emptyDrops_CR filtering: total number of cells: nCB=" << nCB <<" is smaller than emptyCellMinIndex="<< pSolo.cellFilter.eDcr.indMin
                         << ", which is the starting index for the *true empty* cells. The additional non-empty cells will not be detected.\n";
        return;
    };

    time_t rawTime;
    time(&rawTime);
    P.inOut->logMain << timeMonthDayTime(rawTime) <<" ... starting emptyDrops_CR filtering" <<endl;

    const unordered_set<uint32> featDet=detectedFeatures(*this);
    uint32 featDetN=featDet.size(); //total number of detected genes - this should have been done already?

    const vector<IndCount> indCount=cellsByCountDescending(*this);

    const vector<uint32> ambCount=ambientCounts(*this, indCount, P);
    time(&rawTime);
    P.inOut->logMain << timeMonthDayTime(rawTime) <<" ... finished ambient cells counting" <<endl;

    map<uint32,double> ambCountFreqSGT; // only frequencies actually used by the profile
    if (!ambientFrequenciesSGT(ambCount, featuresNumber, featDetN, ambCountFreqSGT, P.inOut->logMain))
        return;
    time(&rawTime);
    P.inOut->logMain << timeMonthDayTime(rawTime) <<" ... finished SGT"<<endl;

    AmbientProfile amb;
    if (!ambientProfile(featDet, ambCount, ambCountFreqSGT, featuresNumber, amb, P.inOut->logMain))
        return;
    time(&rawTime);
    P.inOut->logMain << timeMonthDayTime(rawTime) <<" ... finished ambient profile"<<endl;

    uint32 iCandFirst, iCandLast, minUMI;
    if (!selectCandidates(*this, indCount, iCandFirst, iCandLast, minUMI, P.inOut->logMain))
        return;
    time(&rawTime);
    P.inOut->logMain << timeMonthDayTime(rawTime) << " ... candidate cells: minUMI="<< minUMI << "; number of candidate cells=" << iCandLast-iCandFirst+1 <<endl;

    const vector<double> obsLogProb=observedLogProb(*this, indCount, iCandFirst, iCandLast, amb.logP);
    time(&rawTime);
    P.inOut->logMain << timeMonthDayTime(rawTime) << " ... finished observed logProb" <<endl;

    uint64 nSim = pSolo.cellFilter.eDcr.simN;
    unordered_map<uint32, vector<double>> simByCount=simulateAmbient(indCount, iCandFirst, obsLogProb.size(), nSim, amb);
    time(&rawTime);
    P.inOut->logMain << timeMonthDayTime(rawTime) << " ... finished simulations" <<endl;

    const vector<IndPPadj> pValues=adjustedPValues(indCount, iCandFirst, obsLogProb, simByCount, nSim);

    uint32 extraCells=0;
    for (auto &ip: pValues) {
        if (ip.padj<=pSolo.cellFilter.eDcr.FDR) {
            ++extraCells;
            filteredCells.filtVecBool[ip.index]=true;
        };
    };
    time(&rawTime);
    P.inOut->logMain << timeMonthDayTime(rawTime) <<  " ... finished emptyDrops_CR filtering: number of additional non-ambient cells=" << extraCells <<endl;

    return;
};

double logMultinomialPDFsparse(const vector<double> &ambProfileLogP, const vector<uint32> &countCellGeneUMI, const uint32 stride, const uint32 shift, const int64 start, const uint32 nGenes, const vector<double> &logFactorial)
{
    uint32 sumCount=0;
    double sumLogFac=0.0, sumCountLogP=0.0;
    for (uint32 ig=0; ig<nGenes; ig++) {
        auto count1 = countCellGeneUMI[start+(uint64_t)ig*stride+shift];
        sumCount += count1;
        sumLogFac += logFactorial[count1];
        sumCountLogP += ambProfileLogP[countCellGeneUMI[start+(uint64_t)ig*stride]] * count1;
    };

    return logFactorial[sumCount] - sumLogFac + sumCountLogP;
};
