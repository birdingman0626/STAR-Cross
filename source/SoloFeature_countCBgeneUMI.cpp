#include "SoloFeature.h"
#include "streamFuns.h"
#include "TimeFunctions.h"
#include "SequenceFuns.h"
#include "systemFunctions.h"
#include "uninitArray.h"
#include "ErrorWarning.h"
#include <exception>
using namespace std;
void SoloFeature::countCBgeneUMI()
{    
    time_t rawTime;
    
    rguStride=2;
    if (pSolo.readIndexYes[featureType])
        rguStride=3; //to keep readI column

    if (pSolo.readInfoYes[featureType]) {
        readInfo.resize(nReadsInput,{(uint64)-1,(uint32)-1});
        time(&rawTime);
        P.inOut->logMain << timeMonthDayTime(rawTime) << " ... Allocated and initialized readInfo array, nReadsInput = " << nReadsInput <<endl;        
    };

    auto recordsStorage=makeUninitArray<uint32>(rguStride*nReadsMapped);
    rGeneUMI=recordsStorage.get(); // mutable algorithm view, allocation base never changes
    auto barcodePointers=makeUninitArray<uint32*>(nCB+1);
    rCBp=barcodePointers.get();
    auto whitelistPointers=makeUninitArray<uint32*>(pSolo.cbWLsize+1);
    uint32 **rCBpa=whitelistPointers.get();
    
    rCBp[0]=rGeneUMI;
    rCBpa[0]=rGeneUMI;
    vector<uint32> wlToSlot(pSolo.cbWLsize, (uint32)-1); //WL index -> slot (index in rCBp)
    nCB=0;//will count it again below
    for (uint32 ii=0; ii<pSolo.cbWLsize; ii++) {
        if (readFeatSum->cbReadCount[ii]>0) {
            wlToSlot[ii]=nCB;
            rCBp[nCB+1] = rCBp[nCB] + rguStride*readFeatSum->cbReadCount[ii];
            ++nCB;
        };
        rCBpa[ii+1]=rCBp[nCB];
    };

    //read and store the CB/gene/UMI from files
    time(&rawTime);
    P.inOut->logMain << timeMonthDayTime(rawTime) << " ... Finished allocating arrays for Solo " << nReadsMapped*rguStride*4.0/1024/1024/1024 <<" GiB" <<endl;

    ///////////////////////////////////////////////////////////////////////////
    ////////////// Input records
    readFlagCounts.flagCounts.reserve(nCB*3/2);
    readFlagCounts.flagCountsNoCB = {};
    vector<uint32> nReadPerCBunique1(pSolo.cbWLsize), nReadPerCBmulti1(pSolo.cbWLsize); //temp arrays to record # of reads for all cells in the WL
    inputRecordsAllFiles(wlToSlot, rCBpa, nReadPerCBunique1, nReadPerCBmulti1);
    readFlagCounts.countsAddNoCBarray(readFeatSum->readFlag.flagCountsNoCB);//add no-CB counts calculated in SoloReadFeature_record.cpp and not recorded to temp Solo files

    nReadPerCBtotal.resize(nCB);
    nReadPerCBunique.resize(nCB);
    for (uint32 icb=0; icb<nCB; icb++) {
        nReadPerCBunique[icb] = nReadPerCBunique1[indCB[icb]];
        nReadPerCBtotal[icb] = nReadPerCBunique[icb] + nReadPerCBmulti1[indCB[icb]];
    };

    //debug
    /*{
        uint64 n1=0,n2=0;
        for (uint32 icb=0; icb<nCB; icb++) {
            n1 += nReadPerCBtotal[icb];
        };

        for (auto &c: readFlagCounts.flagCounts) {
            n2+= c.second[readFlagCounts.countedU] + c.second[readFlagCounts.countedM];
        };

        cout << "n1,2=" << n1<<" "<<n2<<endl;
    };*/

    nReadPerCB.resize(nCB);
    nReadPerCBmax=0;
    for (uint32 iCB=0; iCB<nCB; iCB++) {
        nReadPerCB[iCB] = (rCBpa[indCB[iCB]]-rCBp[iCB])/rguStride;  //number of reads that were matched to WL, rCBpa accumulated reference to the last element+1
                                                                    //for multimappers this is the number of all alignments > number of reads
        nReadPerCBmax=max(nReadPerCBmax,nReadPerCB[iCB]);
    };    
    
    time(&rawTime);
    P.inOut->logMain << timeMonthDayTime(rawTime) << " ... Finished reading reads from Solo files nCB="<<nCB <<", nReadPerCBmax="<<nReadPerCBmax;
    P.inOut->logMain <<", yesWLmatch="<<readFeatSum->stats.V[readFeatSum->stats.yesWLmatch]<<endl;
    
    //////////////////////////////////////////////////////////////////////////////
    /////////////////////////// collapse each CB
    nUMIperCB.resize(nCB);
    nGenePerCB.resize(nCB);
    
                     //dedup options        //gene ID
    countMatStride = pSolo.umiDedup.yes.N + 1;
    countCellGeneUMI.resize(nReadsMapped*countMatStride/5+16); //5 is heuristic, will be resized if needed
    countCellGeneUMIindex.resize(nCB+1, 0);
    
    if (pSolo.multiMap.yes.multi) {
                    //gene   //uniform  //rescue
        countMatMult.s = 1 + pSolo.multiMap.yes.N * pSolo.umiDedup.yes.N;
        countMatMult.m.resize(nReadsMapped*countMatMult.s/5+16);
        countMatMult.i.resize(nCB+1, 0);
    };

    collapseUMIall();
        
    P.inOut->logMain << "RAM for solo feature "<< SoloFeatureTypes::Names[featureType] <<"\n"
                     <<  linuxProcMemory() << flush;        
    recordsStorage.reset(); rGeneUMI=nullptr;
    barcodePointers.reset(); rCBp=nullptr;
    whitelistPointers.reset();
    
    time(&rawTime);
    P.inOut->logMain << timeMonthDayTime(rawTime) << " ... Finished collapsing UMIs" <<endl;
};

//Reads the temporary files of all mapping threads in parallel, with the result of reading them one after another:
//the records of each CB are in file order, readFlagCounts has the same keys in the same order.
//Pass 1 counts each file's records per CB, which gives each file its own range in every CB slot; pass 2 stores them there.
void SoloFeature::inputRecordsAllFiles(const vector<uint32> &wlToSlot, uint32 **rCBpa, vector<uint32> &nReadPerCBunique1, vector<uint32> &nReadPerCBmulti1)
{
    const int nFiles=P.runThreadN;
    vector<SoloRecordSlots> slots(nFiles);
    vector<SoloReadFlagClass> fileFlags(nFiles);
    for (int ii=0; ii<nFiles; ii++) {
        slots[ii].wlToSlot=wlToSlot.data();
        slots[ii].slotStart=rCBp;
        slots[ii].stride=rguStride;
        slots[ii].pos.assign(nCB, 0);
        fileFlags[ii].trackOrder=true;
    };

    auto inputPass = [&]() {
        vector<std::exception_ptr> errors(nFiles);
        #pragma omp parallel for schedule(dynamic,1) num_threads(nFiles)
        for (int ii=0; ii<nFiles; ii++) {
            try {
                readFeatAll[ii]->inputRecords(slots[ii], readBarSum->cbReadCountExact, readInfo, fileFlags[ii], nReadPerCBunique1, nReadPerCBmulti1);
            } catch (...) {
                errors[ii]=std::current_exception();
            };
        };
        for (auto &e: errors)
            if (e)
                std::rethrow_exception(e);
    };

    inputPass(); //count

    //each file's start in each slot: the records of the files before it
    uint64 nNoSlot=0;
    for (auto &sl: slots)
        nNoSlot += sl.nNoSlot;
    for (uint32 islot=0; islot<nCB; islot++) {
        uint64 nRec=0;
        for (auto &sl: slots) {
            uint32 nFile=sl.pos[islot];
            sl.pos[islot]=(uint32)nRec;
            nRec += nFile;
        };
        if (nRec > (uint64)(rCBp[islot+1]-rCBp[islot])/rguStride)
            nNoSlot += nRec;
    };
    if (nNoSlot>0) {
        exitWithError("BUG: Solo records do not fit the per-cell-barcode arrays, number of misplaced records=" + to_string(nNoSlot)
                      + "\nSOLUTION: please report this error with the full Log.out file\n",
                      std::cerr, P.inOut->logMain, EXIT_CODE_BUG, P);
    };

    for (auto &sl: slots)
        sl.countOnly=false;
    inputPass(); //store

    //rCBpa: end of the records of each CB; after pass 2 the last file's pos is the slot's record count
    for (uint32 ii=0; ii<pSolo.cbWLsize; ii++) {
        if (wlToSlot[ii]!=(uint32)-1)
            rCBpa[ii] = rCBp[wlToSlot[ii]] + (uint64)slots[nFiles-1].pos[wlToSlot[ii]]*rguStride;
    };

    for (int ii=0; ii<nFiles; ii++) {//in file order
        readFeatSum->addStats(*readFeatAll[ii]);//sum stats: has to be done after inputRecords, since the stats values are updated there
        readFlagCounts.mergeInOrder(fileFlags[ii]);
        decltype(fileFlags[ii].flagCounts)().swap(fileFlags[ii].flagCounts); //release memory
        vector<uintCB>().swap(fileFlags[ii].insertOrder);
    };
};
