#include "Parameters.h"
using namespace std;
Parameters::Parameters(InOutStreams& borrowedStreams) : inOut(&borrowedStreams) {
    registerParameters();
}

// All parArray.push_back registrations extracted from Parameters::Parameters().
// Call order matches the original constructor exactly to preserve registration order.
void Parameters::registerParameters() {
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
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "gpuSjdbRemap", &gpuSjdbRemapMode));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "seedSearchBackend", &seed.searchBackend));
    parArray.push_back(new ParameterInfoScalar <uint64> (-1, -1, "seedBatchSize", &seed.batchSize));

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
    parArray.push_back(new ParameterInfoScalar <uint64> (-1, -1, "genomeSAindexNbases", &pGe.gSAindexNbases));
    parArray.push_back(new ParameterInfoScalar <uint64> (-1, -1, "genomeChrBinNbits", &pGe.gChrBinNbits));
    parArray.push_back(new ParameterInfoScalar <uint64> (-1, -1, "genomeSAsparseD", &pGe.gSAsparseD));
    parArray.push_back(new ParameterInfoScalar <uint64> (-1, -1, "genomeSuffixLengthMax", &pGe.gSuffixLengthMax));
    parArray.push_back(new ParameterInfoVector <uint64> (-1, -1, "genomeFileSizes", &pGe.gFileSizes));
    //parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "genomeConsensusFile", &pGe.gConsensusFile)); DEPRECATED
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "genomeTransformType", &pGe.transform.typeString));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "genomeTransformVCF", &pGe.transform.vcfFile));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "genomeTransformOutput", &pGe.transform.output));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "genomeChrSetMitochondrial", &pGe.chrSet.mitoStrings));

    //read
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readFilesType", &readFiles.type));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readFilesIn", &readFiles.in));
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "readFilesPrefix", &readFiles.prefix));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readFilesCommand", &readFiles.command));

    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "readMatesLengthsIn", &readMatesLengthsIn));
    parArray.push_back(new ParameterInfoScalar <uint64> (-1, -1, "readMapNumber", &readMapNumber));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readNameSeparator", &readNameSeparator));
    parArray.push_back(new ParameterInfoScalar <uint32> (-1, -1, "readQualityScoreBase", &readQualityScoreBase));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readFilesManifest", &readFiles.manifest));
    parArray.push_back(new ParameterInfoVector <string> (-1, -1, "readFilesSAMattrKeep", &readFiles.samAttrKeepIn));


    //input from BAM
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "inputBAMfile", &inputBAMfile));

    //BAM processing
    parArray.push_back(new ParameterInfoScalar <string> (-1, -1, "bamRemoveDuplicatesType", &removeDuplicates.mode));
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "bamRemoveDuplicatesMate2basesN", &removeDuplicates.mate2basesN));

    //limits
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "limitGenomeGenerateRAM", &limit.genomeGenerateRAM));
    parArray.push_back(new ParameterInfoVector <uint64>   (-1, -1, "limitIObufferSize", &limit.IObufferSize));
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "limitOutSAMoneReadBytes", &limit.outSAMoneReadBytes));
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "limitOutSJcollapsed", &limit.outSJcollapsed));
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "limitOutSJoneRead", &limit.outSJoneRead));
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "limitBAMsortRAM", &limit.BAMsortRAM));
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "limitSjdbInsertNsj", &limit.sjdbInsertNsj));
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "limitNreadsSoft", &limit.nreadsSoft));

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
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "outSAMmapqUnique", &output.uniqueMapq));
    parArray.push_back(new ParameterInfoScalar <uint16>        (-1, -1, "outSAMflagOR", &outSAMflagOR));
    parArray.push_back(new ParameterInfoScalar <uint16>        (-1, -1, "outSAMflagAND", &outSAMflagAND));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSAMattrRGline", &outSAMattrRGline));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSAMheaderHD", &outSAMheaderHD));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSAMheaderPG", &outSAMheaderPG));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "outSAMheaderCommentFile", &outSAMheaderCommentFile));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "outBAMcompression", &output.bamCompression));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "outBAMsortingThreadN", &outBAMsortingThreadN));
    parArray.push_back(new ParameterInfoScalar <uint32>        (-1, -1, "outBAMsortingBinsN", &outBAMsortingBinsN));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSAMfilter", &outSAMfilter.mode));
    parArray.push_back(new ParameterInfoScalar <uint64>     (-1, -1, "outSAMmultNmax", &output.maxAlignments));
    parArray.push_back(new ParameterInfoScalar <uint64>     (-1, -1, "outSAMattrIHstart", &outSAMattrIHstart));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "outSAMtlen", &outSAMtlen));

    //outSJ
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "outSJtype", &outSJ.type));

    //output SJ filtering
    parArray.push_back(new ParameterInfoScalar <string>  (-1, -1, "outSJfilterReads", &outSJfilter.reads));
    parArray.push_back(new ParameterInfoVector <int32>   (-1, -1, "outSJfilterCountUniqueMin", &outSJfilter.countUniqueMin));
    parArray.push_back(new ParameterInfoVector <int32>   (-1, -1, "outSJfilterCountTotalMin", &outSJfilter.countTotalMin));
    parArray.push_back(new ParameterInfoVector <int32>   (-1, -1, "outSJfilterOverhangMin", &outSJfilter.overhangMin));
    parArray.push_back(new ParameterInfoVector <int32>   (-1, -1, "outSJfilterDistToOtherSJmin", &outSJfilter.distToOtherSJmin));
    parArray.push_back(new ParameterInfoVector <int32>   (-1, -1, "outSJfilterIntronMaxVsReadN", &outSJfilter.intronMaxVsReadN));

    //output wiggle
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "outWigType", &outWigType));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "outWigStrand", &outWigStrand));
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "outWigReferencesPrefix", &outWigReferencesPrefix));
    parArray.push_back(new ParameterInfoVector <string>   (-1, -1, "outWigNorm", &outWigNorm));

    //output filtering
    parArray.push_back(new ParameterInfoScalar <string>   (-1, -1, "outFilterType", &outFilterType) );

    parArray.push_back(new ParameterInfoScalar <uint64>     (-1, -1, "outFilterMultimapNmax", &alignFilter.multimapMax));
    parArray.push_back(new ParameterInfoScalar <intScore> (-1, -1, "outFilterMultimapScoreRange", &alignFilter.multimapScoreRange));

    parArray.push_back(new ParameterInfoScalar <intScore> (-1, -1, "outFilterScoreMin", &alignFilter.scoreMin));
    parArray.push_back(new ParameterInfoScalar <double>   (-1, -1, "outFilterScoreMinOverLread", &alignFilter.scoreOverReadLengthMin));

    parArray.push_back(new ParameterInfoScalar <uint64>     (-1, -1, "outFilterMatchNmin", &alignFilter.matchMin));
    parArray.push_back(new ParameterInfoScalar <double>   (-1, -1, "outFilterMatchNminOverLread", &alignFilter.matchOverReadLengthMin));

    parArray.push_back(new ParameterInfoScalar <uint64>     (-1, -1, "outFilterMismatchNmax", &alignFilter.mismatchMax));
    parArray.push_back(new ParameterInfoScalar <double>   (-1, -1, "outFilterMismatchNoverLmax", &alignFilter.mismatchOverLengthMax));
    parArray.push_back(new ParameterInfoScalar <double>   (-1, -1, "outFilterMismatchNoverReadLmax", &alignFilter.mismatchOverReadLengthMax));
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
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "winBinNbits", &win.binNbits));
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "winAnchorDistNbins", &win.anchorDistNbins));
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "winFlankNbins", &win.flankNbins));
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "winAnchorMultimapNmax", &win.anchorMultimapNmax));
    parArray.push_back(new ParameterInfoScalar <double>   (-1, -1, "winReadCoverageRelativeMin", &win.readCoverageRelativeMin));
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "winReadCoverageBasesMin", &win.readCoverageBasesMin));

    //scoring
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreGap", &score.gap));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreGapNoncan", &score.gapNoncan));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreGapGCAG", &score.gapGCAG));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreGapATAC", &score.gapATAC));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreStitchSJshift", &score.stitchSJshift));
    parArray.push_back(new ParameterInfoScalar <double>     (-1, -1, "scoreGenomicLengthLog2scale", &score.genomicLengthLog2scale));

    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreDelBase", &score.delBase));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreDelOpen", &score.delOpen));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreInsOpen", &score.insOpen));
    parArray.push_back(new ParameterInfoScalar <intScore>   (-1, -1, "scoreInsBase", &score.insBase));

    //alignment
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "seedSearchLmax", &seed.searchLmax));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "seedSearchStartLmax", &seed.searchStartLmax));
    parArray.push_back(new ParameterInfoScalar <double>     (-1, -1, "seedSearchStartLmaxOverLread", &seed.searchStartLmaxOverLread));

    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "seedPerReadNmax", &seed.perReadNmax));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "seedPerWindowNmax", &seed.perWindowNmax));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "seedNoneLociPerWindow", &seed.noneLociPerWindow));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "seedMultimapNmax", &seed.multimapNmax));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "seedSplitMin", &seed.splitMin));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "seedMapMin", &seed.mapMin));

    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "alignIntronMin", &align.intronMin));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "alignIntronMax", &align.intronMax));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "alignMatesGapMax", &align.matesGapMax));

    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "alignTranscriptsPerReadNmax", &align.transcriptsPerReadNmax));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "alignSJoverhangMin", &align.SJoverhangMin));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "alignSJDBoverhangMin", &align.SJDBoverhangMin));
    parArray.push_back(new ParameterInfoVector <int32>      (-1, -1, "alignSJstitchMismatchNmax", &align.SJstitchMismatchNmax));

    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "alignSplicedMateMapLmin", &align.splicedMateMapLmin));
    parArray.push_back(new ParameterInfoScalar <double>     (-1, -1, "alignSplicedMateMapLminOverLmate", &align.splicedMateMapLminOverLmate));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "alignWindowsPerReadNmax", &align.windowsPerReadNmax));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "alignTranscriptsPerWindowNmax", &align.transcriptsPerWindowNmax));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "alignEndsType", &align.endsType.in));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "alignSoftClipAtReferenceEnds", &align.softClipAtReferenceEnds.in));

    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "alignEndsProtrude", &align.endsProtrude.in));
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "alignInsertionFlush", &align.insertionFlush.in));

    //peOverlap
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "peOverlapNbasesMin", &peOverlap.NbasesMin));
    parArray.push_back(new ParameterInfoScalar <double>     (-1, -1, "peOverlapMMp", &peOverlap.MMp));

    //chimeric
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "chimSegmentMin", &pCh.segmentMin));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "chimScoreMin", &pCh.scoreMin));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "chimScoreDropMax", &pCh.scoreDropMax));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "chimScoreSeparation", &pCh.scoreSeparation));
    parArray.push_back(new ParameterInfoScalar <int>        (-1, -1, "chimScoreJunctionNonGTAG", &pCh.scoreJunctionNonGTAG));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "chimMainSegmentMultNmax", &pCh.mainSegmentMultNmax));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "chimJunctionOverhangMin", &pCh.junctionOverhangMin));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "chimOutType", &pCh.out.type));
    parArray.push_back(new ParameterInfoVector <string>     (-1, -1, "chimFilter", &pCh.filter.stringIn));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "chimSegmentReadGapMax", &pCh.segmentReadGapMax));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "chimMultimapNmax", &pCh.multimapNmax));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "chimMultimapScoreRange", &pCh.multimapScoreRange));
    parArray.push_back(new ParameterInfoScalar <uint64>       (-1, -1, "chimNonchimScoreDropMin", &pCh.nonchimScoreDropMin));
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

    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "sjdbOverhang", &pGe.sjdbOverhang));
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
    parArray.push_back(new ParameterInfoScalar <string>     (-1, -1, "quantTranscriptomeBAMprimary", &quant.trSAM.bamPrimary));

    //2-pass
    parArray.push_back(new ParameterInfoScalar <uint64>   (-1, -1, "twopass1readsN", &twoPass.pass1readsN));
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
    parameterStorage = std::make_shared<std::vector<std::unique_ptr<ParameterInfoBase>>>();
    parameterStorage->reserve(parArray.size());
    for (auto* parameter : parArray)
        parameterStorage->emplace_back(parameter);
}
