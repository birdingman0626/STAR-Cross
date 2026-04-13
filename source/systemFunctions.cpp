// system functions
#include <string>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include "wincompat.h"
#include <psapi.h>

std::string linuxProcMemory()
{
    PROCESS_MEMORY_COUNTERS_EX pmc;
    std::string outString;
    if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
        outString += "WorkingSetSize: " + std::to_string(pmc.WorkingSetSize / 1024) + " kB; ";
        outString += "PeakWorkingSetSize: " + std::to_string(pmc.PeakWorkingSetSize / 1024) + " kB; ";
        outString += "PrivateUsage: " + std::to_string(pmc.PrivateUsage / 1024) + " kB; ";
    } else {
        outString = "Could not retrieve memory info";
    }
    outString += '\n';
    return outString;
}
#else

std::string linuxProcMemory()
{
    std::ifstream t("/proc/self/status");
    std::stringstream buffer;
    buffer << t.rdbuf();


    std::string outString;
    while (buffer.good()) {
        std::string str1;
        std::getline(buffer,str1);
        if ( (str1.rfind("VmPeak",0) == 0) ||
             (str1.rfind("VmSize",0) == 0) ||
             (str1.rfind("VmHWM",0) == 0)  ||
             (str1.rfind("VmRSS",0) == 0) ) {
                 outString += str1+"; ";
             };
    };
    outString += '\n';

    return outString;
};

#endif