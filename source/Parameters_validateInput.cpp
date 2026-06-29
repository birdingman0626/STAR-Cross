#include "IncludeDefine.h"
#include "Parameters.h"
#include "ErrorWarning.h"
#include "sysRemoveDir.h"
#include SAMTOOLS_BGZF_H

//for mkdir
#include <sys/stat.h>

// Validate all user-supplied parameter values and compute derived flags.
// Must be called after inputParameters_runtimeSetup().
void Parameters::inputParameters_validate() {

    //versions
    for (uint ii=0;ii<1;ii++) {
        if (parArray[ii]->inputLevel>0) {
            ostringstream errOut;
            errOut <<"EXITING because of fatal input ERROR: the version parameter "<< parArray[ii]->nameString << " cannot be re-defined by the user\n";
            errOut <<"SOLUTION: please remove this parameter from the command line or input files and re-start STAR\n"<<flush;
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
    };

    //run
    if (runThreadN<=0) {
        ostringstream errOut;
        errOut <<"EXITING: fatal input ERROR: runThreadN must be >0, user-defined runThreadN="<<runThreadN<<"\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    //
    if (outFilterType=="BySJout" && outSAMorder=="PairedKeepInputOrder") {
        ostringstream errOut;
        errOut <<"EXITING: fatal input ERROR: --outFilterType=BySJout is not presently compatible with --outSAMorder=PairedKeepInputOrder\n";
        errOut <<"SOLUTION: re-run STAR without setting one of those parameters.\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };
    if (!outSAMbool && outSAMorder=="PairedKeepInputOrder") {
        ostringstream errOut;
        errOut <<"EXITING: fatal input ERROR: --outSAMorder=PairedKeepInputOrder is presently only compatible with SAM output, i.e. default --outSMAtype SAM\n";
        errOut <<"SOLUTION: re-run STAR without --outSAMorder=PairedKeepInputOrder, or with --outSAMorder=PairedKeepInputOrder --outSMAtype SAM .\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };
    //SJ filtering
    for (int ii=0;ii<4;ii++) {
        if (outSJfilterOverhangMin.at(ii)<0) outSJfilterOverhangMin.at(ii)=numeric_limits<int32>::max();
        if (outSJfilterCountUniqueMin.at(ii)<0) outSJfilterCountUniqueMin.at(ii)=numeric_limits<int32>::max();
        if (outSJfilterCountTotalMin.at(ii)<0) outSJfilterCountTotalMin.at(ii)=numeric_limits<int32>::max();
        if (outSJfilterDistToOtherSJmin.at(ii)<0) outSJfilterDistToOtherSJmin.at(ii)=numeric_limits<int32>::max();

        if (alignSJstitchMismatchNmax.at(ii)<0) alignSJstitchMismatchNmax.at(ii)=numeric_limits<int32>::max();
    };

    if (limitGenomeGenerateRAM==0) {//must be >0
        inOut->logMain <<"EXITING because of FATAL PARAMETER ERROR: limitGenomeGenerateRAM=0\n";
        inOut->logMain <<"SOLUTION: please specify a >0 value for limitGenomeGenerateRAM\n"<<flush;
        exit(1);
    } else if (limitGenomeGenerateRAM>1000000000000) {//
        inOut->logMain <<"WARNING: specified limitGenomeGenerateRAM="<<limitGenomeGenerateRAM<<" bytes appears to be too large, if you do not have enough memory the code will crash!\n"<<flush;
    };

    outSAMfilter.KeepOnlyAddedReferences=false;
    outSAMfilter.KeepAllAddedReferences=false;
    outSAMfilter.yes=true;
    if (outSAMfilter.mode.at(0)=="KeepOnlyAddedReferences") {
        outSAMfilter.KeepOnlyAddedReferences=true;
    } else if (outSAMfilter.mode.at(0)=="KeepAllAddedReferences") {
        outSAMfilter.KeepAllAddedReferences=true;
    } else if (outSAMfilter.mode.at(0)=="None") {
      outSAMfilter.yes=false;
    } else {
        ostringstream errOut;
        errOut <<"EXITING because of FATAL INPUT ERROR: unknown/unimplemented value for --outSAMfilter: "<<outSAMfilter.mode.at(0) <<"\n";
        errOut <<"SOLUTION: specify one of the allowed values: KeepOnlyAddedReferences or None\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    if ( (outSAMfilter.KeepOnlyAddedReferences || outSAMfilter.KeepAllAddedReferences) && pGe.gFastaFiles.at(0)=="-" ) {
        ostringstream errOut;
        errOut <<"EXITING because of FATAL INPUT ERROR: --outSAMfilter KeepOnlyAddedReferences OR KeepAllAddedReferences options can only be used if references are added on-the-fly with --genomeFastaFiles" <<"\n";
        errOut <<"SOLUTION: use default --outSAMfilter None, OR add references with --genomeFataFiles\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };


    if (outMultimapperOrder.mode=="Old_2.4") {
        outMultimapperOrder.random=false;
    } else if (outMultimapperOrder.mode=="Random") {
        outMultimapperOrder.random=true;
    } else {
        ostringstream errOut;
        errOut <<"EXITING because of FATAL INPUT ERROR: unknown/unimplemented value for --outMultimapperOrder: "<<outMultimapperOrder.mode <<"\n";
        errOut <<"SOLUTION: specify one of the allowed values: Old_2.4 or SortedByCoordinate or Random\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    //read parameters
    readFilesInit();

    //two-pass
    if (parArray.at(twoPass.pass1readsN_par)->inputLevel>0  && twoPass.mode=="None") {
        ostringstream errOut;
        errOut << "EXITING because of fatal PARAMETERS error: --twopass1readsN is defined, but --twoPassMode is not defined\n";
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    twoPass.yes=false;
    twoPass.pass2=false;
    if (twoPass.mode!="None") {//2-pass parameters
        if (runMode!="alignReads") {
            ostringstream errOut;
            errOut << "EXITING because of fatal PARAMETERS error: 2-pass mapping option  can only be used with --runMode alignReads\n";
            errOut << "SOLUTION: remove --twopassMode option";
            exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };

        if (twoPass.mode!="Basic") {
            ostringstream errOut;
            errOut << "EXITING because of fatal PARAMETERS error: unrecognized value of --twopassMode="<<twoPass.mode<<"\n";
            errOut << "SOLUTION: for the 2-pass mode, use allowed values --twopassMode: Basic";
            exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };

        if (twoPass.pass1readsN==0) {
            ostringstream errOut;
            errOut << "EXITING because of fatal PARAMETERS error: --twopass1readsN = 0 in the 2-pass mode\n";
            errOut << "SOLUTION: for the 2-pass mode, specify --twopass1readsN > 0. Use a very large number or -1 to map all reads in the 1st pass.\n";
            exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };

        if (pGe.gLoad!="NoSharedMemory") {
            ostringstream errOut;
            errOut << "EXITING because of fatal PARAMETERS error: 2-pass method is not compatible with --genomeLoad "<<pGe.gLoad<<"\n";
            errOut << "SOLUTION: re-run STAR with --genomeLoad NoSharedMemory ; this is the only option compatible with --twopassMode Basic .\n";
            exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
        twoPass.yes=true;
        twoPass.dir=outFileNamePrefix+"_STARpass1/";
        sysRemoveDir (twoPass.dir);
        if (mkdir (twoPass.dir.c_str(),runDirPerm)!=0) {
            ostringstream errOut;
            errOut <<"EXITING because of fatal ERROR: could not make pass1 directory: "<< twoPass.dir<<"\n";
            errOut <<"SOLUTION: please check the path and writing permissions \n";
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
    };

    // openReadFiles depends on twoPass for reading SAM header
    if (runMode=="alignReads" && pGe.gLoad!="Remove" && pGe.gLoad!="LoadAndExit") {//open reads files to check if they are present
        openReadsFiles();

        if (readNends > 2 && pSolo.typeStr=="None") {//could have >2 mates only for Solo
            ostringstream errOut;
            errOut <<"EXITING: because of fatal input ERROR: number of read mates files > 2: " <<readNends << "\n";
            errOut <<"SOLUTION:specify only one or two files in the --readFilesIn option. If file names contain spaces, use quotes: \"file name\"\n";
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };

        if ( runMode=="alignReads" && outReadsUnmapped=="Fastx" ) {//open unmapped reads file
            for (uint imate=0;imate<readNends;imate++) {
                ostringstream ff;
                ff << outFileNamePrefix << "Unmapped.out.mate" << imate+1;
                inOut->outUnmappedReadsStream[imate].open(ff.str().c_str());
            };
        };
    };

    if (outSAMmapqUnique<0 || outSAMmapqUnique>255) {
            ostringstream errOut;
            errOut <<"EXITING because of FATAL input ERROR: out of range value for outSAMmapqUnique=" << outSAMmapqUnique <<"\n";
            errOut <<"SOLUTION: specify outSAMmapqUnique within the range of 0 to 255\n";
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    //variation
    var.yes=false;
    if (var.vcfFile!="-") {
        var.yes=true;
    };

    //WASP
    wasp.yes=false;
    wasp.SAMtag=false;
    if (wasp.outputMode=="SAMtag") {
        wasp.yes=true;
        wasp.SAMtag=true;
        var.heteroOnly=true;
    } else if (wasp.outputMode=="None") {
        //nothing to do
    } else {
        ostringstream errOut;
        errOut <<"EXITING because of FATAL INPUT ERROR: unknown/unimplemented --waspOutputMode option: "<<wasp.outputMode <<"\n";
        errOut <<"SOLUTION: re-run STAR with allowed --waspOutputMode options: None or SAMtag\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    if (wasp.yes && !var.yes) {
        ostringstream errOut;
        errOut <<"EXITING because of FATAL INPUT ERROR: --waspOutputMode option requires VCF file: "<<wasp.outputMode <<"\n";
        errOut <<"SOLUTION: re-run STAR with --waspOutputMode ... and --varVCFfile /path/to/file.vcf\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    // WASP vW tag is now emitted for SAM as well as BAM/CRAM output (upstream PR #2617),
    // so --waspOutputMode no longer requires BAM.

    //quantification parameters
    quant.yes=false;
    quant.geCount.yes=false;
    quant.trSAM.yes=false;
    quant.trSAM.bamYes=false;
    quant.trSAM.indel=false;
    quant.trSAM.softClip=false;
    quant.trSAM.singleEnd=false;
    if (quant.mode.at(0) != "-") {
        quant.yes=true;
        for (uint32 ii=0; ii<quant.mode.size(); ii++) {
            if (quant.mode.at(ii)=="TranscriptomeSAM") {
                quant.trSAM.yes=true;

                if (quant.trSAM.bamCompression>-2)
                    quant.trSAM.bamYes=true;

                if (quant.trSAM.bamYes) {
                    if (outStd=="BAM_Quant") {
                        outFileNamePrefix="-";
                    } else {
                        outQuantBAMfileName=outFileNamePrefix + "Aligned.toTranscriptome.out.bam";
                    };
                    inOut->outQuantBAMfile=bgzf_open(outQuantBAMfileName.c_str(),("w"+to_string((long long) quant.trSAM.bamCompression)).c_str());
                };
                if (quant.trSAM.output=="BanSingleEnd_BanIndels_ExtendSoftclip") {
                    quant.trSAM.indel=false;
                    quant.trSAM.softClip=false;
                    quant.trSAM.singleEnd=false;
                } else if (quant.trSAM.output=="BanSingleEnd") {
                    quant.trSAM.indel=true;
                    quant.trSAM.softClip=true;
                    quant.trSAM.singleEnd=false;
                } else if (quant.trSAM.output=="BanSingleEnd_ExtendSoftclip") {
                    quant.trSAM.indel=true;
                    quant.trSAM.softClip=false;
                    quant.trSAM.singleEnd=false;
                };
            } else if  (quant.mode.at(ii)=="GeneCounts") {
                quant.geCount.yes=true;
                quant.geCount.outFile=outFileNamePrefix + "ReadsPerGene.out.tab";
            } else {
                ostringstream errOut;
                errOut << "EXITING because of fatal INPUT error: unrecognized option in --quantMode=" << quant.mode.at(ii) << "\n";
                errOut << "SOLUTION: use one of the allowed values of --quantMode : TranscriptomeSAM or GeneCounts or - .\n";
                exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
            };
        };
    };
    //these may be set in STARsolo or in SAM attributes
    quant.geneFull.yes=false;
    quant.gene.yes=false;


    outSAMstrandField.type=0; //none
    if (outSAMstrandField.in=="None") {
        outSAMstrandField.type=0;
    } else if (outSAMstrandField.in=="intronMotif") {
        outSAMstrandField.type=1;
    } else {
        ostringstream errOut;
        errOut << "EXITING because of fatal INPUT error: unrecognized option in outSAMstrandField=" << outSAMstrandField.in << "\n";
        errOut << "SOLUTION: use one of the allowed values of --outSAMstrandField : None or intronMotif \n";
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    //SAM attributes
    samAttributes();

    //solo
    pSolo.initialize(this);

    //clipping
    pClip.initialize(this);

    //alignEnds
    alignEndsType.ext[0][0]=false;
    alignEndsType.ext[0][1]=false;
    alignEndsType.ext[1][0]=false;
    alignEndsType.ext[1][1]=false;

    if (alignEndsType.in=="EndToEnd") {
        alignEndsType.ext[0][0]=true;
        alignEndsType.ext[0][1]=true;
        alignEndsType.ext[1][0]=true;
        alignEndsType.ext[1][1]=true;
    } else if (alignEndsType.in=="Extend5pOfRead1" ) {
        alignEndsType.ext[0][0]=true;
    } else if (alignEndsType.in=="Extend5pOfReads12" ) {
        alignEndsType.ext[0][0]=true;
        alignEndsType.ext[1][0]=true;
    } else if (alignEndsType.in=="Extend3pOfRead1" ) {
        alignEndsType.ext[0][1]=true;
    } else if (alignEndsType.in=="Local") {
        //nothing to do for now
    } else {
        ostringstream errOut;
        errOut <<"EXITING because of FATAL INPUT ERROR: unknown/unimplemented value for --alignEndsType: "<<alignEndsType.in <<"\n";
        errOut <<"SOLUTION: re-run STAR with --alignEndsType Local OR EndToEnd OR Extend5pOfRead1 OR Extend3pOfRead1\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    //open compilation-dependent streams
    #ifdef OUTPUT_localChains
            inOut->outLocalChains.open((outFileNamePrefix + "LocalChains.out.tab").c_str());
    #endif

    strcpy(genomeNumToNT,"ACGTN");

   //sjdb insert on the fly
    sjdbInsert.pass1=false;
    sjdbInsert.pass2=false;
    sjdbInsert.yes=false;
    if (pGe.sjdbFileChrStartEnd.at(0)!="-" || pGe.sjdbGTFfile!="-") {//will insert annotated sjdb on the fly
       sjdbInsert.pass1=true;
       sjdbInsert.yes=true;
    };
    if (twoPass.yes) {
       sjdbInsert.pass2=true;
       sjdbInsert.yes=true;
    };

    if (pGe.gLoad!="NoSharedMemory" && sjdbInsert.yes ) {
        ostringstream errOut;
        errOut << "EXITING because of fatal PARAMETERS error: on the fly junction insertion and 2-pass mappng cannot be used with shared memory genome \n" ;
        errOut << "SOLUTION: run STAR with --genomeLoad NoSharedMemory to avoid using shared memory\n" <<flush;
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    if (runMode=="alignReads" && sjdbInsert.yes )
    {//run-time genome directory, this is needed for genome files generated on the fly
        if (pGe.sjdbOverhang<=0) {
            ostringstream errOut;
            errOut << "EXITING because of fatal PARAMETERS error: pGe.sjdbOverhang <=0 while junctions are inserted on the fly with --sjdbFileChrStartEnd or/and --sjdbGTFfile\n";
            errOut << "SOLUTION: specify pGe.sjdbOverhang>0, ideally readmateLength-1";
            exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
        sjdbInsert.outDir=outFileNamePrefix+"_STARgenome/";
        sysRemoveDir (sjdbInsert.outDir);
        if (mkdir (sjdbInsert.outDir.c_str(),runDirPerm)!=0) {
            ostringstream errOut;
            errOut <<"EXITING because of fatal ERROR: could not make run-time genome directory directory: "<< sjdbInsert.outDir<<"\n";
            errOut <<"SOLUTION: please check the path and writing permissions \n";
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
    };

    if (outBAMcoord && limitBAMsortRAM==0) {//check limitBAMsortRAM
        if (pGe.gLoad!="NoSharedMemory") {
            ostringstream errOut;
            errOut <<"EXITING because of fatal PARAMETERS error: limitBAMsortRAM=0 (default) cannot be used with --genomeLoad="<<pGe.gLoad <<", or any other shared memory options\n";
            errOut <<"SOLUTION: please use default --genomeLoad NoSharedMemory, \n        OR specify --limitBAMsortRAM the amount of RAM (bytes) that can be allocated for BAM sorting in addition to shared memory allocated for the genome.\n        --limitBAMsortRAM typically has to be > 10000000000 (i.e 10GB).\n";
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
        inOut->logMain<<"WARNING: --limitBAMsortRAM=0, will use genome size as RAM limit for BAM sorting\n";
    };

    for (uint ii=0; ii<readNameSeparator.size(); ii++) {
        if (readNameSeparator.at(ii)=="space") {
            readNameSeparatorChar.push_back(' ');
        } else if (readNameSeparator.at(ii)=="none") {
            //nothing to do
        } else if (readNameSeparator.at(ii).size()==1) {
            readNameSeparatorChar.push_back(readNameSeparator.at(ii).at(0));
        } else{
            ostringstream errOut;
            errOut << "EXITING because of fatal PARAMETERS error: unrecognized value of --readNameSeparator="<<readNameSeparator.at(ii)<<"\n";
            errOut << "SOLUTION: use allowed values: space OR single characters";
            exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
    };

    //outSAMunmapped
    outSAMunmapped.yes=false;
    outSAMunmapped.within=false;
    outSAMunmapped.keepPairs=false;
    if (outSAMunmapped.mode.at(0)=="None" && outSAMunmapped.mode.size()==1) {
        //nothing to do, all false
    } else if (outSAMunmapped.mode.at(0)=="Within" && outSAMunmapped.mode.size()==1) {
        outSAMunmapped.yes=true;
        outSAMunmapped.within=true;
    } else if (outSAMunmapped.mode.at(0)=="Within" && outSAMunmapped.mode.at(1)=="KeepPairs") {
        outSAMunmapped.yes=true;
        outSAMunmapped.within=true;
        if (readNmates==2) //not readNends, since this control output of alignments
            outSAMunmapped.keepPairs=true;
    } else {
        ostringstream errOut;
        errOut << "EXITING because of fatal PARAMETERS error: unrecognized option for --outSAMunmapped=";
        for (uint ii=0; ii<outSAMunmapped.mode.size(); ii++) errOut <<" "<< outSAMunmapped.mode.at(ii);
        errOut << "\nSOLUTION: use allowed options: None OR Within OR Within KeepPairs";
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    alignEndsProtrude.nBasesMax=stoi(alignEndsProtrude.in.at(0),nullptr);
    alignEndsProtrude.concordantPair=false;
    if (alignEndsProtrude.nBasesMax>0) {//allow ends protrusion
        if (alignEndsProtrude.in.at(1)=="ConcordantPair") {
            alignEndsProtrude.concordantPair=true;
        } else if (alignEndsProtrude.in.at(1)=="DiscordantPair") {
            alignEndsProtrude.concordantPair=false;
        } else  {
            ostringstream errOut;
            errOut << "EXITING because of fatal PARAMETERS error: unrecognized option in of --alignEndsProtrude="<<alignEndsProtrude.in.at(1)<<"\n";
            errOut << "SOLUTION: use allowed option: ConcordantPair or DiscordantPair";
            exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
    };

    if (alignInsertionFlush.in=="None") {
        alignInsertionFlush.flushRight=false;
    } else if (alignInsertionFlush.in=="Right") {
        alignInsertionFlush.flushRight=true;
    } else  {
        ostringstream errOut;
        errOut << "EXITING because of fatal PARAMETERS error: unrecognized option in of --alignInsertionFlush="<<alignInsertionFlush.in<<"\n";
        errOut << "SOLUTION: use allowed option: None or Right";
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    //peOverlap
    if (peOverlap.NbasesMin>0) {
        peOverlap.yes=true;
    } else {
        peOverlap.yes=false;
    };

    //alignSoftClipAtReferenceEnds.in
    if (alignSoftClipAtReferenceEnds.in=="Yes") {
        alignSoftClipAtReferenceEnds.yes=true;
    } else if (alignSoftClipAtReferenceEnds.in=="No") {
        alignSoftClipAtReferenceEnds.yes=false;
    } else {
        ostringstream errOut;
        errOut << "EXITING because of fatal PARAMETERS error: unrecognized option in --alignSoftClipAtReferenceEnds   "<<alignSoftClipAtReferenceEnds.in<<"\n";
        errOut << "SOLUTION: use allowed option: Yes or No";
        exitWithError(errOut.str(),std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    outSAMreadIDnumber=false;
    if (outSAMreadID=="Number") {
        outSAMreadIDnumber=true;
    };


    //////////////////////////////////////////////////////////////////////////////////
    ////////////////////////////////////////////////////////////////////////////////// these parameters do not depend on other parameters
    /////////////////////////////////////////////////////////////////////////////////

    ////////////////////////////////////////////////////////// limitIObufferSize
    /* old before 2.7.9
    // in/out buffers
    #define BUFFER_InSizeFraction 0.5
    if (limitIObufferSize<limitOutSJcollapsed*Junction::dataSize+1000000) {
        ostringstream errOut;
        errOut <<"EXITING because of FATAL INPUT ERROR: --limitIObufferSize="<<limitIObufferSize <<" is too small for ";
        errOut << "--limitOutSJcollapsed*"<<Junction::dataSize<<"="<< limitOutSJcollapsed<<"*"<<Junction::dataSize<<"="<<limitOutSJcollapsed*Junction::dataSize<<"\n";
        errOut <<"SOLUTION: re-run STAR with larger --limitIObufferSize or smaller --limitOutSJcollapsed\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };
    chunkInSizeBytesArray=(uint) int((limitIObufferSize-limitOutSJcollapsed*Junction::dataSize)*BUFFER_InSizeFraction)/2;
    chunkOutBAMsizeBytes= (uint) int((1.0/BUFFER_InSizeFraction-1.0)*chunkInSizeBytesArray*2.0);
    chunkInSizeBytes=chunkInSizeBytesArray-2*(DEF_readSeqLengthMax+1)-2*DEF_readNameLengthMax;//to prevent overflow
    */

    if (limitIObufferSize.size() != 2)
        exitWithError("EXITING because of FATAL input ERROR: --limitIObufferSize requires 2 numbers since 2.7.9a.\n"
                      "SOLUTION: specify 2 numbers in --limitIObufferSize : size of input and output buffers in bytes.\n"
                        , std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);

    chunkInSizeBytesArray = limitIObufferSize[0]/readNends; //array size
    chunkInSizeBytes = chunkInSizeBytesArray-2*(DEF_readSeqLengthMax+1)-2*DEF_readNameLengthMax; //to prevent overflow - array is bigger to allow loading one read
    chunkOutBAMsizeBytes = limitIObufferSize[1];


    ///////////////////////////////////////////////////////// outSJ
    if (outSJ.type[0] == "None") {
        outSJ.yes = false;
    } else if (outSJ.type[0] == "Standard") {
        outSJ.yes = true;
    } else {
        exitWithError("EXITING because of FATAL input ERROR: unrecognized option in --outSJtype   " + outSJ.type[0] + '\n' +
                      "SOLUTION: use one of the allowed options: --outSJtype   Standard    OR    None\n"
                        , std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    if (outFilterType=="Normal") {
        outFilterBySJoutStage=0;
    } else if (outFilterType=="BySJout") {
        if (!outSJ.yes)
            exitWithError("EXITING because of FATAL input ERROR: --outFilterType BySJout requires --outSJtype Standard\n"
                      "SOLUTION: --outFilterType Normal    OR   --outFilterType BySJout --outSJtype Standard\n"
                        , std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);

        outFilterBySJoutStage=1;
    } else {
        ostringstream errOut;
        errOut <<"EXITING because of FATAL input ERROR: unknown value of parameter outFilterType: " << outFilterType <<"\n";
        errOut <<"SOLUTION: specify one of the allowed values: Normal | BySJout\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };

    ////////////////////////////////////////////////
    inOut->logMain << "Finished loading and checking parameters\n" <<flush;
}
