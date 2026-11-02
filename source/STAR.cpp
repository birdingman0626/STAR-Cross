#include <iomanip>
#include <chrono>
#ifdef _WIN32
    #include "wincompat.h"
#endif
#include <sys/types.h>
#include <sys/stat.h>

#include "IncludeDefine.h"
#include "AsyncByteWriter.h"
#include "Parameters.h"
#include "SequenceFuns.h"
#include "Genome.h"
#include "Chain.h"
#include "ReadAlignChunk.h"
#include "ReadAlign.h"
#include "Stats.h"
#include "genomeGenerate.h"
#include "outputSJ.h"
#include "ThreadControl.h"
#include "GlobalVariables.h"
#include "TimeFunctions.h"
#include "ErrorWarning.h"
#include "sysRemoveDir.h"
#include "BAMfunctions.h"
#include "bamSortByCoordinate.h"
#include "cramOutput.h"
#include "Transcriptome.h"
#include "signalFromBAM.h"
#include "mapThreadsSpawn.h"
#include "SjdbClass.h"
#include "sjdbInsertJunctions.h"
#include "Variation.h"
#include "Solo.h"
#include "samHeaders.h"
#include "systemFunctions.h"

#include "twoPassRunPass1.h"

#include "htslib/htslib/sam.h"
#include "parametersDefault.xxd"
#include "webui/WebUI.h"
#include "uninitArray.h"
using namespace std;
void usage(int usageType)
{
    cout << "Usage: STAR  [options]... --genomeDir /path/to/genome/index/   --readFilesIn R1.fq R2.fq\n";
    cout << "Spliced Transcripts Alignment to a Reference (c) Alexander Dobin, 2009-2022\n\n";
    cout << "STAR version=" << STAR_VERSION << "\n";
    cout << "STAR compilation time,server,dir=" << COMPILATION_TIME_PLACE << "\n";
    cout << "For more details see:\n";
    cout << "<https://github.com/alexdobin/STAR>\n";
    cout << "<https://github.com/alexdobin/STAR/blob/master/doc/STARmanual.pdf>\n";

    if (usageType == 0)
    { // brief
        cout << "\nTo list all parameters, run STAR --help\n";
    }
    else if (usageType == 1)
    { // full
        cout.write(reinterpret_cast<char *>(parametersDefault),
                   parametersDefault_len);
    };
    exit(0);
};

namespace {

void logTiming(Parameters &P, const char *label)
{//wall-clock checkpoint in Log.out
    P.inOut->logMain<<"TIMING "<<label<<" t="<<std::fixed<<std::setprecision(3)<<std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count()<<std::defaultfloat<<"\n";
};

void runNonMappingModes(Parameters &P)
{//genomeGenerate, liftOver and webui run here and exit; alignReads and soloCellFiltering return
    if (P.runMode == "alignReads" || P.runMode == "soloCellFiltering")
    {
        // continue
    }
    else if (P.runMode == "genomeGenerate")
    {
        { // normal genome generation
            Genome genomeMain(P, P.pGe);
            genomeMain.genomeGenerate();
        };

        if (P.pGe.transform.type > 0)
        { // generate original genome, in addition to the transfomed generated above
            P.pGe.transform.type = 0;
            P.pGe.transform.typeString = "None";
            P.pGe.transform.vcfFile = "-";
            P.pGe.gDir += "/OriginalGenome/";
            Genome genomeOrig(P, P.pGe);
            genomeOrig.genomeGenerate();
        };

        sysRemoveDir(P.outFileTmp);
        P.inOut->logMain << "DONE: Genome generation, EXITING\n"
                         << flush;
        exit(0);
    }
    else if (P.runMode == "liftOver")
    {
        for (uint64 ii = 0; ii < P.pGe.gChainFiles.size(); ii++)
        {
            Chain chain(P, P.pGe.gChainFiles.at(ii));
            chain.liftOverGTF(P.pGe.sjdbGTFfile, P.outFileNamePrefix + "GTFliftOver_" + to_string(ii + 1) + ".gtf");
            P.inOut->logMain << "DONE: lift-over of GTF file, EXITING\n"
                             << flush;
            exit(0);
        };
    }
    else if (P.runMode == "webui")
    {
        P.webui.metrics = (P.webui.metricsIn != "None");
        WebUI server(P);
        server.run();
        exit(0);
    }
    else
    {
        P.inOut->logMain << "EXITING because of INPUT ERROR: unknown value of input parameter runMode=" << P.runMode << endl
                         << flush;
        exit(1);
    };
};

void initThreadMutexes(Parameters &P)
{
    if (P.runThreadN > 1)
    {
        g_threadChunks.threadArray = makeUninitArray<pthread_t>(P.runThreadN);
        pthread_mutex_init(&g_threadChunks.mutexInRead, NULL);
        pthread_mutex_init(&g_threadChunks.mutexOutSAM, NULL);
        pthread_mutex_init(&g_threadChunks.mutexOutBAM1, NULL);
        pthread_mutex_init(&g_threadChunks.mutexOutChimSAM, NULL);
        pthread_mutex_init(&g_threadChunks.mutexOutChimJunction, NULL);
        pthread_mutex_init(&g_threadChunks.mutexOutUnmappedFastx, NULL);
        pthread_mutex_init(&g_threadChunks.mutexOutFilterBySJout, NULL);
        pthread_mutex_init(&g_threadChunks.mutexStats, NULL);
        pthread_mutex_init(&g_threadChunks.mutexLogMain, NULL);
        pthread_mutex_init(&g_threadChunks.mutexBAMsortBins, NULL);
        pthread_mutex_init(&g_threadChunks.mutexError, NULL);
    };
};

void mapFilterBySJoutStage2(Parameters &P, vector<ReadAlignChunk*> &RAchunk)
{//--outFilterType BySJout: collapse stage-1 junctions, then re-map against them
    P.inOut->logMain << "Completed stage 1 mapping of outFilterBySJout mapping\n"
                     << flush;
    outputSJ(RAchunk.data(), P); // collapse novel junctions
    P.readFilesIndex = -1;

    P.outFilterBySJoutStage = 2;
    if (P.outBAMcoord)
    {
        for (int it = 0; it < P.runThreadN; it++)
        { // prepare the unmapped bin
            RAchunk[it]->chunkOutBAMcoord->coordUnmappedPrepareBySJout();
        };
    };

    mapThreadsSpawn(P, RAchunk.data());
};

template <class Writer>
void closeBgzfOutput(Parameters &P, BGZF *&file, Writer &writer, const char *error)
{//drain the async writer, flush and close; failure is fatal
    if (file == NULL)
        return;
    if (writer) writer->finish();
    const int flushStatus=bgzf_flush(file);
    const int closeStatus=bgzf_close(file);
    file=nullptr;
    if (flushStatus != 0 || closeStatus != 0)
        exitWithError(error, std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
};

void writeSignalAndCram(Parameters &P)
{//wiggle from the sorted BAM, then CRAM transcoding
    if (P.outWigFlags.yes)
    {
        *(P.inOut->logStdOut) << timeMonthDayTime() << " ..... started wiggle output\n"
                              << flush;
        P.inOut->logMain << timeMonthDayTime() << " ..... started wiggle output\n"
                         << flush;
        string wigOutFileNamePrefix = P.outFileNamePrefix + "Signal";
        signalFromBAM(P.outBAMfileCoordName, wigOutFileNamePrefix, P);
    };

    // CRAM output: transcode the finished BAM output(s) to referenceless CRAM.
    // Done after wiggle (which reads the sorted BAM) so the intermediate BAM is
    // still present. Reuses STAR's proven BAM path; only the final container changes.
    if (P.outCRAMbool)
    {
        if (P.outBAMunsorted)
        {
            *(P.inOut->logStdOut) << timeMonthDayTime() << " ..... started CRAM conversion (unsorted)\n" << flush;
            bamToCramReferenceless(P.outBAMfileUnsortedName, P.outCRAMfileUnsortedName, P);
        };
        if (P.outBAMcoord)
        {
            *(P.inOut->logStdOut) << timeMonthDayTime() << " ..... started CRAM conversion (sorted)\n" << flush;
            bamToCramReferenceless(P.outBAMfileCoordName, P.outCRAMfileCoordName, P);
        };
    };
};

void reportFinalStats(Parameters &P)
{//chimeric junction footer, Log.progress.out, Log.final.out
    g_statsAll.writeLines(P.inOut->outChimJunction, P.pCh.outJunctionFormat, "#", STAR_VERSION + string("   ") + P.commandLine);

    g_statsAll.progressReport(P.inOut->logProgress);
    P.inOut->logProgress << "ALL DONE!\n"
                         << flush;
    P.inOut->logFinal.open((P.outFileNamePrefix + "Log.final.out").c_str());
    g_statsAll.reportFinal(P.inOut->logFinal);
    *P.inOut->logStdOut << timeMonthDayTime(g_statsAll.timeFinish) << " ..... finished successfully\n"
                        << flush;

    P.inOut->logMain << "ALL DONE!\n"
                     << flush;
};

void releaseChunkOutputs(vector<ReadAlignChunk*> &RAchunk)
{//finish and drop per-chunk BAM writers before their temporary files are removed
    for (auto* chunk : RAchunk) {
        if (chunk->chunkOutBAMcoord) chunk->chunkOutBAMcoord->finalize();
        chunk->RA->outBAMcoord=chunk->RA->outBAMunsorted=chunk->RA->outBAMquant=nullptr;
        chunk->chunkOutBAMcoord.reset();
        chunk->chunkOutBAMunsorted.reset();
        chunk->chunkOutBAMquant.reset();
    }
};

} // namespace

int main(int argInN, char *argIn[])
try
{
    // If no argument is given, or the first argument is either '-h' or '--help', run usage()
    if (argInN == 1)
    {
        usage(0);
    }
    else if (argInN == 2 && (strcmp("-h", argIn[1]) == 0 || strcmp("--help", argIn[1]) == 0))
    {
        usage(1);
    };

    time(&g_statsAll.timeStart);

    Parameters P; // all parameters
    std::unique_ptr<InOutStreams> streamsOwner(P.inOut);
    P.inputParameters(argInN, argIn);

    *(P.inOut->logStdOut) << "\t" << P.commandLine << '\n';
    *(P.inOut->logStdOut) << "\tSTAR version: " << STAR_VERSION << "   compiled: " << COMPILATION_TIME_PLACE << '\n';
    *(P.inOut->logStdOut) << timeMonthDayTime(g_statsAll.timeStart) << " ..... started STAR run\n"
                          << flush;

    runNonMappingModes(P);

    // transcripome placeholder
    Transcriptome inactiveTranscriptome(P, false);
    Transcriptome *transcriptomeMain = &inactiveTranscriptome;

    // Successful standalone filtering returns normally and releases resources.
    Solo soloCellFilter(P, *transcriptomeMain);
    if (P.runMode == "soloCellFiltering") {
        soloCellFilter.releaseStorage();
        streamsOwner.reset();
        return 0;
    }
    std::unique_ptr<Transcriptome> transcriptomeStorage;

    // genome
    Genome genomeMain(P, P.pGe);
    genomeMain.genomeLoad();

    if (P.pGe.transform.outYes) {
        genomeMain.initializeVariation(false);//no variation for mapGen, only for genOut
        genomeMain.genomeOut.g->initializeVariation(P.var.yes);
    } else {
        genomeMain.initializeVariation(P.var.yes);
    };

    SjdbClass sjdbLoci;

    if (P.sjdbInsert.pass1) {
        Genome genomeMain1(genomeMain, Genome::Snapshot::Borrowed); // insertion-time view; source owns storage
        sjdbInsertJunctions(P, genomeMain, genomeMain1, sjdbLoci);
    };

    initThreadMutexes(P);

    g_statsAll.progressReportHeader(P.inOut->logProgress);

    // 2-pass 1st pass
    twoPassRunPass1(P, genomeMain, transcriptomeMain, sjdbLoci);

    if (P.quant.yes)
    { // load transcriptome
        transcriptomeStorage.reset(new Transcriptome(P));
        transcriptomeMain = transcriptomeStorage.get();
    };

    // initialize Stats
    g_statsAll.resetN();
    time(&g_statsAll.timeStartMap);
    *P.inOut->logStdOut << timeMonthDayTime(g_statsAll.timeStartMap) << " ..... started mapping\n"
                        << flush;
    g_statsAll.timeLastReport = g_statsAll.timeStartMap;

    logTiming(P, "map_begin");
    // SAM headers
    samHeaders(P, *genomeMain.genomeOut.g, *transcriptomeMain);

    // initialize chimeric parameters here - note that chimeric parameters require samHeader
    P.pCh.initialize(&P);

    // prepare chunks and spawn mapping threads
    vector<ReadAlignChunk*> RAchunk(P.runThreadN);
    vector<std::unique_ptr<ReadAlignChunk>> chunkOwners(P.runThreadN);
    for (int ii = 0; ii < P.runThreadN; ii++)
    {
        chunkOwners[ii].reset(new ReadAlignChunk(P, genomeMain, transcriptomeMain, ii));
        RAchunk[ii]=chunkOwners[ii].get();
    };

    logTiming(P, "chunks_built");
    if (P.runRestart.type != 1)
        mapThreadsSpawn(P, RAchunk.data());
    logTiming(P, "spawn_done");

    if (P.outFilterBySJoutStage == 1)
        mapFilterBySJoutStage2(P, RAchunk);

    // Validate input producers before publishing final statistics/counts.
    P.closeReadsFiles();

    closeBgzfOutput(P, P.inOut->outBAMfileUnsorted, P.inOut->unsortedWriter, "EXITING because of fatal BAM output close error");
    closeBgzfOutput(P, P.inOut->outQuantBAMfile, P.inOut->quantWriter, "EXITING because of fatal transcriptome BAM close error");

    if (P.outBAMcoord && P.limit.BAMsortRAM == 0)
    { // make it equal ot the genome size
        P.limit.BAMsortRAM = genomeMain.nGenome + genomeMain.SA.lengthByte + genomeMain.SAi.lengthByte;
    };

    logTiming(P, "map_end");
    time(&g_statsAll.timeFinishMap);
    *P.inOut->logStdOut << timeMonthDayTime(g_statsAll.timeFinishMap) << " ..... finished mapping\n"
                        << flush;
    P.inOut->logMain << timeMonthDayTime(g_statsAll.timeFinishMap) << " ..... finished mapping\n"
                     << "RAM after mapping:\n"
                     << linuxProcMemory() << flush;

    // no need for genome anymore, free the memory
    genomeMain.freeMemory();

    // collapse splice junctions from different threads/chunks, and output them
    if (P.runRestart.type != 1 && P.outSJ.yes)
        outputSJ(RAchunk.data(), P);

    // solo counts
    Solo soloMain(RAchunk.data(), P, RAchunk[0]->chunkTr ? *RAchunk[0]->chunkTr : inactiveTranscriptome);
    soloMain.processAndOutput();

    if (P.quant.geCount.yes)
    { // output gene quantifications
        for (int ichunk = 1; ichunk < P.runThreadN; ichunk++)
        { // sum counts from all chunks into 0th chunk
            RAchunk[0]->chunkTr->quants->addQuants(*(RAchunk[ichunk]->chunkTr->quants));
        };
        RAchunk[0]->chunkTr->quantsOutput();
    };

    if (P.runThreadN > 1 && P.outSAMorder == "PairedKeepInputOrder")
    { // concatenate Aligned.* files
        RAchunk[0]->chunkFilesCat(P.inOut->outSAM, P.outFileTmp + "/Aligned.out.sam.chunk", g_threadChunks.chunkOutN);
    };

    if (P.inOut->samWriter) P.inOut->samWriter->finish();
    bamSortByCoordinate(P, RAchunk.data(), *genomeMain.genomeOut.g, soloMain);

    writeSignalAndCram(P);
    reportFinalStats(P);

    // All mapping, Solo and sorting consumers have finished. Release writers
    // before deleting their temporary files (especially important on Windows).
    releaseChunkOutputs(RAchunk);
    soloMain.releaseStorage(); // aggregate borrowers finish before chunks/temp files
    chunkOwners.clear(); // workers joined; Solo/BAM consumers completed; streams still alive
    std::fill(RAchunk.begin(), RAchunk.end(), nullptr);

    if (P.outTmpKeep == "None")
    {
        sysRemoveDir(P.outFileTmp);
    };
    genomeMain.releaseSharedMemory(); // release while the borrowed log stream is still alive

    streamsOwner.reset(); // to close files

    return 0;
} catch (const std::exception& error) {
    std::cerr << "EXITING because of runtime exception: " << error.what() << std::endl;
    return EXIT_CODE_RUNTIME;
}
