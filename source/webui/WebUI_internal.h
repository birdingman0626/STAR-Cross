#pragma once
#include <cstdint>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <sys/types.h>  // pid_t
#endif

// Job registry entry — shared between WebUI.cpp and WebUI_report.cpp.
struct Job {
    int         id       = 0;
    std::string state;   // queued running succeeded failed cancelled
    std::string runMode;
    std::string cmdLine;
    std::string outputPrefix;
    std::string submitTime;
    std::string startTime;
    std::string endTime;
    int         exitCode       = -1;
    bool        generateReport = false;
    std::string reportPath;
    std::vector<std::string> args;
#ifdef _WIN32
    HANDLE hProcess = INVALID_HANDLE_VALUE;
#else
    pid_t  pid = -1;
#endif
};

// Defined in WebUI.cpp — used by WebUI_report.cpp
std::string readFileTail(const std::string& path, size_t maxBytes = 32768);
bool        pathExists(const std::string& p);
std::string joinPath(const std::string& a, const std::string& b);

// Defined in WebUI_report.cpp — used by WebUI.cpp
void        listDirRecursive(const std::string& base, const std::string& rel,
                             const std::vector<std::string>& exts,
                             std::vector<std::string>& out, int& count, int maxEntries);
int64_t     fileSize(const std::string& p);
std::string artifactType(const std::string& name);
std::vector<std::string> listByExt(const std::string& dir,
                                   const std::vector<std::string>& exts);
void writeReport(Job& job);
