/*
    functions that handle errors and warnings
*/
#include "ErrorWarning.h"
#include "TimeFunctions.h"
#include "GlobalVariables.h"
using namespace std;
void exitWithError(string messageOut, ostream &streamOut1, ostream &streamOut2, int errorInt, Parameters &P)
{
    if (P.runThreadN>1) 
        pthread_mutex_lock(&g_threadChunks.mutexError);
    time_t timeCurrent;
    time( &timeCurrent);
    if (streamOut1.good()) {
        streamOut1 << "\n" << messageOut << endl << timeMonthDayTime(timeCurrent) <<" ...... FATAL ERROR, exiting\n"  <<flush;
    };
    if (streamOut2.good()) {
        streamOut2 << "\n" << messageOut << endl << timeMonthDayTime(timeCurrent) <<" ...... FATAL ERROR, exiting\n"  <<flush;
    };
    // This can run in a mapping worker or the input dispatcher. Other workers
    // still borrow these streams: deleting them here races their reads/writes
    // and may join the dispatcher from its own thread. Diagnostics above are
    // flushed; process termination reclaims descriptors and memory. Normal
    // successful execution retains its explicit, post-join RAII cleanup.
//     if (P.runThreadN>1) pthread_mutex_unlock(&g_threadChunks.mutexError);
    exit(errorInt);
};

void warningMessage(string messageOut, ostream &streamOut1, ostream &streamOut2, Parameters &P)
{
    if (P.runThreadN>1) 
        pthread_mutex_lock(&g_threadChunks.mutexError);
    if (streamOut1.good()) {
        streamOut1 << "!!!!! WARNING: " << messageOut <<endl;
    };
    if (streamOut2.good()) {
        streamOut2 << "!!!!! WARNING: " << messageOut <<endl;
    };
    if (P.runThreadN>1) 
        pthread_mutex_unlock(&g_threadChunks.mutexError);    
};
