#include "Parameters.h"
#include "ErrorWarning.h"
#include <fstream>
#include <sys/stat.h>

#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include "wincompat.h"
#else
#include <sys/wait.h>
#endif
using namespace std;

namespace {
// Fatal error for a header-reading readFilesCommand that system() reports as failed.
// status/savedErrno are system()'s result and errno right after the call.
void exitHeaderCommandFailed(Parameters &P, const string &command, int status, int savedErrno) {
    ostringstream err;
    err << "EXITING because of fatal INPUT FILE error: readFilesCommand failed while reading the SAM header: " << command;
    if (status == -1) {
        err << ": could not run the command: " << strerror(savedErrno);
#ifndef _WIN32
    } else if (WIFEXITED(status)) {
        err << ": exit code " << WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        err << ": signal " << WTERMSIG(status);
#else
    } else {
        err << ": exit code " << status;
#endif
    };
    err << "\nSOLUTION: check --readFilesCommand and --readFilesIn.\n";
    exitWithError(err.str(), std::cerr, P.inOut->logMain, EXIT_CODE_PARAMETER, P);
};
} // namespace
void Parameters::readSAMheader(const string commandString, const vector<string> fileNames) {

    if (commandString=="") {//simply read from file
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
    for (uint32 ii=0; ii<fileNames.size(); ii++) {
        remove(tmpFile.c_str());
        string com1=commandString + "   " + fileNames.at(ii) + " > \"" + tmpFile + "\"";
        const int status=system(com1.c_str());//synchronous: the command's own exit code
        if (status != 0) exitHeaderCommandFailed(*this, com1, status, errno);
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
    for (uint32 ii=0; ii<fileNames.size(); ii++) {
        string com1=commandString + "   " + fileNames.at(ii) + " > " + tmpFifo + "&";
        // The command runs in the background feeding the FIFO, so system() only reports whether the
        // shell could start it; the command's own failure is caught when the read streams close.
        const int status=system(com1.c_str());
        if (status != 0) exitHeaderCommandFailed(*this, com1, status, errno);
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