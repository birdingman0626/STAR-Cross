#include "WebUI.h"
#include "WebUI_internal.h"
#include <cstdint>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <dirent.h>
#  include <sys/stat.h>
#endif

// Recursively list files under dir with matching extensions (relative paths from dir),
// up to maxEntries. Used for Solo.out artifact discovery.
void listDirRecursive(const std::string& base, const std::string& rel,
                      const std::vector<std::string>& exts,
                      std::vector<std::string>& out, int& count, int maxEntries) {
    if (count >= maxEntries) return;
    std::string dir = rel.empty() ? base : base + "/" + rel;
    auto matches = [&](const std::string& name) {
        for (const auto& e : exts)
            if (name.size() >= e.size() && name.compare(name.size()-e.size(), e.size(), e) == 0)
                return true;
        return false;
    };
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if (strcmp(fd.cFileName,".") != 0 && strcmp(fd.cFileName,"..") != 0) {
                std::string child = rel.empty() ? fd.cFileName : rel + "/" + fd.cFileName;
                listDirRecursive(base, child, exts, out, count, maxEntries);
            }
        } else if (matches(fd.cFileName) && count < maxEntries) {
            out.push_back(rel.empty() ? fd.cFileName : rel + "/" + fd.cFileName);
            ++count;
        }
    } while (FindNextFileA(h, &fd) && count < maxEntries);
    FindClose(h);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return;
    struct dirent* de;
    while ((de = readdir(d)) && count < maxEntries) {
        if (strcmp(de->d_name,".") == 0 || strcmp(de->d_name,"..") == 0) continue;
        std::string child = rel.empty() ? de->d_name : rel + "/" + de->d_name;
        std::string full  = base + "/" + child;
        struct stat st;
        if (stat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            listDirRecursive(base, child, exts, out, count, maxEntries);
        } else if (matches(de->d_name) && count < maxEntries) {
            out.push_back(child);
            ++count;
        }
    }
    closedir(d);
#endif
}

// Get file size in bytes, or -1 if not found.
int64_t fileSize(const std::string& p) {
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(p.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return -1;
    FindClose(h);
    ULARGE_INTEGER sz;
    sz.LowPart  = fd.nFileSizeLow;
    sz.HighPart = fd.nFileSizeHigh;
    return (int64_t)sz.QuadPart;
#else
    struct stat st;
    return (stat(p.c_str(), &st) == 0) ? (int64_t)st.st_size : -1;
#endif
}

// Infer artifact type from filename.
std::string artifactType(const std::string& name) {
    auto ends = [&](const char* suf) {
        size_t sl = strlen(suf);
        return name.size() >= sl && name.compare(name.size()-sl, sl, suf) == 0;
    };
    if (ends(".mtx"))   return "mtx";
    if (ends(".tsv"))   return "tsv";
    if (ends(".csv"))   return "csv";
    if (ends(".bam"))   return "bam";
    if (ends(".cram"))  return "cram";
    if (ends(".sam"))   return "sam";
    if (ends(".tab"))   return "tab";
    if (ends(".html"))  return "html";
    if (ends(".stats")) return "stats";
    return "other";
}

// Returns files in dir whose names end with any of the given extensions.
std::vector<std::string> listByExt(const std::string& dir,
                                   const std::vector<std::string>& exts) {
    std::vector<std::string> out;
    auto matches = [&](const std::string& name) {
        for (const auto& e : exts)
            if (name.size() >= e.size() && name.compare(name.size()-e.size(), e.size(), e) == 0)
                return true;
        return false;
    };
#ifdef _WIN32
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA((dir + "\\*").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return out;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            if (matches(fd.cFileName)) out.push_back(joinPath(dir, fd.cFileName));
    } while (FindNextFileA(h, &fd));
    FindClose(h);
#else
    DIR* d = opendir(dir.c_str());
    if (!d) return out;
    struct dirent* de;
    while ((de = readdir(d)))
        if (de->d_type != DT_DIR && matches(de->d_name))
            out.push_back(joinPath(dir, de->d_name));
    closedir(d);
#endif
    return out;
}

// Generate an HTML report for a completed job and set job.reportPath.
void writeReport(Job& job) {
    std::string logFinal = readFileTail(job.outputPrefix + "Log.final.out");
    std::string logOut   = readFileTail(job.outputPrefix + "Log.out", 4096);
    std::string path     = job.outputPrefix + "WebUI_Report.html";
    std::ofstream f(path);
    if (!f) return;

    auto esc = [](const std::string& s) {
        std::string r; r.reserve(s.size());
        for (char c : s) {
            if      (c=='<') r += "&lt;";
            else if (c=='>') r += "&gt;";
            else if (c=='&') r += "&amp;";
            else             r += c;
        }
        return r;
    };

    f << "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
         "<title>STAR Report - Job " << job.id << "</title><style>"
         "body{font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',sans-serif;"
         "background:#f4f5f7;color:#333;margin:0}"
         "header{background:#1a1a2e;color:#fff;padding:14px 24px}"
         "header h1{font-size:1.1rem;font-weight:600;margin:0}"
         ".wrap{max-width:960px;margin:0 auto;padding:24px}"
         ".card{background:#fff;border-radius:6px;padding:20px;margin-bottom:16px;"
         "box-shadow:0 1px 3px rgba(0,0,0,.1)}"
         "h2{font-size:.76rem;font-weight:700;text-transform:uppercase;letter-spacing:.06em;"
         "color:#666;margin:0 0 10px}"
         ".meta{display:flex;flex-wrap:wrap;gap:14px 24px;font-size:.86rem}"
         ".meta span b{color:#333}"
         "pre{background:#1e1e1e;color:#d4d4d4;padding:14px;border-radius:4px;"
         "font-family:Consolas,monospace;font-size:.78rem;white-space:pre-wrap;"
         "overflow-x:auto;margin:0;max-height:500px;overflow-y:auto}"
         "table{width:100%;border-collapse:collapse;font-size:.85rem}"
         "th{text-align:left;padding:7px 10px;border-bottom:2px solid #eee;color:#666;"
         "font-size:.74rem;text-transform:uppercase;font-weight:700}"
         "td{padding:7px 10px;border-bottom:1px solid #f0f0f0}"
         "tr:hover td{background:#fafbff}"
         "</style></head><body>"
         "<header><h1>STAR Run Report</h1></header>"
         "<div class='wrap'>"
         "<div class='card'><h2>Summary</h2><div class='meta'>"
         "<span><b>Job&nbsp;ID:</b> " << job.id << "</span>"
         "<span><b>Mode:</b> " << esc(job.runMode) << "</span>"
         "<span><b>Status:</b> " << esc(job.state) << "</span>"
         "<span><b>Exit&nbsp;code:</b> " << job.exitCode << "</span>"
         "<span><b>Submitted:</b> " << esc(job.submitTime) << "</span>"
         "<span><b>Finished:</b> " << esc(job.endTime) << "</span>"
         "<span><b>Output:</b> " << esc(job.outputPrefix) << "</span>"
         "</div></div>"
         "<div class='card'><h2>Command</h2><pre>" << esc(job.cmdLine) << "</pre></div>";

    // STARsolo summary table: parse Solo.out/Gene/Summary.csv if present
    std::string summaryPath = job.outputPrefix + "Solo.out/Gene/Summary.csv";
    std::ifstream sumF(summaryPath);
    if (sumF) {
        f << "<div class='card'><h2>STARsolo Gene Summary</h2>"
             "<table><thead><tr><th>Metric</th><th>Value</th></tr></thead><tbody>";
        std::string sline;
        while (std::getline(sumF, sline)) {
            if (sline.empty()) continue;
            auto comma = sline.find(',');
            std::string key = (comma != std::string::npos) ? sline.substr(0, comma) : sline;
            std::string val = (comma != std::string::npos) ? sline.substr(comma+1) : "";
            // strip CR
            if (!key.empty() && key.back() == '\r') key.pop_back();
            if (!val.empty() && val.back() == '\r') val.pop_back();
            f << "<tr><td>" << esc(key) << "</td><td>" << esc(val) << "</td></tr>";
        }
        f << "</tbody></table></div>";
    }

    if (!logFinal.empty())
        f << "<div class='card'><h2>Mapping Statistics (Log.final.out)</h2>"
             "<pre>" << esc(logFinal) << "</pre></div>";
    if (!logOut.empty())
        f << "<div class='card'><h2>Run Log tail (Log.out)</h2>"
             "<pre>" << esc(logOut) << "</pre></div>";
    f << "</div></body></html>";
    f.close();
    job.reportPath = path;
}
