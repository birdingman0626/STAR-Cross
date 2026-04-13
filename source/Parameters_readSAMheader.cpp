#include "Parameters.h"
#include "ErrorWarning.h"
#include <fstream>
#include <sys/stat.h>

#ifdef _WIN32
#include "wincompat.h"
#endif

void Parameters::readSAMheader(const string readFilesCommandString, const vector<string> readFilesNames) {

    if (readFilesCommandString=="") {//simply read from file
        while (inOut->readIn[0].peek()=='@') {
            string str1;
            getline(inOut->readIn[0],str1);
            if (str1.substr(1,2)!="HD" && str1.substr(1,2)!="SQ") {
                samHeaderExtra += str1 + '\n';
            };
        };
        return;
    };

#ifdef _WIN32
    // Windows: use temp file instead of FIFO
    string tmpFile=outFileTmp+"tmp.header";
    ifstream tmpFileIn;
    for (uint32 ii=0; ii<readFilesNames.size(); ii++) {
        remove(tmpFile.c_str());
        string com1=readFilesCommandString + "   " + readFilesNames.at(ii) + " > \"" + tmpFile + "\"";
        system(com1.c_str());
        tmpFileIn.open(tmpFile);
        while (tmpFileIn.good() && tmpFileIn.peek()=='@') {
            string str1;
            getline(tmpFileIn,str1);
            if (str1.substr(1,2)!="HD" && str1.substr(1,2)!="SQ" && (!twoPass.pass2) ) {
                samHeaderExtra += str1 + '\n';
            };
        };
        tmpFileIn.close();
    };
    remove(tmpFile.c_str());
#else
    string tmpFifo=outFileTmp+"tmp.fifo.header";
    remove(tmpFifo.c_str());
    if (mkfifo(tmpFifo.c_str(), S_IRUSR | S_IWUSR ) != 0) {
        exitWithError("Exiting because of *FATAL ERROR*: could not create FIFO file " + tmpFifo + "\n"
                      + "SOLUTION: check the if run directory supports FIFO files.\n"
                      + "If run partition does not support FIFO (e.g. Windows partitions FAT, NTFS), "
                      + "re-run on a Linux partition, or point --outTmpDir to a Linux partition.\n"
                      , std::cerr, inOut->logMain, EXIT_CODE_FIFO, *this);
    };

    ifstream tmpFifoIn;
    for (uint32 ii=0; ii<readFilesNames.size(); ii++) {
        string com1=readFilesCommandString + "   " + readFilesNames.at(ii) + " > " + tmpFifo + "&";
        system(com1.c_str());
        tmpFifoIn.open(tmpFifo);
        while (tmpFifoIn.peek()=='@') {
            string str1;
            getline(tmpFifoIn,str1);
            if (str1.substr(1,2)!="HD" && str1.substr(1,2)!="SQ" && (!twoPass.pass2) ) {
                //SQ and HD header lines cannot be imported from uSAM; do not record the header again in the 2nd pass
                samHeaderExtra += str1 + '\n';
            };
        };
        tmpFifoIn.close();
    };
#endif
};