#include "IncludeDefine.h"
#include "Parameters.h"
#include "ErrorWarning.h"
#include "sysRemoveDir.h"
#include "GlobalVariables.h"
#include "signalFromBAM.h"
#include "bamRemoveDuplicates.h"
#include "TimeFunctions.h"
#include SAMTOOLS_BGZF_H

//for mkdir
#include <sys/stat.h>
#include <cerrno>

// Compute runtime variables, create temp directory, open output BAM/SAM files,
// and dispatch early-exit run modes (inputAlignmentsFromBAM).
void Parameters::inputParameters_runtimeSetup() {

    ///////////////////////////////////////// Old variables
    //splitting
    maxNsplit=10;

////////////////////////////////////////////////////// Calculate and check parameters
    iReadAll=0;

    pGe.initialize(this);

    //directory permissions TODO: this needs to be done before outPrefixFileName is created
    if (runDirPermIn=="User_RWX") {
        runDirPerm=S_IRWXU;
    } else if (runDirPermIn=="All_RWX") {
        runDirPerm= S_IRWXU | S_IRWXG | S_IRWXO;
    } else {
        ostringstream errOut;
        errOut << "EXITING because of FATAL INPUT ERROR: unrecognized option in --runDirPerm=" << runDirPerm << "\n";
        errOut << "SOLUTION: use one of the allowed values of --runDirPerm : 'User_RWX' or 'All_RWX' \n";
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    if (outTmpDir=="-") {
#ifdef _WIN32
        // On Windows, default to the system temp directory so decompressed reads
        // are never written to a slow SMB network share when --outFileNamePrefix
        // points to one (e.g. \\server\share\prefix). PID keeps concurrent
        // instances isolated.
        {
            const char* sysTmp = getenv("TEMP");
            if (!sysTmp) sysTmp = getenv("TMP");
            if (!sysTmp) sysTmp = ".";
            outFileTmp = string(sysTmp) + "/STAR_" + to_string((int)GetCurrentProcessId()) + "/";
        }
#else
        outFileTmp=outFileNamePrefix +"_STARtmp/";
#endif
        if (runRestart.type!=1)
            sysRemoveDir (outFileTmp);
    } else {
        outFileTmp=outTmpDir + "/";
    };

    if (mkdir (outFileTmp.c_str(),runDirPerm)!=0 && errno != EEXIST && runRestart.type!=1) {
        ostringstream errOut;
        errOut <<"EXITING because of fatal ERROR: could not make temporary directory: "<< outFileTmp<<"\n";
        errOut <<"SOLUTION: (i) please check the path and writing permissions \n (ii) if you specified --outTmpDir, and this directory exists - please remove it before running STAR\n"<<flush;
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    //threaded or not
    g_threadChunks.threadBool=(runThreadN>1);

    //wigOut parameters
    if (outWigType.at(0)=="None") {
        outWigFlags.yes=false;
    } else if (outWigType.at(0)=="bedGraph") {
        outWigFlags.yes=true;
        outWigFlags.format=0;
    } else if (outWigType.at(0)=="wiggle") {
        outWigFlags.yes=true;
        outWigFlags.format=1;
    } else {
        ostringstream errOut;
        errOut << "EXITING because of FATAL INPUT ERROR: unrecognized option in --outWigType=" << outWigType.at(0) << "\n";
        errOut << "SOLUTION: use one of the allowed values of --outWigType : 'None' or 'bedGraph' \n";
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };
    if (outWigStrand.at(0)=="Stranded") {
        outWigFlags.strand=true;
    } else if (outWigStrand.at(0)=="Unstranded") {
        outWigFlags.strand=false;
    } else {
        ostringstream errOut;
        errOut << "EXITING because of FATAL INPUT ERROR: unrecognized option in --outWigStrand=" << outWigStrand.at(0) << "\n";
        errOut << "SOLUTION: use one of the allowed values of --outWigStrand : 'Stranded' or 'Unstranded' \n";
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    if (outWigType.size()==1) {//simple bedGraph
        outWigFlags.type=0;
    } else {
        if (outWigType.at(1)=="read1_5p") {
            outWigFlags.type=1;
        } else if (outWigType.at(1)=="read2") {
            outWigFlags.type=2;
        } else {
            ostringstream errOut;
            errOut << "EXITING because of FATAL INPUT ERROR: unrecognized second option in --outWigType=" << outWigType.at(1) << "\n";
            errOut << "SOLUTION: use one of the allowed values of --outWigType : 'read1_5p' \n";
            exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
    };

    //wigOut norm parameters
    if (outWigNorm.at(0)=="None") {
        outWigFlags.norm=0;
    } else if (outWigNorm.at(0)=="RPM") {
        outWigFlags.norm=1;
    } else {
        ostringstream errOut;
        errOut << "EXITING because of fatal parameter ERROR: unrecognized option in --outWigNorm=" << outWigNorm.at(0) << "\n";
        errOut << "SOLUTION: use one of the allowed values of --outWigNorm : 'None' or 'RPM' \n";
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };


    //remove duplicates parameters
    if (removeDuplicates.mode=="UniqueIdentical")
    {
        removeDuplicates.yes=true;
        removeDuplicates.markMulti=true;
    } else if (removeDuplicates.mode=="UniqueIdenticalNotMulti")
    {
        removeDuplicates.yes=true;
        removeDuplicates.markMulti=false;
    } else if (removeDuplicates.mode!="-")
    {
            ostringstream errOut;
            errOut << "EXITING because of fatal PARAMETERS error: unrecognized option in of --bamRemoveDuplicatesType="<<removeDuplicates.mode<<"\n";
            errOut << "SOLUTION: use allowed option: - or UniqueIdentical or UniqueIdenticalNotMulti";
            exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    runMode=runModeIn[0];
    if (runMode=="alignReads") {
        inOut->logProgress.open((outFileNamePrefix + "Log.progress.out").c_str());
    } else if (runMode=="inputAlignmentsFromBAM") {
        //at the moment, only wiggle output is implemented
        if (outWigFlags.yes) {
            *inOut->logStdOut << timeMonthDayTime() << " ..... reading from BAM, output wiggle\n" <<flush;
            inOut->logMain << timeMonthDayTime()    << " ..... reading from BAM, output wiggle\n" <<flush;
            string wigOutFileNamePrefix=outFileNamePrefix + "Signal";
            signalFromBAM(inputBAMfile, wigOutFileNamePrefix, *this);
            *inOut->logStdOut << timeMonthDayTime() << " ..... done\n" <<flush;
            inOut->logMain << timeMonthDayTime()    << " ..... done\n" <<flush;
        } else if (removeDuplicates.mode!="-") {
            *inOut->logStdOut << timeMonthDayTime() << " ..... reading from BAM, remove duplicates, output BAM\n" <<flush;
            inOut->logMain << timeMonthDayTime()    << " ..... reading from BAM, remove duplicates, output BAM\n" <<flush;
            bamRemoveDuplicates(inputBAMfile, (outFileNamePrefix+"Processed.out.bam").c_str(), *this);
            *inOut->logStdOut << timeMonthDayTime() << " ..... done\n" <<flush;
            inOut->logMain << timeMonthDayTime()    << " ..... done\n" <<flush;
        } else {
            ostringstream errOut;
            errOut <<"EXITING because of fatal INPUT ERROR: at the moment --runMode inputFromBAM only works with --outWigType bedGraph OR --bamRemoveDuplicatesType Identical"<<"\n";
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
        sysRemoveDir (outFileTmp);
        exit(0);
    };

    outSAMbool=false;
    outBAMunsorted=false;
    outBAMcoord=false;
    outCRAMbool=false;
    if (runMode=="alignReads" && outSAMmode != "None") {//open SAM file and write header
        if (outSAMtype.at(0)=="BAM" || outSAMtype.at(0)=="CRAM") {
            //CRAM reuses the proven BAM output path and is transcoded to referenceless
            //CRAM at finalization (see bamToCramReferenceless / STAR.cpp)
            outCRAMbool = (outSAMtype.at(0)=="CRAM");
            if (outCRAMbool && outStd!="Log") {
                ostringstream errOut;
                errOut <<"EXITING because of fatal PARAMETER error: --outSAMtype CRAM is not compatible with --outStd "<<outStd<<"\n";
                errOut <<"SOLUTION: re-run STAR with --outStd Log (the default) when using --outSAMtype CRAM\n";
                exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
            };
            if (outSAMtype.size()<2) {
                ostringstream errOut;
                errOut <<"EXITING because of fatal PARAMETER error: missing "<<outSAMtype.at(0)<<" option\n";
                errOut <<"SOLUTION: re-run STAR with one of the allowed values of --outSAMtype "<<outSAMtype.at(0)<<" Unsorted OR SortedByCoordinate OR both\n";
                exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
            };
            for (uint32 ii=1; ii<outSAMtype.size(); ii++) {
                if (outSAMtype.at(ii)=="Unsorted") {
                    outBAMunsorted=true;
                } else if (outSAMtype.at(ii)=="SortedByCoordinate") {
                    outBAMcoord=true;
                } else {
                    ostringstream errOut;
                    errOut <<"EXITING because of fatal input ERROR: unknown value for the word " <<ii+1<<" of outSAMtype: "<< outSAMtype.at(ii) <<"\n";
                    errOut <<"SOLUTION: re-run STAR with one of the allowed values of --outSAMtype "<<outSAMtype.at(0)<<" Unsorted or SortedByCoordinate or both\n";
                    exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
                };
            };
            //TODO check for conflicts
            if (outBAMunsorted) {
                if (outStd=="BAM_Unsorted") {
                    outBAMfileUnsortedName="-";
                } else {
                    outBAMfileUnsortedName=outFileNamePrefix + "Aligned.out.bam";
                };
                if (outCRAMbool)
                    outCRAMfileUnsortedName=outFileNamePrefix + "Aligned.out.cram";
                inOut->outBAMfileUnsorted = bgzf_open(outBAMfileUnsortedName.c_str(),("w"+to_string((long long) outBAMcompression)).c_str());
            };
            if (outBAMcoord) {
                if (outStd=="BAM_SortedByCoordinate") {
                    outBAMfileCoordName="-";
                } else {
                    outBAMfileCoordName=outFileNamePrefix + "Aligned.sortedByCoord.out.bam";
                };
                if (outCRAMbool)
                    outCRAMfileCoordName=outFileNamePrefix + "Aligned.sortedByCoord.out.cram";
                inOut->outBAMfileCoord = bgzf_open(outBAMfileCoordName.c_str(),("w"+to_string((long long) outBAMcompression)).c_str());
                if (outBAMsortingThreadN==0) {
                    outBAMsortingThreadNactual=min(6, runThreadN);
                } else {
                    outBAMsortingThreadNactual=outBAMsortingThreadN;
                };
                outBAMcoordNbins=max((uint32)outBAMsortingThreadNactual*3,outBAMsortingBinsN);
                outBAMsortingBinStart= new uint64 [outBAMcoordNbins];
                outBAMsortingBinStart[0]=1;//this initial value means that the bin sizes have not been determined yet

                outBAMsortTmpDir=outFileTmp+"/BAMsort/";
                if (mkdir(outBAMsortTmpDir.c_str(),runDirPerm)!=0 && errno != EEXIST) {
                    ostringstream errOut;
                    errOut <<"EXITING because of fatal ERROR: could not make BAMsort temporary directory: "<< outBAMsortTmpDir<<"\n";
                    errOut <<"SOLUTION: please check the path and writing permissions\n"<<flush;
                    exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
                };
            };
        } else if (outSAMtype.at(0)=="SAM") {
            if (outSAMtype.size()>1)
            {
                ostringstream errOut;
                errOut <<"EXITING because of fatal PARAMETER error: --outSAMtype SAM can cannot be combined with "<<outSAMtype.at(1)<<" or any other options\n";
                errOut <<"SOLUTION: re-run STAR with with '--outSAMtype SAM' only, or with --outSAMtype BAM Unsorted|SortedByCoordinate\n";
                exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
            };
            outSAMbool=true;
            if (outStd=="SAM") {
                inOut->outSAM = & std::cout;
            } else {
                inOut->outSAMfile.open((outFileNamePrefix + "Aligned.out.sam").c_str());
                inOut->outSAM = & inOut->outSAMfile;
            };
        } else if (outSAMtype.at(0)=="None") {
            //nothing to do, all flags are already false
        } else {
            ostringstream errOut;
            errOut <<"EXITING because of fatal input ERROR: unknown value for the first word of outSAMtype: "<< outSAMtype.at(0) <<"\n";
            errOut <<"SOLUTION: re-run STAR with one of the allowed values of outSAMtype: BAM, CRAM, or SAM \n"<<flush;
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
    };

    if (!outBAMcoord && outWigFlags.yes && runMode=="alignReads") {
        ostringstream errOut;
        errOut <<"EXITING because of fatal PARAMETER error: generating signal with --outWigType requires sorted BAM\n";
        errOut <<"SOLUTION: re-run STAR with with --outSAMtype BAM SortedByCoordinate, or, id you also need unsroted BAM, with --outSAMtype BAM SortedByCoordinate Unsorted\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };
}
