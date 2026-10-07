#include "SoloFeature.h"
#include "streamFuns.h"
#include "ErrorWarning.h"
#include "SoloFeatureTypes.h"
#include "serviceFuns.cpp"
#include <cmath>

void SoloFeature::loadRawMatrix()
{    
    //make directories
    if (P.runModeIn.size()<3) {
        string errOut = "Exiting because of fatal PARAMETER error: --runMode soloCellFiltering should contain paths to count matrix input directorry and output prefix.";
        errOut       += "\nSOLUTION: re-run with --runMode soloCellFiltering </path/to/raw/count/dir/> </path/to/output/prefix>\n";
        exitWithError(errOut, std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
    };
    
    string inputPrefix= P.runModeIn[1] + '/';
    outputPrefix= P.runModeIn[2];
    outputPrefixFiltered= outputPrefix;

    /////////////////////////////////////////////////////////////
    //load counting matrix
    string matrixFileName=inputPrefix+pSolo.outFileNames[3];
    ifstream &matStream=ifstrOpen(matrixFileName, ERROR_OUT, "SOLUTION: check path and permission for the matrix file" + matrixFileName, P);
    std::unique_ptr<ifstream> matrixStreamStorage(&matStream);

    const auto invalidInput = [&](const string &reason) {
        exitWithError("Exiting because of fatal INPUT FILE error: invalid count matrix " + matrixFileName
            + ": " + reason + "\nSOLUTION: supply a canonical coordinate integer/real general matrix with matching axes.\n",
            std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
    };
    string header;
    std::getline(matStream, header);
    if (!header.empty() && header.back()=='\r') header.pop_back();
    if (header!="%%MatrixMarket matrix coordinate integer general" &&
        header!="%%MatrixMarket matrix coordinate real general")
        invalidInput("unsupported or missing MatrixMarket header");
    matStream >> std::ws;
    while (matStream.peek() == '%') {
        matStream.ignore(numeric_limits<streamsize>::max(), '\n');
        matStream >> std::ws;
    }
    uint64 nFeatures=0, nColumns=0, nTot=0;
    if (!(matStream >> nFeatures >> nColumns >> nTot) || nFeatures==0 || nColumns==0 || nTot==0 ||
        nFeatures>numeric_limits<uint32>::max() || nColumns>numeric_limits<uint32>::max())
        invalidInput("invalid dimensions or empty matrix");
    featuresNumber=static_cast<uint32>(nFeatures);
    const uint32 nCB1=static_cast<uint32>(nColumns);
    countMatStride=3; //gene, cell, count. Recording cell at shift=1 is temporary: later will replace cell with count
    // Downstream offsets are uint32; reject before multiplication/allocation.
    if (nTot>numeric_limits<uint32>::max()/countMatStride || nTot>countCellGeneUMI.max_size()/countMatStride)
        invalidInput("entry count exceeds supported index range");
    const auto entriesStart=matStream.tellg();
    matStream.seekg(0, std::ios::end);
    const auto matrixEnd=matStream.tellg();
    if (entriesStart<0 || matrixEnd<entriesStart || static_cast<uint64>(matrixEnd-entriesStart)<nTot*5)
        invalidInput("declared entries cannot fit in the input file");
    matStream.seekg(entriesStart);
    countCellGeneUMI.resize(nTot*countMatStride,0);
    for (uint64 ii=0; ii<nTot; ii++) {
        uint64 gene=0, cell=0;
        double count1=0;
        if (!(matStream >> gene >> cell >> count1) || gene==0 || gene>nFeatures || cell==0 || cell>nColumns ||
            !std::isfinite(count1) || count1<0 || std::round(count1)>numeric_limits<uint32>::max())
            invalidInput("truncated entry, out-of-range coordinate or invalid count");
        countCellGeneUMI[ii*countMatStride]=static_cast<uint32>(gene-1);
        countCellGeneUMI[ii*countMatStride+1]=static_cast<uint32>(cell-1);
        countCellGeneUMI[ii*countMatStride+2]=static_cast<uint32>(std::round(count1));
    }
    matStream >> std::ws;
    if (matStream.peek()!=std::char_traits<char>::eof()) invalidInput("extra entries after declared count");
    qsort((void*) countCellGeneUMI.data(), nTot, countMatStride*sizeof(countCellGeneUMI[0]), funCompareTypeSecondFirst<uint32>);
    for (uint64 ii=1; ii<nTot; ii++) {
        if (countCellGeneUMI[ii*countMatStride]==countCellGeneUMI[(ii-1)*countMatStride] &&
            countCellGeneUMI[ii*countMatStride+1]==countCellGeneUMI[(ii-1)*countMatStride+1])
            invalidInput("duplicate coordinates are unsupported; combine them before filtering");
    }
    
    //count number of detected cell
    nCB=0;
    uint32 ciprev=(uint32) -1;
    for (uint32 ii=0; ii<nTot; ii++) {
        uint32 ci1=countCellGeneUMI[ii*countMatStride+1];
        if (ci1!=ciprev) {//new cell
            ciprev=ci1;
            nCB++;
        };
    };    
    
    indCB.resize(nCB);
    countCellGeneUMIindex.resize(nCB);
    nUMIperCB.resize(nCB,0);
    nGenePerCB.resize(nCB,0);
    nReadPerCB.resize(nCB,0);
    
    uint32 cellIndex=(uint32) -1; // nCB remains a count, not the final index
    ciprev=(uint32) -1;
    for (uint32 ii=0; ii<nTot; ii++) {
        uint32 ci1 = countCellGeneUMI[ii*countMatStride+1];
        if (ci1 != ciprev) {//new cell
            ciprev = ci1;
            cellIndex++;
            indCB[cellIndex] = ci1;
            countCellGeneUMIindex[cellIndex] = ii*countMatStride;
        };
        nGenePerCB[cellIndex]++;
        if (countCellGeneUMI[ii*countMatStride+2]>numeric_limits<uint32>::max()-nUMIperCB[cellIndex])
            invalidInput("per-cell total exceeds supported count range");
        nUMIperCB[cellIndex] += countCellGeneUMI[ii*countMatStride+2];
        countCellGeneUMI[ii*countMatStride+1]=countCellGeneUMI[ii*countMatStride+2];//replace cell with count to keep standard convention about countCellGeneUMI
    };
    
    {//load barcodes
        ifstream &wlstream = ifstrOpen(inputPrefix+pSolo.outFileNames[2], ERROR_OUT, "SOLUTION: check the path and permissions of the barcodes file", P);
        std::unique_ptr<ifstream> whitelistStreamStorage(&wlstream);
        pSolo.cbWLstr.clear();
        string cb;
        while (std::getline(wlstream, cb)) {
            if (cb.empty()) invalidInput("empty barcode row");
            if (pSolo.cbWLstr.size()>=nCB1) invalidInput("barcode axis is longer than matrix columns");
            pSolo.cbWLstr.push_back(cb);
        }
        if (pSolo.cbWLstr.size()!=nCB1) invalidInput("barcode axis is shorter than matrix columns");
    };
    
    {//copy features
        std::ifstream &infeat  = ifstrOpen(inputPrefix + pSolo.outFileNames[1], ERROR_OUT, "SOLUTION: check the path and permissions of the features file", P);
        std::unique_ptr<ifstream> featureInputStorage(&infeat);
        uint64 featureRows=0;
        string line;
        while (std::getline(infeat, line)) {
            if (line.empty()) invalidInput("empty feature row");
            ++featureRows;
        }
        if (featureRows!=nFeatures) invalidInput("feature axis length differs from matrix rows");
        infeat.clear();
        infeat.seekg(0);
        createDirectory(outputPrefixFiltered, P.runDirPerm, "Solo output directory", P);
        std::ofstream &outfeat = ofstrOpen(outputPrefixFiltered + pSolo.outFileNames[1], ERROR_OUT, P);
        std::unique_ptr<ofstream> featureOutputStorage(&outfeat);
        outfeat << infeat.rdbuf();
        outfeat.close();
    };
    
    return;
};
