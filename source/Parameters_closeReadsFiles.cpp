#include "Parameters.h"
#include "ErrorWarning.h"
#include <fstream>
#include <sys/stat.h>

#ifdef _WIN32
#include "wincompat.h"
#else
#include <signal.h>
#include <sys/wait.h>
#endif

void Parameters::closeReadsFiles() {
    for (uint imate=0; imate<readFilesIn.size(); imate++) {
        const bool reachedEof = inOut->readIn[imate].eof();
        if ( inOut->readIn[imate].is_open() )
            inOut->readIn[imate].close();
        if (readFilesCommandPID[imate]>0) {
#ifndef _WIN32
            // An intentionally bounded consumer may stop a healthy producer.
            // At actual EOF, however, its exit status is part of input validity.
            if (!reachedEof) kill(readFilesCommandPID[imate], SIGKILL);
            int status = 0;
            pid_t waited;
            do { waited = waitpid(readFilesCommandPID[imate], &status, 0); }
            while (waited < 0 && errno == EINTR);
            readFilesCommandPID[imate] = 0;
            if (waited < 0 || (reachedEof && (!WIFEXITED(status) || WEXITSTATUS(status) != 0)))
                exitWithError("EXITING because of fatal INPUT FILE error: readFilesCommand did not complete successfully.\n",
                              std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
#else
            kill(readFilesCommandPID[imate], SIGKILL);
#endif
            readFilesCommandPID[imate] = 0;
        }
    };
};
