// CPPHTTPLIB_IMPLEMENTATION must be defined in exactly one TU that includes httplib.h.
#define CPPHTTPLIB_IMPLEMENTATION
#include "httplib.h"
#include <nlohmann/json.hpp>
#include "WebUI.h"
#include "WebUI_internal.h"
#include "../IncludeDefine.h"
#include <atomic>
#include <chrono>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#  include <windows.h>
#else
#  include <csignal>
#  include <dirent.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  ifdef __linux__
#    include <linux/limits.h>
#  else
#    include <limits.h>
#  endif
#endif

using json = nlohmann::json;

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// Job registry
// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

// Job struct is defined in WebUI_internal.h (shared with WebUI_report.cpp)

static std::map<int, Job> g_jobs;
static std::mutex         g_mu;
static std::atomic<int>   g_nextId{1};
static std::string        g_persistPath;   // path to .webui_jobs.jsonl (set in run())
static std::chrono::steady_clock::time_point g_startTime = std::chrono::steady_clock::now();

// Bounded queue limits: 1 running + up to MAX_QUEUED waiting
static constexpr int MAX_QUEUED = 9;

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// Helpers
// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

static std::string nowISO() {
    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    struct tm tmBuf{};
#ifdef _WIN32
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char buf[24];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tmBuf);
    return buf;
}

static std::string starExePath() {
#ifdef _WIN32
    char buf[MAX_PATH] = {};
    GetModuleFileNameA(nullptr, buf, MAX_PATH);
    return buf;
#else
    char buf[PATH_MAX] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; return buf; }
    return "STAR";
#endif
}

static std::string qp(const std::string& s) {
    return (s.find(' ') != std::string::npos) ? '"' + s + '"' : s;
}

std::string readFileTail(const std::string& path, size_t maxBytes) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return "";
    std::streamsize sz = f.tellg();
    size_t start = (sz > (std::streamsize)maxBytes) ? (size_t)(sz - maxBytes) : 0;
    f.seekg((std::streamoff)start);
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool pathExists(const std::string& p) {
#ifdef _WIN32
    return GetFileAttributesA(p.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0;
#endif
}

static bool isDirectory(const std::string& p) {
#ifdef _WIN32
    DWORD a = GetFileAttributesA(p.c_str());
    return (a != INVALID_FILE_ATTRIBUTES) && (a & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat st;
    return stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
#endif
}

std::string joinPath(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    char last = a.back();
    return (last == '/' || last == '\\') ? a + b : a + "/" + b;
}

// Detect path traversal: reject if the normalized path contains a ".." component.
static bool hasPathTraversal(const std::string& p) {
    // Check for /../ or leading ../ or trailing /..
    std::string n = p;
    // Normalize backslashes for uniform check
    for (char& c : n) if (c == '\\') c = '/';
    // Look for /../ sequences or boundary cases
    if (n.find("/../") != std::string::npos) return true;
    if (n.size() >= 3 && n.substr(0, 3) == "../") return true;
    if (n.size() >= 3 && n.substr(n.size()-3) == "/..") return true;
    if (n == "..") return true;
    return false;
}

// Append a terminal job record to the persistence file (call with g_mu held).
static void persistJob(const Job& j) {
    if (g_persistPath.empty()) return;
    std::ofstream f(g_persistPath, std::ios::app);
    if (!f) return;
    json rec = {
        {"id",           j.id},
        {"state",        j.state},
        {"runMode",      j.runMode},
        {"cmdLine",      j.cmdLine},
        {"outputPrefix", j.outputPrefix},
        {"submitTime",   j.submitTime},
        {"startTime",    j.startTime},
        {"endTime",      j.endTime},
        {"exitCode",     j.exitCode},
        {"reportPath",   j.reportPath},
        {"generateReport", j.generateReport},
    };
    f << rec.dump() << "\n";
}

// Load historical jobs from the persistence file into g_jobs.
static void loadPersistedJobs(const std::string& path) {
    std::ifstream f(path);
    if (!f) return;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        try {
            json rec = json::parse(line);
            std::string prefix = rec.value("outputPrefix", "");
            // Only restore if the output directory still exists
            if (!prefix.empty() && !pathExists(prefix)) continue;
            Job j;
            j.id           = rec.value("id", 0);
            j.state        = rec.value("state", "failed");
            j.runMode      = rec.value("runMode", "");
            j.cmdLine      = rec.value("cmdLine", "");
            j.outputPrefix = prefix;
            j.submitTime   = rec.value("submitTime", "");
            j.startTime    = rec.value("startTime", "");
            j.endTime      = rec.value("endTime", "");
            j.exitCode     = rec.value("exitCode", -1);
            j.reportPath   = rec.value("reportPath", "");
            j.generateReport = rec.value("generateReport", false);
            if (j.id <= 0) continue;
            g_jobs[j.id] = std::move(j);
            // Advance the ID counter past restored IDs
            int expected = j.id;
            int next = expected + 1;
            // CAS loop to update g_nextId
            int cur = g_nextId.load();
            while (cur <= expected) {
                if (g_nextId.compare_exchange_weak(cur, next)) break;
            }
        } catch (...) { /* skip malformed lines */ }
    }
}

static int spawnJob(Job& job, const std::string& exe); // defined below

static void pollRunning() {
    bool anyFinished = false;
    for (auto& [id, job] : g_jobs) {
        if (job.state != "running") continue;
#ifdef _WIN32
        if (job.hProcess == INVALID_HANDLE_VALUE) continue;
        DWORD rc = WaitForSingleObject(job.hProcess, 0);
        if (rc == WAIT_OBJECT_0) {
            DWORD ec = 0;
            GetExitCodeProcess(job.hProcess, &ec);
            CloseHandle(job.hProcess);
            job.hProcess  = INVALID_HANDLE_VALUE;
            job.exitCode  = (int)ec;
            job.state     = (ec == 0) ? "succeeded" : "failed";
            job.endTime   = nowISO();
            if (ec == 0 && job.generateReport) writeReport(job);
            persistJob(job);
            anyFinished = true;
        }
#else
        int status = 0;
        if (waitpid(job.pid, &status, WNOHANG) == job.pid) {
            job.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            job.state    = (job.exitCode == 0) ? "succeeded" : "failed";
            job.endTime  = nowISO();
            if (job.exitCode == 0 && job.generateReport) writeReport(job);
            persistJob(job);
            anyFinished = true;
        }
#endif
    }
    // Promote a queued job if a slot opened up
    if (anyFinished) {
        int running = 0;
        for (const auto& [id, j] : g_jobs)
            if (j.state == "running") ++running;
        if (running == 0) {
            // Find the oldest queued job (lowest id)
            int oldestId = -1;
            for (const auto& [id, j] : g_jobs)
                if (j.state == "queued" && (oldestId < 0 || id < oldestId))
                    oldestId = id;
            if (oldestId >= 0) {
                Job& qj = g_jobs[oldestId];
                qj.state     = "running";
                qj.startTime = nowISO();
                spawnJob(qj, starExePath());
                qj.args.clear();
            }
        }
    }
}

static json jobToJson(const Job& j) {
    return {{"id", j.id}, {"state", j.state}, {"runMode", j.runMode},
            {"cmdLine", j.cmdLine}, {"outputPrefix", j.outputPrefix},
            {"submitTime", j.submitTime}, {"startTime", j.startTime},
            {"endTime", j.endTime}, {"exitCode", j.exitCode},
            {"reportPath", j.reportPath}};
}

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// Argument builder
// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

static std::pair<std::vector<std::string>, std::string>
buildArgs(const json& b, std::string& err) {
    auto str = [&](const char* k, const std::string& def = "") -> std::string {
        if (b.contains(k) && b[k].is_string()) return b[k].get<std::string>();
        return def;
    };

    std::string runMode = str("runMode");
    if (runMode.empty()) { err = "Missing runMode"; return {}; }

    std::string outPrefix = str("outFileNamePrefix");
    if (outPrefix.empty()) { err = "Missing outFileNamePrefix"; return {}; }
    if (outPrefix.back() != '/' && outPrefix.back() != '\\') outPrefix += '/';

    std::string genomeDir = str("genomeDir");
    if (genomeDir.empty() && runMode != "soloCellFiltering") {
        err = "Missing genomeDir"; return {};
    }

    // Path traversal check
    if (hasPathTraversal(outPrefix) || hasPathTraversal(genomeDir)) {
        err = "Path traversal rejected"; return {};
    }
    if (b.contains("readFilesIn") && b["readFilesIn"].is_array()) {
        for (const auto& rfi : b["readFilesIn"]) {
            if (rfi.is_string() && hasPathTraversal(rfi.get<std::string>())) {
                err = "Path traversal rejected"; return {};
            }
        }
    }

    std::vector<std::string> args;
    auto add = [&](auto&&... vs) { (args.push_back(std::string(vs)), ...); };

    add("--runMode", runMode);
    add("--outFileNamePrefix", outPrefix);
    if (!genomeDir.empty()) add("--genomeDir", genomeDir);

    // readFilesIn
    if (b.contains("readFilesIn") && b["readFilesIn"].is_array()) {
        const auto& rfi = b["readFilesIn"];
        if (!rfi.empty()) {
            add("--readFilesIn");
            for (const auto& f : rfi) args.push_back(f.get<std::string>());
        }
    } else if (runMode == "alignReads") {
        err = "Missing readFilesIn"; return {};
    }

    // Scalar string params
    for (const char* k : {"readFilesCommand", "sjdbGTFfile",
                           "soloType", "soloCBwhitelist", "soloMultiMappers",
                           "clipAdapterType", "soloStrand", "soloUMIdedup",
                           "twopassMode", "outSAMstrandField"}) {
        std::string v = str(k);
        // skip defaults to keep command lines clean
        if (v.empty() || v == "None" || v == "Forward" || v == "1MM_All") continue;
        add(std::string("--") + k, v);
    }

    // Integer params
    for (const char* k : {"runThreadN", "soloCBlen", "soloUMIlen", "soloCBstart",
                           "soloUMIstart", "soloBarcodeReadLength", "genomeSAindexNbases",
                           "sjdbOverhang", "outFilterMultimapNmax"}) {
        if (b.contains(k) && b[k].is_number_integer())
            add(std::string("--") + k, std::to_string(b[k].get<int>()));
    }
    // limitBAMsortRAM: int64, only send if > 0
    if (b.contains("limitBAMsortRAM") && b["limitBAMsortRAM"].is_number()) {
        int64_t v = b["limitBAMsortRAM"].get<int64_t>();
        if (v > 0) add("--limitBAMsortRAM", std::to_string(v));
    }

    // Space-separated multi-token params
    for (const char* k : {"outSAMtype", "soloCellFilter", "quantMode"}) {
        std::string v = str(k);
        if (v.empty()) continue;
        add(std::string("--") + k);
        std::istringstream iss(v);
        std::string tok;
        while (iss >> tok) args.push_back(tok);
    }

    // Array params
    for (const char* k : {"soloFeatures", "genomeFastaFiles"}) {
        if (b.contains(k) && b[k].is_array()) {
            const auto& arr = b[k];
            if (!arr.empty()) {
                add(std::string("--") + k);
                for (const auto& f : arr) args.push_back(f.get<std::string>());
            }
        }
    }

    // Extra verbatim args (e.g. --legacy)
    if (b.contains("extraArgs") && b["extraArgs"].is_string()) {
        const std::string extra = b["extraArgs"].get<std::string>();
        std::istringstream iss(extra);
        std::string tok;
        while (iss >> tok) args.push_back(tok);
    }

    return {args, outPrefix};
}

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// Process spawning
// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

static int spawnJob(Job& job, const std::string& exe) {
    std::string cmd = qp(exe);
    for (const auto& a : job.args) cmd += ' ' + qp(a);
    job.cmdLine = cmd;

#ifdef _WIN32
    STARTUPINFOA si{};
    PROCESS_INFORMATION pi{};
    si.cb = sizeof(si);
    std::string buf = cmd;
    if (!CreateProcessA(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi))
        return (int)GetLastError();
    CloseHandle(pi.hThread);
    job.hProcess = pi.hProcess;
    return 0;
#else
    pid_t pid = fork();
    if (pid < 0) return errno;
    if (pid == 0) {
        std::vector<const char*> argv;
        argv.push_back(exe.c_str());
        for (const auto& a : job.args) argv.push_back(a.c_str());
        argv.push_back(nullptr);
        execv(exe.c_str(), const_cast<char* const*>(argv.data()));
        _exit(127);
    }
    job.pid = pid;
    return 0;
#endif
}

// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// Embedded HTML UI  (content in WebUI_html.inc)
// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

static const char* g_html =
#include "WebUI_html.inc"
;


// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
// WebUI::run
// 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

WebUI::WebUI(Parameters& P) : P(P) {}

void WebUI::run() {
    httplib::Server srv;
    const std::string exe = starExePath();

    // Phase 4.4: Set up persistence and restore historical jobs
    {
        std::string outDir = P.outFileNamePrefix;
        if (!outDir.empty() && outDir.back() != '/' && outDir.back() != '\\') outDir += '/';
        g_persistPath = outDir + ".webui_jobs.jsonl";
        std::lock_guard<std::mutex> lk(g_mu);
        loadPersistedJobs(g_persistPath);
    }

    srv.Get("/",  [](const httplib::Request&, httplib::Response& res) {
        res.set_content(g_html, "text/html; charset=utf-8");
    });
    srv.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content(json{{"status","ok"}}.dump(), "application/json");
    });
    srv.Get("/props", [this](const httplib::Request&, httplib::Response& res) {
        json body = {
            {"version",      STAR_VERSION},
            {"build",        COMPILATION_TIME_PLACE},
            {"git",          GIT_BRANCH_COMMIT_DIFF},
#ifdef _WIN32
            {"platform",     "windows"},
#elif defined(__APPLE__)
            {"platform",     "macos"},
#else
            {"platform",     "linux"},
#endif
            {"runModes",     json::array({"alignReads","genomeGenerate","soloCellFiltering","webui"})},
            {"webuiVersion", 4},
            {"cpuCount",     (int)std::thread::hardware_concurrency()},
            {"outDir",       P.outFileNamePrefix},
        };
        res.set_content(body.dump(2), "application/json");
    });
    if (P.webui.metrics) {
        srv.Get("/metrics", [](const httplib::Request&, httplib::Response& res) {
            std::lock_guard<std::mutex> lk(g_mu);
            pollRunning();
            int nQueued=0, nRunning=0, nSucceeded=0, nFailed=0, nCancelled=0;
            for (const auto& [id, j] : g_jobs) {
                if      (j.state == "queued")    ++nQueued;
                else if (j.state == "running")   ++nRunning;
                else if (j.state == "succeeded") ++nSucceeded;
                else if (j.state == "failed")    ++nFailed;
                else if (j.state == "cancelled") ++nCancelled;
            }
            auto upSec = std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::steady_clock::now() - g_startTime).count();
            std::ostringstream ss;
            ss << "# HELP star_jobs_total Number of STAR WebUI jobs by state\n"
               << "# TYPE star_jobs_total gauge\n"
               << "star_jobs_total{state=\"queued\"} "    << nQueued    << "\n"
               << "star_jobs_total{state=\"running\"} "   << nRunning   << "\n"
               << "star_jobs_total{state=\"succeeded\"} " << nSucceeded << "\n"
               << "star_jobs_total{state=\"failed\"} "    << nFailed    << "\n"
               << "star_jobs_total{state=\"cancelled\"} " << nCancelled << "\n"
               << "# HELP star_server_uptime_seconds Seconds since server start\n"
               << "# TYPE star_server_uptime_seconds counter\n"
               << "star_server_uptime_seconds " << upSec << "\n";
            res.set_content(ss.str(), "text/plain; version=0.0.4; charset=utf-8");
        });
    }

    // Probe a directory: is it a STAR index, a CellRanger ref, or something else?
    srv.Get("/genome/probe", [](const httplib::Request& req, httplib::Response& res) {
        std::string dir = req.get_param_value("dir");
        auto j = [&](json r){ res.set_content(r.dump(), "application/json"); };

        if (dir.empty()) { j({{"type","empty"}}); return; }
        // Trim trailing separators for consistent probing
        while (!dir.empty() && (dir.back()=='/' || dir.back()=='\\')) dir.pop_back();

        // Valid STAR index: contains SA + genomeParameters.txt
        if (pathExists(joinPath(dir,"SA")) && pathExists(joinPath(dir,"genomeParameters.txt"))) {
            j({{"type","star_index"}}); return;
        }

        // CellRanger reference: has star/SA subdir (index) + genes/genes.gtf (annotation)
        if (pathExists(joinPath(dir,"star/SA"))) {
            json r{{"type","cellranger_ref"},
                   {"starDir", joinPath(dir,"star")}};
            std::string gtf = joinPath(dir,"genes/genes.gtf");
            if (pathExists(gtf)) r["gtfFile"] = gtf;
            j(r); return;
        }

        if (!isDirectory(dir)) { j({{"type","not_found"}}); return; }

        // Scan for FASTA and GTF source files the user might want for genomeGenerate
        auto fastas = listByExt(dir, {".fa",".fasta",".fa.gz",".fasta.gz"});
        auto gtfs   = listByExt(dir, {".gtf",".gff",".gff3"});
        j({{"type","has_source"},
           {"fastaFiles", fastas},
           {"gtfFiles",   gtfs}});
    });

    // Scan a parent directory and return all valid STAR indexes / CellRanger refs inside it.
    srv.Get("/genome/scan", [](const httplib::Request& req, httplib::Response& res) {
        std::string base = req.get_param_value("base");
        while (!base.empty() && (base.back()=='/' || base.back()=='\\')) base.pop_back();
        if (!isDirectory(base)) {
            res.set_content(json{{"error","Not a directory"}}.dump(), "application/json");
            return;
        }
        json results = json::array();
        auto probe = [&](const std::string& name, const std::string& sub) {
            if (pathExists(joinPath(sub,"SA")) && pathExists(joinPath(sub,"genomeParameters.txt"))) {
                results.push_back({{"name",name},{"path",sub},{"type","star_index"}});
            } else if (pathExists(joinPath(sub,"star/SA"))) {
                json e = {{"name",name},{"path",sub},{"type","cellranger_ref"},
                          {"starDir",joinPath(sub,"star")}};
                std::string gtf = joinPath(sub,"genes/genes.gtf");
                if (pathExists(gtf)) e["gtfFile"] = gtf;
                results.push_back(e);
            }
        };
#ifdef _WIN32
        WIN32_FIND_DATAA fd;
        HANDLE h = FindFirstFileA((base + "\\*").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                    strcmp(fd.cFileName,".") != 0 && strcmp(fd.cFileName,"..") != 0)
                    probe(fd.cFileName, joinPath(base, fd.cFileName));
            } while (FindNextFileA(h, &fd));
            FindClose(h);
        }
#else
        DIR* dir = opendir(base.c_str());
        if (dir) {
            struct dirent* de;
            while ((de = readdir(dir)))
                if (de->d_type == DT_DIR && strcmp(de->d_name,".") != 0 && strcmp(de->d_name,"..") != 0)
                    probe(de->d_name, joinPath(base, de->d_name));
            closedir(dir);
        }
#endif
        res.set_content(results.dump(2), "application/json");
    });

    srv.Post("/jobs", [&exe](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content(json{{"error","Invalid JSON"}}.dump(), "application/json");
            return;
        }
        std::string err;
        auto [args, outPrefix] = buildArgs(body, err);
        if (!err.empty()) {
            res.status = 400;
            res.set_content(json{{"error",err}}.dump(), "application/json");
            return;
        }
        Job job;
        job.id             = g_nextId.fetch_add(1);
        job.runMode        = body.value("runMode","");
        job.outputPrefix   = outPrefix;
        job.submitTime     = nowISO();
        job.generateReport = body.value("generateReport", true);
        job.args           = std::move(args);
        {
            std::lock_guard<std::mutex> lk(g_mu);
            // Count running and queued jobs
            int running = 0, queued = 0;
            for (const auto& [jid, jj] : g_jobs) {
                if (jj.state == "running") ++running;
                else if (jj.state == "queued") ++queued;
            }
            if (running >= 1 && queued >= MAX_QUEUED) {
                res.status = 429;
                res.set_content(json{{"error","Job queue full"}}.dump(), "application/json");
                return;
            }
            if (running >= 1) {
                // Queue the job instead of spawning immediately
                job.state = "queued";
                int jobId = job.id;
                g_jobs[jobId] = std::move(job);
                res.status = 201;
                res.set_content(jobToJson(g_jobs[jobId]).dump(2), "application/json");
                return;
            }
            // Spawn immediately
            job.state     = "running";
            job.startTime = job.submitTime;
            int rc = spawnJob(job, exe);
            if (rc != 0) {
                res.status = 500;
                res.set_content(json{{"error","Failed to spawn process (code "+std::to_string(rc)+")"}}.dump(), "application/json");
                return;
            }
            int jobId = job.id;
            job.args.clear();  // args consumed by spawnJob; free memory
            g_jobs[jobId] = std::move(job);
            res.status = 201;
            res.set_content(jobToJson(g_jobs[jobId]).dump(2), "application/json");
        }
    });

    srv.Get("/jobs", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mu);
        pollRunning();
        json arr = json::array();
        for (const auto& [id, j] : g_jobs) arr.push_back(jobToJson(j));
        res.set_content(arr.dump(2), "application/json");
    });

    srv.Get(R"(/jobs/(\d+))", [](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        std::lock_guard<std::mutex> lk(g_mu);
        pollRunning();
        auto it = g_jobs.find(id);
        if (it == g_jobs.end()) { res.status=404; res.set_content(json{{"error","Not found"}}.dump(),"application/json"); return; }
        res.set_content(jobToJson(it->second).dump(2), "application/json");
    });

    srv.Post(R"(/jobs/(\d+)/cancel)", [](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_jobs.find(id);
        if (it == g_jobs.end()) { res.status=404; res.set_content(json{{"error","Not found"}}.dump(),"application/json"); return; }
        Job& j = it->second;
        if (j.state == "running") {
#ifdef _WIN32
            if (j.hProcess != INVALID_HANDLE_VALUE) { TerminateProcess(j.hProcess,1); CloseHandle(j.hProcess); j.hProcess=INVALID_HANDLE_VALUE; }
#else
            if (j.pid > 0) kill(j.pid, SIGTERM);
#endif
            j.state="cancelled"; j.endTime=nowISO();
            persistJob(j);
        }
        res.set_content(jobToJson(j).dump(2), "application/json");
    });

    srv.Get(R"(/jobs/(\d+)/logs)", [](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        std::string prefix;
        { std::lock_guard<std::mutex> lk(g_mu);
          auto it = g_jobs.find(id);
          if (it == g_jobs.end()) { res.status=404; res.set_content(json{{"error","Not found"}}.dump(),"application/json"); return; }
          prefix = it->second.outputPrefix; }
        std::string logs;
        for (const char* f : {"Log.out","Log.progress.out","Log.final.out"}) {
            std::string c = readFileTail(prefix + f);
            if (!c.empty()) { if (!logs.empty()) logs+="\n\n--- "+std::string(f)+" ---\n"; logs+=c; }
        }
        res.set_content(json{{"logs",logs}}.dump(), "application/json");
    });

    srv.Get(R"(/jobs/(\d+)/report)", [](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        std::string rpath;
        { std::lock_guard<std::mutex> lk(g_mu);
          auto it = g_jobs.find(id);
          if (it == g_jobs.end()) { res.status=404; res.set_content("Not found","text/plain"); return; }
          rpath = it->second.reportPath; }
        if (rpath.empty()) { res.status=404; res.set_content("Report not generated","text/plain"); return; }
        std::string html = readFileTail(rpath, 2*1024*1024);
        if (html.empty()) { res.status=404; res.set_content("Report file missing","text/plain"); return; }
        res.set_content(html, "text/html; charset=utf-8");
    });

    // Phase 3: artifact discovery endpoint
    srv.Get(R"(/jobs/(\d+)/artifacts)", [](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        std::string prefix;
        { std::lock_guard<std::mutex> lk(g_mu);
          auto it = g_jobs.find(id);
          if (it == g_jobs.end()) { res.status=404; res.set_content(json{{"error","Not found"}}.dump(),"application/json"); return; }
          prefix = it->second.outputPrefix; }

        json arr = json::array();
        auto addFile = [&](const std::string& relName) {
            std::string full = prefix + relName;
            int64_t sz = fileSize(full);
            if (sz < 0) return;
            arr.push_back({{"name", relName}, {"size", sz}, {"type", artifactType(relName)}});
        };

        // Recurse into Solo.out/ for .tsv, .mtx, .csv, .stats files
        std::string soloDir = prefix + "Solo.out";
        if (isDirectory(soloDir)) {
            std::vector<std::string> soloFiles;
            int count = 0;
            listDirRecursive(soloDir, "", {".tsv",".mtx",".csv",".stats"}, soloFiles, count, 100);
            for (const auto& f : soloFiles)
                addFile("Solo.out/" + f);
        }

        // Top-level known files
        for (const char* name : {"Aligned.sortedByCoord.out.bam", "Aligned.out.bam",
                                  "Aligned.sortedByCoord.out.cram", "Aligned.out.cram",
                                  "Aligned.out.sam", "SJ.out.tab",
                                  "Log.final.out", "Log.out", "WebUI_Report.html"}) {
            addFile(name);
        }

        res.set_content(arr.dump(2), "application/json");
    });

    std::string host = P.webui.host;
    int         port = P.webui.port;
    std::cout << "STAR WebUI listening on http://" << host << ":" << port << "\n"
              << "Press Ctrl+C to stop.\n" << std::flush;
    if (!srv.listen(host, port)) {
        std::cerr << "EXITING: WebUI failed to bind to " << host << ":" << port
                  << " \xe2\x80\x94 check that the port is not already in use.\n";
        exit(1);
    }
}
