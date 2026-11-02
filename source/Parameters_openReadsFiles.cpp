#include "Parameters.h"
#include "ErrorWarning.h"
#include <fstream>
#include <sys/stat.h>

#ifdef _WIN32
#include "wincompat.h"
#else
// macOS 26 SDK Mach headers (reached via <spawn.h>) use the C11 keyword
// _Static_assert, which Apple Clang accepts in C++ but GCC rejects.
#if defined(__APPLE__) && defined(__GNUC__) && !defined(__clang__) && !defined(_Static_assert)
#define _Static_assert static_assert
#include <spawn.h>
#undef _Static_assert
#else
#include <spawn.h>
#endif
#include <unistd.h>
// environ is declared in <unistd.h> on glibc but not on macOS; declare it
// explicitly so posix_spawnp can pass the current environment on every platform.
extern char **environ;
#endif
using namespace std;
// Large read buffer for ifstream to reduce system call overhead
// Persistent: must outlive the ifstream that uses it
static char s_readInBuf[MAX_N_MATES][4 * 1024 * 1024]; // 4MB per mate

void Parameters::openReadsFiles()
{
    if (readFiles.commandString=="") {//read from file
        openReadsFilesPlain();
    } else {//create fifo files, execute pre-processing command
         for (uint64 imate=0;imate<readFiles.names.size();imate++) //open readIn files
            openReadsFilesCommand(imate);
    };
    readFilesIndex=0;

    if (readFiles.typeN==10) {//SAM file - skip header lines
        readSAMheader(readFiles.commandString, readFiles.names.at(0));
    };

};

void Parameters::openReadsFilesPlain()
{
    for (uint64 ii=0;ii<readFiles.in.size();ii++) {//open readIn files
        readFiles.commandPID[ii]=0;//no command process IDs
        if ( inOut->readIn[ii].is_open() ) inOut->readIn[ii].close();

        string rfName=readFiles.prefixFinal + readFiles.in.at(ii);

        inOut->readIn[ii].open(rfName.c_str()); //try to open the Sequences file right away, exit if failed
        inOut->readIn[ii].rdbuf()->pubsetbuf(s_readInBuf[ii], sizeof(s_readInBuf[ii])); // 4MB read buffer
        if (inOut->readIn[ii].fail()) {
            ostringstream errOut;
            errOut <<"EXITING because of fatal input ERROR: could not open readFilesIn=" << rfName <<"\n";
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
    };
};

//run --readFilesCommand for one mate; its output is read through inOut->readIn[imate]
void Parameters::openReadsFilesCommand(uint64 imate)
{
#ifdef _WIN32
    // Windows: use temp files + system() instead of FIFO + vfork/exec
    ostringstream sysCom;
    sysCom << outFileTmp << "tmp.read" << imate+1;
    readFiles.inTmp.push_back(sysCom.str());
    remove(readFiles.inTmp.at(imate).c_str());

    inOut->logMain << "\n   Input read files for mate "<< imate+1 <<" :\n";

    // Match POSIX file-boundary markers and retain every file's output.
    for (uint32 ifile=0; ifile<readFiles.N; ifile++) {
        {//try to open the files - throw an error if a file cannot be opened
            ifstream rftry(readFiles.names[imate][ifile].c_str());
            if (!rftry.good()){
                exitWithError("EXITING: because of fatal INPUT file error: could not open read file: " + \
                               readFiles.names[imate][ifile] + \
                               "\nSOLUTION: check that this file exists and has read permision.\n", \
                               std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
            };
            rftry.close();
        };

        string fullCommand = "(echo FILE " + std::to_string(ifile) + " & "
            + readFiles.commandString + "   \"" + readFiles.names[imate][ifile]
            + "\") " + (ifile == 0 ? ">" : ">>") + " \"" + readFiles.inTmp.at(imate) + "\"";
        inOut->logMain << "\n   readsCommand: " << fullCommand << endl;
        int sysRet = system(fullCommand.c_str());
        if (sysRet != 0) {
            ostringstream errOut;
            errOut << "EXITING: because of fatal EXECUTION error: readFilesCommand failed with exit code " << sysRet << "\n";
            exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
        };
    };
    readFiles.commandPID[imate]=0;

    inOut->readIn[imate].open(readFiles.inTmp.at(imate).c_str());
#else
    // POSIX: use FIFO + vfork/exec
    ostringstream sysCom;
    sysCom << outFileTmp <<"tmp.fifo.read"<<imate+1;
    readFiles.inTmp.push_back(sysCom.str());
    remove(readFiles.inTmp.at(imate).c_str());
    if (mkfifo(readFiles.inTmp.at(imate).c_str(), S_IRUSR | S_IWUSR ) != 0) {
        exitWithError("Exiting because of *FATAL ERROR*: could not create FIFO file " + readFiles.inTmp.at(imate) + "\n"
                    + "SOLUTION: check the if run directory supports FIFO files.\n"
                    + "If run partition does not support FIFO (e.g. Windows partitions FAT, NTFS), "
                    + "re-run on a Linux partition, or point --outTmpDir to a Linux partition.\n"
                    , std::cerr, inOut->logMain, EXIT_CODE_FIFO, *this);
    };

    inOut->logMain << "\n   Input read files for mate "<< imate+1 <<" :\n";

    string readsCommandFileName=outFileTmp+"/readsCommand_read" + to_string(imate+1);
    writeReadsCommandScript(imate, readsCommandFileName);
    spawnReadsCommand(imate, readsCommandFileName);

    inOut->readIn[imate].open(readFiles.inTmp.at(imate).c_str());
#endif
};

#ifndef _WIN32
//shell script that writes all files of one mate, each preceded by a FILE marker, into the FIFO
void Parameters::writeReadsCommandScript(uint64 imate, const string &readsCommandFileName)
{
    fstream readsCommandFile( readsCommandFileName.c_str(), ios::out);
    readsCommandFile.close();
    readsCommandFile.open( readsCommandFileName.c_str(), ios::in | ios::out);
    //first line in the commands file
    if (sysShell!="-") {//executed via specified shell
        readsCommandFile << "#!" <<sysShell <<"\n";
    };
    readsCommandFile << "set -e\n"; // do not conceal an earlier input-command failure
    readsCommandFile << "exec > \""<<readFiles.inTmp.at(imate)<<"\"\n" ; // redirect stdout to temp fifo files

    for (uint32 ifile=0; ifile<readFiles.N; ifile++) {

        if ( system(("ls -lL " + readFiles.names[imate][ifile] + " > "+ outFileTmp+"/readFilesIn.info 2>&1").c_str()) !=0 )
            warningMessage(" Could not ls " + readFiles.names[imate][ifile], std::cerr, inOut->logMain, *this);

        ifstream readFilesIn_info((outFileTmp+"/readFilesIn.info").c_str());
        inOut->logMain <<readFilesIn_info.rdbuf();

        {//try to open the files - throw an error if a file cannot be opened
            ifstream rftry(readFiles.names[imate][ifile].c_str());
            if (!rftry.good()){
                exitWithError("EXITING: because of fatal INPUT file error: could not open read file: " + \
                               readFiles.names[imate][ifile] + \
                               "\nSOLUTION: check that this file exists and has read permision.\n", \
                               std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
            };
            rftry.close();
        };

        readsCommandFile <<"echo FILE "<< ifile << "\n";
        readsCommandFile << readFiles.commandString <<"   "<< ("\""+readFiles.names[imate][ifile]+"\"") <<"\n";
    };

    readsCommandFile.flush();
    readsCommandFile.seekg(0,ios::beg);
    inOut->logMain <<"\n   readsCommandsFile:\n"<<readsCommandFile.rdbuf()<<endl;
    readsCommandFile.close();

    chmod(readsCommandFileName.c_str(),S_IXUSR | S_IRUSR | S_IWUSR);
};

void Parameters::spawnReadsCommand(uint64 imate, const string &readsCommandFileName)
{
    readFiles.commandPID[imate]=0;

    // Run the generated readsCommand script through a shell using
    // posix_spawnp. Invoking it explicitly as "<shell> <script>" means
    // the script does not need a #! shebang line (none is written by
    // default, i.e. when --sysShell is "-") and we do not rely on
    // execvp's ENOEXEC -> /bin/sh fallback, which posix_spawn does not
    // provide. This fixes "Failed spawning readFilesCommand" on macOS
    // (upstream issue #2663). posix_spawnp also avoids the undefined
    // behavior of the previous vfork()+exit(0): calling exit() in a
    // vfork child flushes the parent's stdio buffers and runs atexit
    // handlers in the shared address space, and reported success even
    // when exec failed.
    ostringstream errOut;
    string readsCommandShell = (sysShell!="-" ? sysShell : "/bin/sh");
    char* spawnArgv[] = { const_cast<char*>(readsCommandShell.c_str()),
                          const_cast<char*>(readsCommandFileName.c_str()),
                          (char*) NULL };
    pid_t PID=0;
    int spawnErr = posix_spawnp(&PID, readsCommandShell.c_str(), NULL, NULL, spawnArgv, environ);
    if (spawnErr != 0) {
        errOut << "EXITING: because of fatal EXECUTION error: Failed spawning readFilesCommand: "
               << readsCommandShell << " " << readsCommandFileName << "\n";
        errOut << spawnErr << ": " << strerror(spawnErr) << "\n";
        exitWithError(errOut.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
    };
    readFiles.commandPID[imate]=PID; //record PID of the child
};
#endif
