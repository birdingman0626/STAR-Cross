#include "WebUI.h"
#include "WebUI_internal.h"
#include "../IncludeDefine.h" // EXIT_CODE_*
#include <algorithm>
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
using namespace std;
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

std::string exitReason(int exitCode) {
    switch (exitCode) {
        case 0:   return "";
        case EXIT_CODE_BUG:              return "internal error in STAR (please report it)";
        case EXIT_CODE_PARAMETER:        return "invalid parameter";
        case EXIT_CODE_RUNTIME:          return "runtime error";
        case EXIT_CODE_INPUT_FILES:      return "problem with an input file";
        case EXIT_CODE_GENOME_FILES:     return "problem with the genome index";
        case EXIT_CODE_SHM:              return "shared-memory error";
        case EXIT_CODE_GENOME_LOADING_WAITED_TOO_LONG: return "timed out waiting for a shared genome";
        case EXIT_CODE_MEMORY_ALLOCATION: return "not enough memory";
        case EXIT_CODE_FILE_OPEN:        return "could not create an output file";
        case EXIT_CODE_FILE_WRITE:       return "could not write an output file (disk full?)";
        case EXIT_CODE_INCONSISTENT_DATA: return "inconsistent input data";
        case EXIT_CODE_FIFO:             return "error with a named pipe (FIFO)";
        case 127:                        return "STAR could not be started";
        case -1:                         return "STAR could not be started or was stopped by a signal";
        default:                         return "STAR exited with code " + std::to_string(exitCode);
    }
}

std::string fatalMessage(const std::string& outputPrefix, const char* consoleLog) {
    static const std::string marker = "FATAL ERROR, exiting";
    const char* files[] = {"Log.out", consoleLog};
    for (const char* file : files) {
        std::string text = readFileTail(outputPrefix + file, 65536);
        text.erase(std::remove(text.begin(), text.end(), '\r'), text.end()); // Windows logs use CRLF
        const size_t end = text.rfind(marker);
        if (end == std::string::npos) continue;
        // exitWithError writes "\n<message>\n<time> ...... FATAL ERROR, exiting", and messages
        // usually end in '\n' themselves: drop the timestamp line and the line breaks above
        // it, then take the message block back to the blank line that precedes it.
        size_t stop = text.rfind('\n', end);
        if (stop == std::string::npos) continue;
        while (stop > 0 && (text[stop-1] == '\n' || text[stop-1] == '\r')) --stop;
        if (stop == 0) continue;
        size_t start = text.rfind("\n\n", stop - 1);
        start = (start == std::string::npos) ? 0 : start + 2;
        if (start >= stop) continue;
        std::string msg = text.substr(start, stop - start);
        while (!msg.empty() && (msg.front() == '\n' || msg.front() == '\r')) msg.erase(0, 1);
        if (msg.size() > 2000) msg = msg.substr(msg.size() - 2000);
        if (!msg.empty()) return msg;
    }
    return "";
}

// Generate an HTML report for a finished job (succeeded or failed) and set job.reportPath.
void writeReport(Job& job) {
    std::string logFinal = readFileTail(job.outputPrefix + "Log.final.out");
    std::string logOut   = readFileTail(job.outputPrefix + "Log.out", 4096);
    // Windows logs use CRLF, and this text-mode stream would turn "\r\n" into "\r\r\n".
    for (std::string* s : {&logFinal, &logOut})
        s->erase(std::remove(s->begin(), s->end(), '\r'), s->end());
    std::string path     = job.outputPrefix + "WebUI_Report.html";
    std::ofstream f(path);
    if (!f) return;

    auto esc = [](const std::string& s) {
        std::string r; r.reserve(s.size());
        for (char c : s) {
            if      (c=='<') r += "&lt;";
            else if (c=='>') r += "&gt;";
            else if (c=='&') r += "&amp;";
            else if (c=='"') r += "&quot;";
            else             r += c;
        }
        return r;
    };
    auto trim = [](std::string s) {
        const char* ws = " \t\r\n";
        s.erase(0, s.find_first_not_of(ws));
        const size_t e = s.find_last_not_of(ws);
        s.erase(e == std::string::npos ? 0 : e + 1);
        return s;
    };
    auto when = [&](std::string s) { // ISO 8601 "2026-11-05T14:02:58" -> "2026-11-05 14:02:58"
        if (s.size() > 10 && s[10] == 'T') s[10] = ' ';
        return esc(s);
    };
    const bool failed = job.state == "failed";
    const std::string badgeClass = (job.state == "succeeded" || job.state == "failed") ? job.state : std::string("cancelled");

    f << "<!DOCTYPE html><html lang='en'><head><meta charset='UTF-8'>"
         "<meta name='viewport' content='width=device-width,initial-scale=1'>"
         "<meta name='color-scheme' content='light dark'>"
         "<title>Job " << job.id << " report - STAR-Cross</title><style>"
         ":root{--bg:#f5f6f8;--surface:#fff;--surface-2:#f0f2f5;--border:#dde1e7;--text:#1c222b;--muted:#596270;"
         "--accent:#0b6b68;--ok:#1b7339;--ok-bg:#e5f3e9;--err:#b3261e;--err-bg:#fcebea;--neutral:#4a5260;--neutral-bg:#eceef2;"
         "--term-bg:#11161c;--term-text:#d3dae3;"
         "--sans:system-ui,-apple-system,'Segoe UI',Roboto,sans-serif;--mono:ui-monospace,SFMono-Regular,'Cascadia Mono',Menlo,Consolas,monospace}"
         "@media (prefers-color-scheme:dark){:root{--bg:#0e1115;--surface:#161a20;--surface-2:#1d222a;--border:#2a3039;"
         "--text:#e4e8ee;--muted:#9ba5b3;--accent:#52c2bb;--ok:#7fd396;--ok-bg:#132a1b;--err:#ff8f86;--err-bg:#33171a;"
         "--neutral:#b4bcc8;--neutral-bg:#242a33;--term-bg:#0a0d11}}"
         "*{box-sizing:border-box;margin:0}"
         "body{font:14px/1.5 var(--sans);background:var(--bg);color:var(--text)}"
         "a{color:var(--accent)}"
         "header{display:flex;flex-wrap:wrap;align-items:center;gap:10px 14px;padding:12px 24px;background:var(--surface);border-bottom:1px solid var(--border)}"
         "header h1{font-size:1.05rem;font-weight:650}header h1 span{font-weight:500;color:var(--muted)}"
         ".badge{display:inline-flex;align-items:center;gap:6px;padding:2px 9px 2px 8px;border-radius:999px;font-size:.78rem;font-weight:600}"
         ".badge::before{content:'';width:6px;height:6px;border-radius:50%;background:currentColor}"
         ".s-succeeded{background:var(--ok-bg);color:var(--ok)}.s-failed{background:var(--err-bg);color:var(--err)}"
         ".s-cancelled{background:var(--neutral-bg);color:var(--neutral)}"
         ".wrap{max-width:980px;margin:0 auto;padding:20px 24px 40px;display:flex;flex-direction:column;gap:16px}"
         ".card{background:var(--surface);border:1px solid var(--border);border-radius:10px;overflow:hidden}"
         ".card h2{font-size:.95rem;font-weight:650;padding:14px 18px;border-bottom:1px solid var(--border)}"
         ".body{padding:16px 18px}"
         ".alert{border-color:var(--err)}.alert h2{color:var(--err);background:var(--err-bg);border-color:var(--err)}"
         ".reason{font-size:1rem;font-weight:600;margin-bottom:10px}"
         ".note{font-size:.85rem;color:var(--muted);margin-top:10px}"
         "dl{display:grid;grid-template-columns:repeat(auto-fill,minmax(200px,1fr));gap:12px 20px}"
         "dt{font-size:.74rem;text-transform:uppercase;letter-spacing:.03em;color:var(--muted)}"
         "dd{font-variant-numeric:tabular-nums;overflow-wrap:anywhere}dd.mono{font:.82rem var(--mono)}"
         "pre{font:.78rem/1.55 var(--mono);white-space:pre-wrap;word-break:break-word;margin:0}"
         ".code{background:var(--surface-2);border:1px solid var(--border);border-radius:8px;padding:12px 14px}"
         ".term{background:var(--term-bg);color:var(--term-text);padding:14px 16px;max-height:480px;overflow:auto}"
         "table{width:100%;border-collapse:collapse;font-size:.86rem}"
         "td,th{padding:7px 18px;border-bottom:1px solid var(--border);text-align:left;vertical-align:top}"
         "td:last-child{text-align:right;font-variant-numeric:tabular-nums;white-space:nowrap}"
         "th{font-size:.74rem;text-transform:uppercase;letter-spacing:.03em;color:var(--muted);background:var(--surface-2);font-weight:600}"
         "tr:last-child td{border-bottom:0}"
         "@media (max-width:640px){header,.wrap{padding-left:12px;padding-right:12px}td,th{padding-left:14px;padding-right:14px}"
         "td:last-child{white-space:normal}}"
         "</style></head><body>"
         "<header><h1>STAR-Cross <span>job " << job.id << " report</span></h1>"
         "<span class='badge s-" << badgeClass << "'>" << esc(job.state) << "</span></header>"
         "<main class='wrap'>";

    if (failed) {
        f << "<section class='card alert'><h2>Why it stopped</h2><div class='body'>"
             "<p class='reason'>" << esc(job.exitReason) << " (exit code " << job.exitCode << ")</p>";
        if (!job.errorMessage.empty())
            f << "<pre class='code'>" << esc(job.errorMessage) << "</pre>";
        f << "<p class='note'>Outputs of a failed run may be missing or incomplete; do not use them for analysis.</p>"
             "</div></section>";
    }

    f << "<section class='card'><h2>Summary</h2><div class='body'><dl>"
         "<div><dt>Mode</dt><dd>" << esc(job.runMode) << "</dd></div>"
         "<div><dt>Status</dt><dd>" << esc(job.state) << "</dd></div>"
         "<div><dt>Exit code</dt><dd>" << job.exitCode << "</dd></div>"
         "<div><dt>Submitted</dt><dd>" << when(job.submitTime) << "</dd></div>"
         "<div><dt>Finished</dt><dd>" << when(job.endTime) << "</dd></div>"
         "<div><dt>Output</dt><dd class='mono'>" << esc(job.outputPrefix) << "</dd></div>"
         "</dl></div></section>"
         "<section class='card'><h2>Command</h2><div class='body'><pre class='code'>" << esc(job.cmdLine) << "</pre></div></section>";

    // STARsolo summary table: parse Solo.out/Gene/Summary.csv if present
    std::string summaryPath = job.outputPrefix + "Solo.out/Gene/Summary.csv";
    std::ifstream sumF(summaryPath);
    if (sumF) {
        f << "<section class='card'><h2>STARsolo gene summary</h2>"
             "<table><thead><tr><th scope='col'>Metric</th><th scope='col'>Value</th></tr></thead><tbody>";
        std::string sline;
        while (std::getline(sumF, sline)) {
            if (sline.empty()) continue;
            auto comma = sline.find(',');
            std::string key = (comma != std::string::npos) ? sline.substr(0, comma) : sline;
            std::string val = (comma != std::string::npos) ? sline.substr(comma+1) : "";
            f << "<tr><td>" << esc(trim(key)) << "</td><td>" << esc(trim(val)) << "</td></tr>";
        }
        f << "</tbody></table></section>";
    }

    // Log.final.out: "key |\tvalue" lines, with "SECTION:" lines as group headers
    if (!logFinal.empty()) {
        f << "<section class='card'><h2>Mapping statistics</h2><table><tbody>";
        std::istringstream in(logFinal);
        std::string line;
        while (std::getline(in, line)) {
            const size_t bar = line.find('|');
            const std::string text = trim(line);
            if (bar != std::string::npos) {
                f << "<tr><td>" << esc(trim(line.substr(0, bar))) << "</td><td>" << esc(trim(line.substr(bar + 1))) << "</td></tr>";
            } else if (!text.empty()) {
                std::string head = text;
                if (head.back() == ':') head.pop_back();
                f << "<tr><th scope='rowgroup' colspan='2'>" << esc(head) << "</th></tr>";
            }
        }
        f << "</tbody></table></section>";
    }
    if (!logOut.empty())
        f << "<section class='card'><h2>End of Log.out</h2><pre class='term'>" << esc(logOut) << "</pre></section>";
    f << "</main></body></html>";
    f.close();
    job.reportPath = path;
}
