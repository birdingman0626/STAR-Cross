#include "SoloFeature.h"
#include "streamFuns.h"
#include "TimeFunctions.h"
#include "serviceFuns.cpp"
#include <unordered_map>
#include "SoloCommon.h"
#include "DirectionalUMI.h"
#include "SoloEM.h"
using namespace std;
inline int funCompareSolo1 (const void *a, const void *b);          //defined below
inline int funCompare_uint32_1_2_0 (const void *a, const void *b);

void SoloFeature::collapseUMIall() 
{
    vector<uint32> umiArray(nReadPerCBmax*umiArrayStride);//temp array for collapsing UMI
    vector<uint32> gID(min(2*featuresNumber,nReadPerCBmax)+1); //gene IDs, 2* is needed because each gene can have unique and multi-mappers
    vector<uint32> gReadS(min(2*featuresNumber,nReadPerCBmax)+1); //start of gene reads TODO: allocate this array in the 2nd half of rGU

    for (uint32 icb=0; icb<nCB; icb++) {//main collapse cycle
        
        collapseUMIperCB(icb, umiArray, gID, gReadS);
        
        readFeatSum->stats.V[readFeatSum->stats.yesUMIs] += nUMIperCB[icb];
        if (nGenePerCB[icb]>0) //nGenePerCB contains only unique
            ++readFeatSum->stats.V[readFeatSum->stats.yesCellBarcodes];
        
        readFeatSum->stats.V[readFeatSum->stats.yesWLmatch] += nReadPerCBtotal[icb];        
        readFeatSum->stats.V[readFeatSum->stats.yessubWLmatch_UniqueFeature ] += nReadPerCBunique[icb];        
    };
};

void SoloFeature::collapseUMIperCB(uint32 iCB, vector<uint32> &umiArray, vector<uint32> &gID,  vector<uint32> &gReadS)
{
    uint32 *rGU=rCBp[iCB];
    uint32 rN=nReadPerCB[iCB]; //with multimappers, this is the number of all aligns, not reads

    qsort(rGU,rN,rguStride*sizeof(uint32),funCompareNumbers<uint32>); //sort by gene index

    uint32 nGenes = 0, nGenesMult = 0; //number of unique and multi genes
    groupReadsByGene(rGU, rN, gID, gReadS, nGenes, nGenesMult);

    //UMI -> gene -> read count. Note: lookups below with [] insert entries; the multigene step checks which UMIs are present.
    UmiGeneCounts umiGeneMapCount, umiGeneMapCount0;
    filterMultiGeneUMIs(rGU, gReadS[nGenes], umiGeneMapCount);

    vector<unordered_map <uintUMI,uintUMI>> umiCorrected(nGenes);

    if (countCellGeneUMI.size() < countCellGeneUMIindex[iCB] + (uint64_t)nGenes*countMatStride)
        countCellGeneUMI.resize((countCellGeneUMI.size() + (uint64_t)nGenes*countMatStride)*2);//allocated vector too small

    nGenePerCB[iCB]=0;
    nUMIperCB[iCB]=0;
    countCellGeneUMIindex[iCB+1]=countCellGeneUMIindex[iCB];

    for (uint32 iG=0; iG<nGenes; iG++) //collapse UMIs for each gene with unique-gene-mappers
        collapseGeneUMIs(iCB, iG, rGU, gID, gReadS, umiArray, umiGeneMapCount, umiGeneMapCount0, umiCorrected[iG]);

    if (pSolo.umiFiltering.MultiGeneUMI_CR)
        countMultiGeneUMI_CR(iCB, nGenes, rGU, gID, gReadS, umiGeneMapCount, umiGeneMapCount0, umiCorrected);

    if (pSolo.multiMap.yes.multi)
        countMatMult.i[iCB+1] = countMatMult.i[iCB];

    if (nGenesMult>0) //process multigene reads
        collapseMultiGeneReads(iCB, rGU, gReadS, nGenes, nGenesMult, umiGeneMapCount);
};

void SoloFeature::groupReadsByGene(uint32 *rGU, uint32 rN, vector<uint32> &gID, vector<uint32> &gReadS, uint32 &nGenes, uint32 &nGenesMult)
{// gene boundaries in the gene-sorted reads: gReadS[i] is the start of gene gID[i]; unique genes come first
    uint32 gid1 = -1;//current gID
    nGenes = 0;
    nGenesMult = 0;
    for (uint32 iR=0; iR<rN*rguStride; iR+=rguStride) {
        if (rGU[iR+rguG]!=gid1) {//record gene boundary
            gReadS[nGenes]=iR;
            gid1=rGU[iR+rguG];
            gID[nGenes]=gid1;

            ++nGenes;
            if (pSolo.multiMap.yes.multi && (gid1 & geneMultMark) )
                ++nGenesMult;
        };
    };
    gReadS[nGenes]=rguStride*rN;//so that gReadS[nGenes]-gReadS[nGenes-1] is the number of reads for nGenes
    nGenes -= nGenesMult;//unique only gene
}

void SoloFeature::filterMultiGeneUMIs(const uint32 *rGU, uint32 uniqueEnd, UmiGeneCounts &umiGeneMapCount)
{// count reads per UMI and gene; zero the genes that lose a multi-gene UMI
    if (pSolo.umiFiltering.MultiGeneUMI) {
        for (uint32 iR=0; iR<uniqueEnd; iR+=rguStride) {
            umiGeneMapCount[rGU[iR+1]][rGU[iR]]++;
        };

        for (auto &iu : umiGeneMapCount) {//loop over all UMIs
            if (iu.second.size()==1)
                continue;
            uint32 maxu=0;
            for (const auto &ig : iu.second) {//loop over genes for a given UMI
                if (maxu<ig.second)
                    maxu=ig.second; //find gene with maximum count
            };
            if (maxu==1)
                maxu=2;//to kill UMIs with 1 read to one gene, 1 read to another gene
            for (auto &ig : iu.second) {
                if (maxu>ig.second)
                    ig.second=0; //kills Gene with read count *strictly* < maximum count
            };
        };
    };

    if (pSolo.umiFiltering.MultiGeneUMI_All) {
        for (uint32 iR=0; iR<uniqueEnd; iR+=rguStride) {
            umiGeneMapCount[rGU[iR+1]][rGU[iR]]++;
        };

        for (auto &iu : umiGeneMapCount) {//loop over all UMIs
            if (iu.second.size()>1) {
                for (auto &ig : iu.second)
                    ig.second=0; //kill all genes for this UMI
            };
        };
    };
}

void SoloFeature::collapseGeneUMIs(uint32 iCB, uint32 iG, uint32 *rGU, const vector<uint32> &gID, const vector<uint32> &gReadS, vector<uint32> &umiArray,
                                   UmiGeneCounts &umiGeneMapCount, UmiGeneCounts &umiGeneMapCount0, unordered_map<uintUMI,uintUMI> &umiCorrected)
{// collapse and count the UMIs of one unique gene with every requested deduplication; record per-read CB/UMI
    uint32 *rGU1=rGU+gReadS[iG];

    uint32 nR0 = (gReadS[iG+1]-gReadS[iG])/rguStride; //total number of reads
    if (nR0==0)
        return; //no reads - this should not happen?

    qsort(rGU1, nR0, rguStride*sizeof(uint32), funCompareTypeShift<uint32,rguU>);

    //exact collapse
    uint32 iR1=-umiArrayStride; //number of distinct UMIs for this gene
    uint32 u1=-1;
    for (uint32 iR=rguU; iR<gReadS[iG+1]-gReadS[iG]; iR+=rguStride) {//count and collapse identical UMIs
        if (pSolo.umiFiltering.MultiGeneUMI && umiGeneMapCount[rGU1[iR]][gID[iG]]==0) {//multigene UMI is not recorded
            if ( pSolo.umiDedup.typeMain != UMIdedup::typeI::NoDedup ) //for NoDedup, the UMI filtering is not done
                rGU1[iR] = (uintUMI) -1; //mark multigene UMI, so that UB tag will be set to -
            continue;
        };

        if (rGU1[iR]!=u1) {
            iR1 += umiArrayStride;
            u1=rGU1[iR];
            umiArray[iR1]=u1;
            umiArray[iR1+1]=0;
        };
        umiArray[iR1+1]++;
    };

    uint32 nU0 = (iR1+umiArrayStride)/umiArrayStride;//number of UMIs after simple exact collapse

    if (pSolo.umiFiltering.MultiGeneUMI_CR) {
        if (nU0==0)
            return; //nothing to count

        for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
            umiGeneMapCount0[umiArray[iu+0]][iG]+=umiArray[iu+1];//this sums read counts over UMIs that were collapsed
        };

        umiArrayCorrect_CR(nU0, umiArray.data(), readInfo.size()>0, false, umiCorrected);

        for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {//just fill the umiGeneMapCount - will calculate UMI counts later
            umiGeneMapCount[umiArray[iu+2]][iG]+=umiArray[iu+1];//this sums read counts over UMIs that were collapsed
        };

        return; //done with MultiGeneUMI_CR, readInfo will be filled later
    };

    const uint64 rec=countCellGeneUMIindex[iCB+1]; //this gene's record
    if (pSolo.umiDedup.yes.NoDedup)
        countCellGeneUMI[rec + pSolo.umiDedup.countInd.NoDedup] = nR0;

    if (nU0>0) {//otherwise no need to count
        const bool recordInfo = readInfo.size()>0;
        if (pSolo.umiDedup.yes.Exact)
            countCellGeneUMI[rec + pSolo.umiDedup.countInd.Exact] = nU0;

        if (pSolo.umiDedup.yes.CR)
            countCellGeneUMI[rec + pSolo.umiDedup.countInd.CR] =
                umiArrayCorrect_CR(nU0, umiArray.data(), recordInfo && pSolo.umiDedup.typeMain==UMIdedup::typeI::CR, true, umiCorrected);

        if (pSolo.umiDedup.yes.Directional)
            countCellGeneUMI[rec + pSolo.umiDedup.countInd.Directional] =
                umiArrayCorrect_Directional(nU0, umiArray.data(), recordInfo && pSolo.umiDedup.typeMain==UMIdedup::typeI::Directional, true, umiCorrected, 0);

        if (pSolo.umiDedup.yes.Directional_UMItools)
            countCellGeneUMI[rec + pSolo.umiDedup.countInd.Directional_UMItools] =
                umiArrayCorrect_Directional(nU0, umiArray.data(), recordInfo && pSolo.umiDedup.typeMain==UMIdedup::typeI::Directional_UMItools, true, umiCorrected, -1);

        //this changes umiArray, so it should be last call
        if (pSolo.umiDedup.yes.All)
            countCellGeneUMI[rec + pSolo.umiDedup.countInd.All] =
                umiArrayCorrect_Graph(nU0, umiArray.data(), recordInfo && pSolo.umiDedup.typeMain==UMIdedup::typeI::All, true, umiCorrected);
    };

    {//check any count>0 and finalize record for this gene
        uint32 totcount=0;
        for (uint32 ii=countCellGeneUMIindex[iCB+1]+1; ii<countCellGeneUMIindex[iCB+1]+countMatStride; ii++) {
            totcount += countCellGeneUMI[ii];
        };
        if (totcount>0) {//at least one umiDedup type is non-0
            countCellGeneUMI[countCellGeneUMIindex[iCB+1] + 0] = gID[iG];
            nGenePerCB[iCB]++;
            nUMIperCB[iCB] += countCellGeneUMI[countCellGeneUMIindex[iCB+1] + pSolo.umiDedup.countInd.main];
            countCellGeneUMIindex[iCB+1] = countCellGeneUMIindex[iCB+1] + countMatStride;//iCB+1 accumulates the index
        };
    };

    if (readInfo.size()>0) {//record cb/umi for each read
        for (uint32 iR=0; iR<gReadS[iG+1]-gReadS[iG]; iR+=rguStride) {//cycle over reads
            uint64 iread1 = rGU1[iR+rguR];
            readInfo[iread1].cb = indCB[iCB] ;
            uint32 umi=rGU1[iR+rguU];

            if (umiCorrected.count(umi)>0)
                umi=umiCorrected[umi]; //correct UMI
            readInfo[iread1].umi=umi;
        };
    };
}

void SoloFeature::countMultiGeneUMI_CR(uint32 iCB, uint32 nGenes, uint32 *rGU, const vector<uint32> &gID, const vector<uint32> &gReadS,
                                       UmiGeneCounts &umiGeneMapCount, UmiGeneCounts &umiGeneMapCount0, vector<unordered_map<uintUMI,uintUMI>> &umiCorrected)
{// CellRanger-like multi-gene UMI filtering: each corrected UMI is counted for its single top gene, if any
    vector<uint32> geneCounts(nGenes,0);

    vector<unordered_set<uintUMI>> geneUmiHash;
    if (readInfo.size()>0)
        geneUmiHash.resize(nGenes);

    for (const auto &iu: umiGeneMapCount) {//loop over UMIs for all genes
        uint32 maxu=0, maxg=-1;
        for (const auto &ig : iu.second) {
            if (ig.second>maxu) {
                maxu=ig.second;
                maxg=ig.first;
            } else if (ig.second==maxu) {
                maxg=-1;
            };
        };

        if ( maxg+1==0 )
            continue; //this umi is not counted for any gene, because two genes have the same read count for this UMI

        for (const auto &ig : umiGeneMapCount0[iu.first]) {//check that this umi/gene had also top count for uncorrected umis
            if (ig.second>umiGeneMapCount0[iu.first][maxg]) {
                maxg=-1;
                break;
            };
        };

        if ( maxg+1!=0 ) {//this UMI is counted
            geneCounts[maxg]++;
            if (readInfo.size()>0)
                geneUmiHash[maxg].insert(iu.first);
        };
    };

    for (uint32 ig=0; ig<nGenes; ig++) {
        if (geneCounts[ig] == 0)
            continue; //no counts for this gene
        nGenePerCB[iCB]++;
        nUMIperCB[iCB] += geneCounts[ig];
        countCellGeneUMI[countCellGeneUMIindex[iCB+1] + 0] = gID[ig];
        countCellGeneUMI[countCellGeneUMIindex[iCB+1] + pSolo.umiDedup.countInd.CR] = geneCounts[ig];
        countCellGeneUMIindex[iCB+1] = countCellGeneUMIindex[iCB+1] + countMatStride;//iCB+1 accumulates the index
    };

    if (readInfo.size()>0) {//record cb/umi for each read
        for (uint32 iG=0; iG<nGenes; iG++) {//cycle over genes
            uint32 *rGU1=rGU+gReadS[iG];
            for (uint32 iR=0; iR<gReadS[iG+1]-gReadS[iG]; iR+=rguStride) {//cycle over reads
                uint64 iread1 = rGU1[iR+rguR];
                readInfo[iread1].cb = indCB[iCB];
                uint32 umi=rGU1[iR+rguU];

                if (umiCorrected[iG].count(umi)>0)
                    umi=umiCorrected[iG][umi]; //correct UMI

                if (geneUmiHash[iG].count(umi)>0) {
                    readInfo[iread1].umi=umi;
                } else {
                    readInfo[iread1].umi=(uintUMI) -1;
                };
            };
        };
    };
}

vector<vector<uint32>> SoloFeature::multiGeneUMIgenes(uint32 *rGUm, uint32 nRm, const UmiGeneCounts &umiGeneMapCount)
{// for each UMI of multigene reads (not seen among unique reads): the genes that every read of the UMI maps to
    std::vector<vector<uint32>> umiGenes;
    umiGenes.reserve(256);

    //sort by UMI, then by read, then by gene
    qsort(rGUm, nRm, rguStride*sizeof(uint32), funCompare_uint32_1_2_0);//there is no need to sort by read or gene actually

    std::unordered_map<uint32, uint32> geneReadCount; //number of reads per gene
    uint32 nRumi=0;
    bool skipUMI=false;
    uintUMI umiPrev = (uintUMI)-1;
    uintRead readPrev = (uintRead)-1;
    for (uint32 iR=0; iR<nRm*rguStride; iR+=rguStride) {//for each umi, find intersection of genes from each read
        uintUMI umi1 = rGUm[iR+1];
        if (umi1!=umiPrev) {//starting new UMI
            umiPrev = umi1;
            if (umiGeneMapCount.count(umi1)>0) {
                skipUMI = true;//this UMI is skipped because it was among uniquely mapped
            } else {
                skipUMI = false;//new good umi
                geneReadCount.clear();
                nRumi=0;
                readPrev = (uintRead)-1;
            };
        };

        if (skipUMI)
            continue; //this UMI is skipped because it was among uniquely mapped

        uintRead read1 = rGUm[iR+2];
        if (read1 != readPrev) {
            ++nRumi;
            readPrev = read1;
        };

        uint32 g1 = rGUm[iR+0] ^ geneMultMark; //XOR to unset the geneMultMark bit
        geneReadCount[g1]++;

        if (iR == nRm*rguStride-rguStride || umi1 != rGUm[iR+1+rguStride]) {//record this umi
            uint32 ng=0;
            for (const auto &gg: geneReadCount) {
                if (gg.second == nRumi)
                    ++ng;
            };
            vector<uint32> vg;
            vg.reserve(ng);
            for (const auto &gg: geneReadCount) {
                if (gg.second == nRumi)
                    vg.push_back(gg.first);
            };
            umiGenes.push_back(vg);
        };
    };
    return umiGenes;
}

vector<double> SoloFeature::uniqueGeneCounts(uint32 iCB, const std::map<uint32,uint32> &genesM, uint32 indDedup)
{// this cell's unique counts (one dedup type) of the multigene genes, indexed as in genesM
    vector<double> gEu(genesM.size(), 0);
    for (uint32 igm=countCellGeneUMIindex[iCB]; igm<countCellGeneUMIindex[iCB+1]; igm+=countMatStride) {
        auto g1 = genesM.find(countCellGeneUMI[igm]);
        if (g1!=genesM.end())
            gEu[g1->second]=(double)countCellGeneUMI[igm+1+indDedup];
    };
    return gEu;
}

void SoloFeature::collapseMultiGeneReads(uint32 iCB, uint32 *rGU, const vector<uint32> &gReadS, uint32 nGenes, uint32 nGenesMult, const UmiGeneCounts &umiGeneMapCount)
{// distribute UMIs of multigene reads over their genes with the requested methods, record them in countMatMult
    if (readInfo.size()>0) {
        for (uint32 iR=gReadS[nGenes]; iR<gReadS[nGenes+nGenesMult]; iR+=rguStride) {//cycle over multi-gene reads to record their CB and UMI, no corrections
            uint64 iread1 = rGU[iR+rguR];
            readInfo[iread1].cb = indCB[iCB];
            readInfo[iread1].umi = rGU[iR+rguU];
        };
    };

    //only genes with nReads = nReads-for-this-UMI are kept for each UMI
    std::vector<vector<uint32>> umiGenes = multiGeneUMIgenes(rGU + gReadS[nGenes], ( gReadS[nGenes+nGenesMult] - gReadS[nGenes] ) / rguStride, umiGeneMapCount);

    std::map<uint32,uint32> genesM; //genes to quantify
    {//collect all genes, replace geneID with index in umiGenes
        uint32 ng = 0;
        for (auto &uu: umiGenes) {
            for (auto &gg: uu) {
                if (genesM.count(gg) == 0) {//new gene
                    genesM[gg]=ng;
                    ++ng;
                };
                gg = genesM[gg];
            };
        };
    };

    vector<double> gEuniform(genesM.size(), 0);
    for (auto &ug: umiGenes) {//gEuniform=uniformly distribute multigene UMIs
        for (auto &gg: ug) {
            gEuniform[gg] += 1.0 / double(ug.size()); // 1/n_genes_umi
        };
    };

    const uint32 nDedup=pSolo.umiDedup.yes.N;
    vector<vector<double>> gErescue(nDedup), gEpropUnique(nDedup), gEem(nDedup);
    for (uint32 indDedup=0; indDedup < nDedup; indDedup++) {
        if (pSolo.multiMap.yes.Rescue)
            gErescue[indDedup] = multiGeneRescue(umiGenes, gEuniform, uniqueGeneCounts(iCB, genesM, indDedup));
        if (pSolo.multiMap.yes.PropUnique)
            gEpropUnique[indDedup] = multiGenePropUnique(umiGenes, uniqueGeneCounts(iCB, genesM, indDedup));
        if (pSolo.multiMap.yes.EM)
            gEem[indDedup] = multiGeneEM(umiGenes, gEuniform, uniqueGeneCounts(iCB, genesM, indDedup));
    };

    {//write to countMatMult
        if (countMatMult.m.size() < countMatMult.i[iCB+1] + genesM.size()*countMatMult.s*nDedup + 100) //+100 just in case
            countMatMult.m.resize((countMatMult.i[iCB+1] + genesM.size()*countMatMult.s*nDedup + 100)*2);

        for (const auto &gm: genesM) {
            countMatMult.m[countMatMult.i[iCB+1] + 0] = gm.first;

            for (uint32 indDedup=0; indDedup < nDedup; indDedup++) {
                uint32 ind1 = countMatMult.i[iCB+1] + indDedup;

                if (pSolo.multiMap.yes.Uniform)
                    countMatMult.m[ind1 + pSolo.multiMap.countInd.Uniform] = gEuniform[gm.second];

                if (pSolo.multiMap.yes.Rescue)
                    countMatMult.m[ind1 + pSolo.multiMap.countInd.Rescue] = gErescue[indDedup][gm.second];

                if (pSolo.multiMap.yes.PropUnique)
                    countMatMult.m[ind1 + pSolo.multiMap.countInd.PropUnique] = gEpropUnique[indDedup][gm.second];

                if (pSolo.multiMap.yes.EM)
                    countMatMult.m[ind1 + pSolo.multiMap.countInd.EM] = gEem[indDedup][gm.second];

                countMatMult.i[iCB+1] += countMatMult.s;
            };
        };
    };
}

vector<double> SoloFeature::multiGeneRescue(const vector<vector<uint32>> &umiGenes, const vector<double> &gEuniform, const vector<double> &gEu)
{// distribute each UMI proportionally to uniform + unique counts
    vector<double> gE(gEu.size(), 0);
    for (auto &ug: umiGenes) {
        double norm1 = 0.0;
        for (auto &gg: ug)
            norm1 += gEuniform[gg]+gEu[gg];

        if (norm1==0.0)
            continue; //this should not happen since gEuniform is non-zero for all genes involved
        norm1 = 1.0 / norm1;

        for (auto &gg: ug) {
            gE[gg] += (gEuniform[gg]+gEu[gg])*norm1;
        };
    };
    return gE;
}

vector<double> SoloFeature::multiGenePropUnique(const vector<vector<uint32>> &umiGenes, const vector<double> &gEu)
{// distribute each UMI proportionally to unique counts, uniformly if its genes have none
    vector<double> gE(gEu.size(), 0);
    for (auto &ug: umiGenes) {
        double norm1 = 0.0;
        for (auto &gg: ug)
            norm1 += gEu[gg];

        if (norm1==0.0) {//this UMI has no genes with unique mappers - distribute it uniformly
            for (auto &gg: ug)
                gE[gg] += 1.0 / double(ug.size());
        } else {//this UMI has genes with unique mappers - distribute it proportionally to unique mappers
            norm1 = 1.0 / norm1;
            for (auto &gg: ug)
                gE[gg] += gEu[gg]*norm1;
        };
    };
    return gE;
}

vector<double> SoloFeature::multiGeneEM(const vector<vector<uint32>> &umiGenes, const vector<double> &gEuniform, const vector<double> &gEu)
{// EM over unique + multigene counts, started from unique + uniform; returns the multigene part only
    vector<double> gEM1 = gEuniform;
    for (uint32 ii=0; ii<gEM1.size(); ii++)
        gEM1[ii] += gEu[ii]; //start with sum of unique and uniform

    vector<double> gEM2(gEu.size(), 0);
    vector<double> gE;

    auto *gEM1p=&gEM1;
    auto *gEM2p=&gEM2;

    double maxAbsChange=1;
    uint32 iterI=0;
    while(true) {
        ++iterI;

        auto &gEMold=*gEM1p; //for convenience - to use instead of pointer
        auto &gEMnew=*gEM2p;

        std::copy(gEu.begin(), gEu.end(), gEMnew.begin());//gEMnew is initialized with unique counts

        for (const auto &ug: umiGenes)
            soloEMDistribute(ug,gEMold,gEMnew);

        maxAbsChange=0.0;
        for (uint32 ii=0; ii<gEMnew.size(); ii++) {
            double change1 = abs(gEMnew[ii]-soloEMThreshold(gEMold[ii]));
            if (change1 > maxAbsChange)
                maxAbsChange = change1;
        };

        if (maxAbsChange < 0.01 || iterI>100) {//hardcoded
            gE = gEMnew;
            break;
        };

        swap(gEM1p, gEM2p); //swap new and old for the next interation of EM
    };

    for (uint32 ii=0; ii<gEM1.size(); ii++)
        gE[ii] -= gEu[ii]; //only multimapper counts
    return gE;
}

////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////// sorting functions
inline int funCompareSolo1 (const void *a, const void *b) {
    uint32 *va= (uint32*) a;
    uint32 *vb= (uint32*) b;

    if (va[1]>vb[1]) {
        return 1;
    } else if (va[1]<vb[1]) {
        return -1;
    } else if (va[0]>vb[0]){
        return 1;
    } else if (va[0]<vb[0]){
        return -1;
    } else {
        return 0;
    };
};

inline int funCompare_uint32_1_2_0 (const void *a, const void *b) {
    uint32 *va= (uint32*) a;
    uint32 *vb= (uint32*) b;

    if (va[1]>vb[1]) {
        return 1;
    } else if (va[1]<vb[1]) {
        return -1;
    } else if (va[2]>vb[2]){
        return 1;
    } else if (va[2]<vb[2]){
        return -1;
    } else if (va[0]>vb[0]){
        return 1;
    } else if (va[0]<vb[0]){
        return -1;
    } else {
        return 0;
    };
};


////////////////////////////////////////////////////////////////////////////////////////////////
uint32 SoloFeature::umiArrayCorrect_CR(const uint32 nU0, uintUMI *umiArr, const bool readInfoRec, const bool nUMIyes, unordered_map <uintUMI,uintUMI> &umiCorr)
{
    qsort(umiArr, nU0, umiArrayStride*sizeof(uint32), funCompareSolo1);
    
    for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
        
        umiArr[iu+2] = umiArr[iu+0]; //stores corrected UMI for 1MM_CR and 1MM_Directional
        for (uint64 iuu=(nU0-1)*umiArrayStride; iuu>iu; iuu-=umiArrayStride) {

            uint32 uuXor = umiArr[iu+0] ^ umiArr[iuu+0];

            if ( (uuXor >> (__builtin_ctz(uuXor)/2)*2) <= 3 ) {//1MM                 
                umiArr[iu+2]=umiArr[iuu+0];//replace iu with iuu
                break;
            };
        };
    };
    
    if (readInfoRec) {//record corrections
        for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
            if (umiArr[iu+0] != umiArr[iu+2])
                umiCorr[umiArr[iu+0]]=umiArr[iu+2];
        };
    };
    
    if (!nUMIyes) {
        return 0;
    } else {
        unordered_set<uintUMI> umiC;
        for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
            umiC.insert(umiArr[iu+2]);
        };
       return umiC.size();
    };
};

/////////////////////////////////////////////////////////////////////////////////////////////////////////
uint32 SoloFeature::umiArrayCorrect_Directional(const uint32 nU0, uintUMI *umiArr, const bool readInfoRec, const bool nUMIyes, unordered_map <uintUMI,uintUMI> &umiCorr, const int32 dirCountAdd)
{
    if (dirCountAdd==-1) {
        const auto groups=collapseDirectionalUMIs(umiArr,nU0,umiArrayStride);
        if (readInfoRec) for (uint64 iu=0;iu<uint64(nU0)*umiArrayStride;iu+=umiArrayStride)
            if (umiArr[iu]!=umiArr[iu+2]) umiCorr[umiArr[iu]]=umiArr[iu+2];
        return nUMIyes ? groups:0;
    }
    qsort(umiArr, nU0, umiArrayStride*sizeof(uint32), funCompareNumbersReverseShift<uint32, 1>);//TODO no need to sort by sequence here, only by count. 
    
    for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride)
        umiArr[iu+2] = umiArr[iu+0]; //initialized - it will store corrected UMI for 1MM_CR and 1MM_Directional

    uint32 nU1 = nU0;
    for (uint64 iu=umiArrayStride; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
        
        for (uint64 iuu=0; iuu<iu; iuu+=umiArrayStride) {

            uint32 uuXor = umiArr[iu+0] ^ umiArr[iuu+0];

            if ( (uuXor >> (__builtin_ctz(uuXor)/2)*2) <= 3 && umiArr[iuu+1] >= (2*umiArr[iu+1]+dirCountAdd) ) {//1MM && directional condition
                umiArr[iu+2]=umiArr[iuu+2];//replace iuu with iu-corrected
                nU1--;
                break;
            };
        };
    };
    
    if (readInfoRec) {//record corrections
        for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
            if (umiArr[iu+0] != umiArr[iu+2])
                umiCorr[umiArr[iu+0]]=umiArr[iu+2];
        };
    };
    
    if (!nUMIyes) {
        return 0;
    } else {
        unordered_set<uintUMI> umiC;
        for (uint64 iu=0; iu<nU0*umiArrayStride; iu+=umiArrayStride) {
            umiC.insert(umiArr[iu+2]);
        };
        if (umiC.size()!=nU1)
            cout << nU1 <<" "<< umiC.size()<<endl;
        return umiC.size();
    };
};

