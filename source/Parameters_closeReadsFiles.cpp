#include "Parameters.h"
#include "ErrorWarning.h"
#include <fstream>
#include <sys/stat.h>

#ifdef _WIN32
#include "wincompat.h"
#else
#include <signal.h>
#endif

void Parameters::closeReadsFiles() {
    for (uint imate=0; imate<readFilesIn.size(); imate++) {//open readIn files
        if ( inOut->readIn[imate].is_open() )
            inOut->readIn[imate].close();
        if (readFilesCommandPID[imate]>0)
            kill(readFilesCommandPID[imate],SIGKILL);
    };
};