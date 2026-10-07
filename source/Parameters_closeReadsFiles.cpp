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
    // A manifest can specify fewer ends than the default readFilesIn placeholders.
    for (uint imate=0; imate<readFilesNames.size(); imate++) {
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
            if (waited < 0 || (reachedEof && (!WIFEXITED(status) || WEXITSTATUS(status) != 0))) {
                ostringstream error;
                error << "EXITING because of fatal INPUT FILE error: readFilesCommand did not complete successfully for mate " << imate+1;
                if (waited < 0) error << ": waitpid: " << strerror(errno);
                else if (WIFEXITED(status)) error << ": exit code " << WEXITSTATUS(status);
                else if (WIFSIGNALED(status)) error << ": signal " << WTERMSIG(status);
                error << '\n';
                exitWithError(error.str(), std::cerr, inOut->logMain, EXIT_CODE_PARAMETER, *this);
            }
#else
            kill(readFilesCommandPID[imate], SIGKILL);
#endif
            readFilesCommandPID[imate] = 0;
        }
    };
};
