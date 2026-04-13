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
        if ( inOut->readIn[imate].is_open() )
            inOut->readIn[imate].close();
        if (readFilesCommandPID[imate]>0) {
            kill(readFilesCommandPID[imate], SIGKILL);
#ifndef _WIN32
            int status;
            waitpid(readFilesCommandPID[imate], &status, 0); // reap zombie, avoid race with temp cleanup
#endif
            readFilesCommandPID[imate] = 0;
        }
    };
};