#include "Parameters.h"

// All parArray.push_back registrations extracted from Parameters::Parameters().
// Call order matches the original constructor exactly to preserve registration order.
void Parameters::registerParameters() {
    inOut = new InOutStreams;

    //versions
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "versionGenome", &versionGenome));

    //parameters
    parArray.push_back(new ParameterInfoVector <string> (-1, 2, "parametersFiles", &parametersFiles));

    //system
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "sysShell", &sysShell));

    //run
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "runMode", &runModeIn));
    parArray.push_back(new ParameterInfoScalar <int> (-1, -1, "runThreadN", &runThreadN));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "runDirPerm", &runDirPermIn));
    parArray.push_back(new ParameterInfoScalar <int> (-1, -1, "runRNGseed", &runRNGseed));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "legacy", &legacyIn));

    //webui
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "webuiHost",    &webui.host));
    parArray.push_back(new ParameterInfoScalar <int>    (-1, -1, "webuiPort",    &webui.port));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "webuiMetrics", &webui.metricsIn));

    //genome
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "genomeType", &pGe.gTypeString));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "genomeDir", &pGe.gDir));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "genomeLoad", &pGe.gLoad));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "genomeFastaFiles", &pGe.gFastaFiles));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "genomeChainFiles", &pGe.gChainFiles));
    parArray.push_back(new ParameterInfoScalar <uint> (-1, -1, "genomeSAindexNbases", &pGe.gSAindexNbases));
    parArray.push_back(new ParameterInfoScalar <uint> (-1, -1, "genomeChrBinNbits", &pGe.gChrBinNbits));
    parArray.push_back(new ParameterInfoScalar <uint> (-1, -1, "genomeSAsparseD", &pGe.gSAsparseD));
    parArray.push_back(new ParameterInfoScalar <uint> (-1, -1, "genomeSuffixLengthMax", &pGe.gSuffixLengthMax));
    parArray.push_back(new ParameterInfoVector <uint> (-1, -1, "genomeFileSizes", &pGe.gFileSizes));
    //parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "genomeConsensusFile", &pGe.gConsensusFile)); DEPRECATED
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "genomeTransformType", &pGe.transform.typeString));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "genomeTransformVCF", &pGe.transform.vcfFile));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "genomeTransformOutput", &pGe.transform.output));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "genomeChrSetMitochondrial", &pGe.chrSet.mitoStrings));

    //read
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readFilesType", &readFilesType));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readFilesIn", &readFilesIn));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "readFilesPrefix", &readFilesPrefix));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readFilesCommand", &readFilesCommand));

    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "readMatesLengthsIn", &readMatesLengthsIn));
    parArray.push_back(new ParameterInfoScalar <uint> (-1, -1, "readMapNumber", &readMapNumber));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readNameSeparator", &readNameSeparator));
    parArray.push_back(new ParameterInfoScalar <uint32> (-1, -1, "readQualityScoreBase", &readQualityScoreBase));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readFilesManifest", &readFilesManifest));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readFilesSAMattrKeep", &readFiles.samAttrKeepIn));

    //parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "readStrand", &pReads.strandString));


    //input from BAM
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "inputBAMfile", &inputBAMfile));

    //BAM processing
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "bamRemoveDuplicatesType", &removeDuplicates.mode));
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "bamRemoveDuplicatesMate2basesN", &removeDuplicates.mate2basesN));

    //limits
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "limitGenomeGenerateRAM", &limitGenomeGenerateRAM));
    parArray.push_back(new ParameterInfoVector <uint64>   (-1, -1, "limitIObufferSize", &limitIObufferSize));
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "limitOutSAMoneReadBytes", &limitOutSAMoneReadBytes));
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "limitOutSJcollapsed", &limitOutSJcollapsed));
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "limitOutSJoneRead", &limitOutSJoneRead));
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "limitBAMsortRAM", &limitBAMsortRAM));
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "limitSjdbInsertNsj", &limitSjdbInsertNsj));
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "limitNreadsSoft", &limitNreadsSoft));

    //output
    parArray.push_back(new ParameterInfoScalar <string>     (-1, 2, "outFileNamePrefix", &outFileNamePrefix));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, 2, "outTmpDir", &outTmpDir));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, 2, "outTmpKeep", &outTmpKeep));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, 2, "outStd", &outStd));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "outReadsUnmapped", &outReadsUnmapped));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "outQSconversionAdd", &outQSconversionAdd));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "outMultimapperOrder", &outMultimapperOrder.mode));

    //outSAM
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSAMtype", &outSAMtype));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "outSAMmode", &outSAMmode));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "outSAMstrandField", &outSAMstrandField.in));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSAMattributes", &outSAMattributes));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSAMunmapped", &outSAMunmapped.mode));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "outSAMorder", &outSAMorder));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "outSAMprimaryFlag", &outSAMprimaryFlag));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "outSAMreadID", &outSAMreadID));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "outSAMmapqUnique", &outSAMmapqUnique));
    parArray.push_back(new ParameterInfoScalar <uint16>        (-1, -1, "outSAMflagOR", &outSAMflagOR));
    parArray.push_back(new ParameterInfoScalar <uint16>        (-1, -1, "outSAMflagAND", &outSAMflagAND));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSAMattrRGline", &outSAMattrRGline));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSAMheaderHD", &outSAMheaderHD));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSAMheaderPG", &outSAMheaderPG));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "outSAMheaderCommentFile", &outSAMheaderCommentFile));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "outBAMcompression", &outBAMcompression));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "outBAMsortingThreadN", &outBAMsortingThreadN));
    parArray.push_back(new ParameterInfoScalar <uint32>        (-1, -1, "outBAMsortingBinsN", &outBAMsortingBinsN));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSAMfilter", &outSAMfilter.mode));
    parArray.push_back(new ParameterInfoScalar <uint>     (-1, -1, "outSAMmultNmax", &outSAMmultNmax));
    parArray.push_back(new ParameterInfoScalar <uint>     (-1, -1, "outSAMattrIHstart", &outSAMattrIHstart));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "outSAMtlen", &outSAMtlen));

    //outSJ
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSJtype", &outSJ.type));

    //output SJ filtering
    parArray.push_back(new ParameterInfoScalar <string>  (-1, -1, "outSJfilterReads", &outSJfilterReads));
    parArray.push_back(new ParameterInfoVector <int32>   (-1, -1, "outSJfilterCountUniqueMin", &outSJfilterCountUniqueMin));
    parArray.push_back(new ParameterInfoVector <int32>   (-1, -1, "outSJfilterCountTotalMin", &outSJfilterCountTotalMin));
    parArray.push_back(new ParameterInfoVector <int32>   (-1, -1, "outSJfilterOverhangMin", &outSJfilterOverhangMin));
    parArray.push_back(new ParameterInfoVector <int32>   (-1, -1, "outSJfilterDistToOtherSJmin", &outSJfilterDistToOtherSJmin));
    parArray.push_back(new ParameterInfoVector <int32>   (-1, -1, "outSJfilterIntronMaxVsReadN", &outSJfilterIntronMaxVsReadN));

    //output wiggle
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "outWigType", &outWigType));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "outWigStrand", &outWigStrand));
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "outWigReferencesPrefix", &outWigReferencesPrefix));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "outWigNorm", &outWigNorm));

    //output filtering
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "outFilterType", &outFilterType) );

    parArray.push_back(new ParameterInfoScalar <uint>     (-1, -1, "outFilterMultimapNmax", &outFilterMultimapNmax));
    parArray.push_back(new ParameterInfoScalar <intScore> (-1, -1, "outFilterMultimapScoreRange", &outFilterMultimapScoreRange));

    parArray.push_back(new ParameterInfoScalar <intScore> (-1, -1, "outFilterScoreMin", &outFilterScoreMin));
    parArray.push_back(new ParameterInfoScalar <double>   (-1, -1, "outFilterScoreMinOverLread", &outFilterScoreMinOverLread));

    parArray.push_back(new ParameterInfoScalar <uint>     (-1, -1, "outFilterMatchNmin", &outFilterMatchNmin));
    parArray.push_back(new ParameterInfoScalar <double>   (-1, -1, "outFilterMatchNminOverLread", &outFilterMatchNminOverLread));

    parArray.push_back(new ParameterInfoScalar <uint>     (-1, -1, "outFilterMismatchNmax", &outFilterMismatchNmax));
    parArray.push_back(new ParameterInfoScalar <double>   (-1, -1, "outFilterMismatchNoverLmax", &outFilterMismatchNoverLmax));
    parArray.push_back(new ParameterInfoScalar <double>   (-1, -1, "outFilterMismatchNoverReadLmax", &outFilterMismatchNoverReadLmax));
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "outFilterIntronMotifs", &outFilterIntronMotifs));
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "outFilterIntronStrands", &outFilterIntronStrands));

    //clipping
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "clipAdapterType", &pClip.adapterType));
    parArray.push_back(new ParameterInfoVector <uint32>   (-1, -1, "clip5pNbases", &pClip.in[0].N));
    parArray.push_back(new ParameterInfoVector <uint32>   (-1, -1, "clip3pNbases", &pClip.in[1].N));
    parArray.push_back(new ParameterInfoVector <uint32>   (-1, -1, "clip5pAfterAdapterNbases", &pClip.in[0].NafterAd));
    parArray.push_back(new ParameterInfoVector <uint32>   (-1, -1, "clip3pAfterAdapterNbases", &pClip.in[1].NafterAd));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "clip5pAdapterSeq", &pClip.in[0].adSeq));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "clip3pAdapterSeq", &pClip.in[1].adSeq));
    parArray.push_back(new ParameterInfoVector <double> (-1, -1, "clip5pAdapterMMp", &pClip.in[0].adMMp));
    parArray.push_back(new ParameterInfoVector <double> (-1, -1, "clip3pAdapterMMp", &pClip.in[1].adMMp));

    //binning, anchors, windows
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "winBinNbits", &winBinNbits));
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "winAnchorDistNbins", &winAnchorDistNbins));
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "winFlankNbins", &winFlankNbins));
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "winAnchorMultimapNmax", &winAnchorMultimapNmax));
    parArray.push_back(new ParameterInfoScalar <double>   (-1, -1, "winReadCoverageRelativeMin", &winReadCoverageRelativeMin));
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "winReadCoverageBasesMin", &winReadCoverageBasesMin));

    //scoring
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreGap", &scoreGap));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreGapNoncan", &scoreGapNoncan));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreGapGCAG", &scoreGapGCAG));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreGapATAC", &scoreGapATAC));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreStitchSJshift", &scoreStitchSJshift));
    parArray.push_back(new ParameterInfoScalar <double>     (-1, -1, "scoreGenomicLengthLog2scale", &scoreGenomicLengthLog2scale));

    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreDelBase", &scoreDelBase));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreDelOpen", &scoreDelOpen));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreInsOpen", &scoreInsOpen));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreInsBase", &scoreInsBase));

    //alignment
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "seedSearchLmax", &seedSearchLmax));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "seedSearchStartLmax", &seedSearchStartLmax));
    parArray.push_back(new ParameterInfoScalar <double>     (-1, -1, "seedSearchStartLmaxOverLread", &seedSearchStartLmaxOverLread));

    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "seedPerReadNmax", &seedPerReadNmax));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "seedPerWindowNmax", &seedPerWindowNmax));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "seedNoneLociPerWindow", &seedNoneLociPerWindow));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "seedMultimapNmax", &seedMultimapNmax));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "seedSplitMin", &seedSplitMin));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "seedMapMin", &seedMapMin));

    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "alignIntronMin", &alignIntronMin));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "alignIntronMax", &alignIntronMax));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "alignMatesGapMax", &alignMatesGapMax));

    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "alignTranscriptsPerReadNmax", &alignTranscriptsPerReadNmax));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "alignSJoverhangMin", &alignSJoverhangMin));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "alignSJDBoverhangMin", &alignSJDBoverhangMin));
    parArray.push_back(new ParameterInfoVector <int32>      (-1, -1, "alignSJstitchMismatchNmax", &alignSJstitchMismatchNmax));

    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "alignSplicedMateMapLmin", &alignSplicedMateMapLmin));
    parArray.push_back(new ParameterInfoScalar <double>     (-1, -1, "alignSplicedMateMapLminOverLmate", &alignSplicedMateMapLminOverLmate));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "alignWindowsPerReadNmax", &alignWindowsPerReadNmax));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "alignTranscriptsPerWindowNmax", &alignTranscriptsPerWindowNmax));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "alignEndsType", &alignEndsType.in));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "alignSoftClipAtReferenceEnds", &alignSoftClipAtReferenceEnds.in));

    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "alignEndsProtrude", &alignEndsProtrude.in));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "alignInsertionFlush", &alignInsertionFlush.in));

    //peOverlap
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "peOverlapNbasesMin", &peOverlap.NbasesMin));
    parArray.push_back(new ParameterInfoScalar <double>     (-1, -1, "peOverlapMMp", &peOverlap.MMp));

    //chimeric
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "chimSegmentMin", &pCh.segmentMin));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "chimScoreMin", &pCh.scoreMin));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "chimScoreDropMax", &pCh.scoreDropMax));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "chimScoreSeparation", &pCh.scoreSeparation));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "chimScoreJunctionNonGTAG", &pCh.scoreJunctionNonGTAG));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "chimMainSegmentMultNmax", &pCh.mainSegmentMultNmax));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "chimJunctionOverhangMin", &pCh.junctionOverhangMin));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "chimOutType", &pCh.out.type));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "chimFilter", &pCh.filter.stringIn));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "chimSegmentReadGapMax", &pCh.segmentReadGapMax));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "chimMultimapNmax", &pCh.multimapNmax));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "chimMultimapScoreRange", &pCh.multimapScoreRange));
    parArray.push_back(new ParameterInfoScalar <uint>       (-1, -1, "chimNonchimScoreDropMin", &pCh.nonchimScoreDropMin));
    parArray.push_back(new ParameterInfoVector <int>        (-1, -1, "chimOutJunctionFormat", &pCh.outJunctionFormat));

    //sjdb
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "sjdbFileChrStartEnd", &pGe.sjdbFileChrStartEnd));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "sjdbGTFfile", &pGe.sjdbGTFfile));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "sjdbGTFchrPrefix", &pGe.sjdbGTFchrPrefix));

    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "sjdbGTFfeatureExon", &pGe.sjdbGTFfeatureExon));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "sjdbGTFtagExonParentTranscript", &pGe.sjdbGTFtagExonParentTranscript));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "sjdbGTFtagExonParentGene", &pGe.sjdbGTFtagExonParentGene));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "sjdbGTFtagExonParentGeneName", &pGe.sjdbGTFtagExonParentGeneName));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "sjdbGTFtagExonParentGeneType", &pGe.sjdbGTFtagExonParentGeneType));

    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "sjdbOverhang", &pGe.sjdbOverhang));
    pGe.sjdbOverhang_par=parArray.size()-1;
    parArray.push_back(new ParameterInfoScalar <int>    (-1, -1, "sjdbScore", &pGe.sjdbScore));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "sjdbInsertSave", &pGe.sjdbInsertSave));

    //variation
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "varVCFfile", &var.vcfFile));

    //WASP
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "waspOutputMode", &wasp.outputMode));

    //quant
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "quantMode", &quant.mode));
    parArray.push_back(new ParameterInfoScalar <int>     (-1, -1, "quantTranscriptomeBAMcompression", &quant.trSAM.bamCompression));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "quantTranscriptomeSAMoutput", &quant.trSAM.output));

    //2-pass
    parArray.push_back(new ParameterInfoScalar <uint>   (-1, -1, "twopass1readsN", &twoPass.pass1readsN));
    twoPass.pass1readsN_par=parArray.size()-1;
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "twopassMode", &twoPass.mode));

    //solo
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "soloType", &pSolo.typeStr));
    parArray.push_back(new ParameterInfoScalar <uint32>   (-1, -1, "soloCBstart", &pSolo.cbS));
    parArray.push_back(new ParameterInfoScalar <uint32>   (-1, -1, "soloUMIstart", &pSolo.umiS));
    parArray.push_back(new ParameterInfoScalar <uint32>   (-1, -1, "soloCBlen", &pSolo.cbL));
    parArray.push_back(new ParameterInfoScalar <uint32>   (-1, -1, "soloUMIlen", &pSolo.umiL));
    parArray.push_back(new ParameterInfoScalar <uint32>   (-1, -1, "soloBarcodeReadLength", &pSolo.bL));
    parArray.push_back(new ParameterInfoScalar <uint32>   (-1, -1, "soloBarcodeMate", &pSolo.barcodeReadIn));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "soloCBwhitelist", &pSolo.soloCBwhitelist));
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "soloStrand", &pSolo.strandStr));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "soloOutFileNames", &pSolo.outFileNames));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "soloFeatures", &pSolo.featureIn));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "soloUMIdedup", &pSolo.umiDedup.typesIn));
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "soloAdapterSequence",&pSolo.adapterSeq));
    parArray.push_back(new ParameterInfoScalar <uint32>   (-1, -1, "soloAdapterMismatchesNmax", &pSolo.adapterMismatchesNmax));
    parArray.push_back(new ParameterInfoScalar <string>    (-1, -1, "soloCBmatchWLtype", &pSolo.CBmatchWL.type));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "soloCBposition",&pSolo.cbPositionStr));
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "soloUMIposition",&pSolo.umiPositionStr));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "soloCellFilter",&pSolo.cellFilter.type));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "soloUMIfiltering",&pSolo.umiFiltering.type));

    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "soloMultiMappers", &pSolo.multiMap.typesIn));

    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "soloClusterCBfile",&pSolo.clusterCBfile));
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "soloOutFormatFeaturesGeneField3",&pSolo.outFormat.featuresGeneField3));

    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "soloInputSAMattrBarcodeSeq",&pSolo.samAtrrBarcodeSeq));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "soloInputSAMattrBarcodeQual",&pSolo.samAtrrBarcodeQual));
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "soloCellReadStats",&pSolo.readStats.type));
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "soloCBtype",&pSolo.CBtype.typeString));

    parameterInputName.push_back("Default");
    parameterInputName.push_back("Command-Line-Initial");
    parameterInputName.push_back("Command-Line");
    parameterInputName.push_back("genomeParameters.txt");
}
