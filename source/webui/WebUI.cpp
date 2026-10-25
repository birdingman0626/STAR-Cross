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
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>
using namespace std;
#ifdef _WIN32
#  include <windows.h>
#else
#  include <cerrno>
#  include <csignal>
#  include <dirent.h>
#  include <fcntl.h>
#  include <sys/stat.h>
#  include <sys/wait.h>
#  include <unistd.h>
#  ifdef __linux__
#    include <linux/limits.h>
#  else
#    include <limits.h>
#  endif
#  ifdef __APPLE__
#    include <cstdint>
#    include <stdlib.h>
// Public dyld API, declared directly: <mach-o/dyld.h> reaches Mach headers that
// GCC cannot parse with the macOS 26 SDK (see Parameters_openReadsFiles.cpp).
extern "C" int _NSGetExecutablePath(char* buf, uint32_t* bufsize);
#  endif
#endif

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// Job registry
// ---------------------------------------------------------------------------

// Job struct is defined in WebUI_internal.h (shared with WebUI_report.cpp)

static std::map<int, Job> g_jobs;
static std::mutex         g_mu;
static std::atomic<int>   g_nextId{1};
static std::string        g_persistPath;   // path to .webui_jobs.jsonl (set in run())
static std::string        g_exe;           // this STAR binary, used for every job
static std::chrono::steady_clock::time_point g_startTime = std::chrono::steady_clock::now();

// Bounded queue limits: 1 running + up to MAX_QUEUED waiting
static constexpr int MAX_QUEUED = 9;

// Only these modes may be submitted or restored; anything else is rejected
// before it reaches a command line, a persisted record or the page.
static const std::set<std::string> kRunModes = {"alignReads", "genomeGenerate", "soloCellFiltering"};

// Each job's STAR stdout/stderr, written under its output prefix.
static const char* kJobConsole = "WebUI_console.log";

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

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
#elif defined(__APPLE__)
    // No /proc on macOS: without this, jobs exec'd the bare name "STAR" and exited 127.
    uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::vector<char> path(size + 1);
    if (_NSGetExecutablePath(path.data(), &size) != 0) return "STAR";
    char resolved[PATH_MAX];
    return realpath(path.data(), resolved) ? std::string(resolved) : std::string(path.data());
#else
    char buf[PATH_MAX] = {};
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0) { buf[n] = '\0'; return buf; }
    return "STAR";
#endif
}

// Quote one argument so CommandLineToArgvW / the MSVC CRT parse it back exactly.
// Also used for the displayed command line on every platform.
static std::string quoteArg(const std::string& a) {
    if (!a.empty() && a.find_first_of(" \t\n\v\"") == std::string::npos) return a;
    std::string r = "\"";
    size_t backslashes = 0;
    for (char c : a) {
        if (c == '\\') { ++backslashes; continue; }
        if (c == '"') r.append(backslashes * 2 + 1, '\\');
        else          r.append(backslashes, '\\');
        backslashes = 0;
        r += c;
    }
    r.append(backslashes * 2, '\\');
    return r + '"';
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
// Absolute paths remain allowed: the server runs jobs as the local user.
static bool hasPathTraversal(const std::string& p) {
    std::string n = p;
    for (char& c : n) if (c == '\\') c = '/';
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
            if (j.id <= 0 || !kRunModes.count(j.runMode)) continue;
            // Only terminal states are persisted; never resurrect a live job.
            if (j.state != "succeeded" && j.state != "failed" && j.state != "cancelled") continue;
            const int id = j.id;
            g_jobs[id] = std::move(j);
            int cur = g_nextId.load();
            while (cur <= id && !g_nextId.compare_exchange_weak(cur, id + 1)) {}
        } catch (...) { /* skip malformed lines */ }
    }
}

// ---------------------------------------------------------------------------
// Process spawning
// ---------------------------------------------------------------------------

// Starts job.args with g_exe. Returns 0 on success, otherwise an OS error code.
static int spawnJob(Job& job) {
    std::string cmd = quoteArg(g_exe);
    for (const auto& a : job.args) cmd += ' ' + quoteArg(a);
    job.cmdLine = cmd;

    std::error_code ec;
    std::filesystem::create_directories(job.outputPrefix, ec); // log target; STAR reuses it
    const std::string console = job.outputPrefix + kJobConsole;

#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE out = CreateFileA(console.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE) // e.g. an invalid prefix: still run STAR so it reports the path itself
        out = CreateFileA("NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    HANDLE in  = CreateFileA("NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             &sa, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (out == INVALID_HANDLE_VALUE || in == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (out != INVALID_HANDLE_VALUE) CloseHandle(out);
        if (in  != INVALID_HANDLE_VALUE) CloseHandle(in);
        return err ? (int)err : 1;
    }
    // Inherit only the console handles: never the server's listening socket.
    HANDLE inherit[2] = {in, out};
    SIZE_T attrSize = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrSize);
    std::vector<char> attrBuf(attrSize);
    auto* attrs = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attrBuf.data());
    int rc = 0;
    if (!InitializeProcThreadAttributeList(attrs, 1, 0, &attrSize)) {
        rc = (int)GetLastError();
    } else {
        if (!UpdateProcThreadAttribute(attrs, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                       inherit, sizeof(inherit), nullptr, nullptr)) {
            rc = (int)GetLastError();
        } else {
            STARTUPINFOEXA si{};
            si.StartupInfo.cb = sizeof(si);
            si.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
            si.StartupInfo.hStdInput = in;
            si.StartupInfo.hStdOutput = out;
            si.StartupInfo.hStdError = out;
            si.lpAttributeList = attrs;
            PROCESS_INFORMATION pi{};
            std::string buf = cmd;
            // Start suspended so STAR joins its Job Object before it can spawn helpers
            // (e.g. --readFilesCommand); cancel then ends the whole tree, as on POSIX.
            if (CreateProcessA(nullptr, buf.data(), nullptr, nullptr, TRUE,
                               CREATE_NEW_PROCESS_GROUP | EXTENDED_STARTUPINFO_PRESENT | CREATE_SUSPENDED,
                               nullptr, nullptr, &si.StartupInfo, &pi)) {
                HANDLE group = CreateJobObjectA(nullptr, nullptr);
                if (group && !AssignProcessToJobObject(group, pi.hProcess)) {
                    CloseHandle(group); // e.g. a server already inside a restrictive job:
                    group = nullptr;    // still run STAR; cancel falls back to the process
                }
                job.hJob = group;
                ResumeThread(pi.hThread);
                CloseHandle(pi.hThread);
                job.hProcess = pi.hProcess;
            } else {
                rc = (int)GetLastError();
            }
        }
        DeleteProcThreadAttributeList(attrs);
    }
    CloseHandle(out);
    CloseHandle(in);
    return rc;
#else
    // Prepare everything before fork: the child may only call async-signal-safe functions.
    std::vector<const char*> argv;
    argv.push_back(g_exe.c_str());
    for (const auto& a : job.args) argv.push_back(a.c_str());
    argv.push_back(nullptr);
    int out = open(console.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    int in  = open("/dev/null", O_RDONLY | O_CLOEXEC);
    pid_t pid = fork();
    if (pid < 0) {
        int err = errno;
        if (out >= 0) close(out);
        if (in >= 0) close(in);
        return err ? err : 1;
    }
    if (pid == 0) {
        setpgid(0, 0); // own group: cancel also stops readFilesCommand helpers
        if (in >= 0)  dup2(in, 0);
        if (out >= 0) { dup2(out, 1); dup2(out, 2); }
        execv(g_exe.c_str(), const_cast<char* const*>(argv.data()));
        _exit(127);
    }
    setpgid(pid, pid); // also from the parent, so a cancel cannot race the child
    if (out >= 0) close(out);
    if (in >= 0) close(in);
    job.pid = pid;
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// Queue maintenance (all functions below require g_mu held)
// ---------------------------------------------------------------------------

static void finishJob(Job& job, int exitCode) {
    job.exitCode = exitCode;
    job.state    = (exitCode == 0) ? "succeeded" : "failed";
    job.endTime  = nowISO();
    if (exitCode == 0 && job.generateReport) writeReport(job);
    persistJob(job);
}

// Start the oldest queued job when nothing is running. A failed spawn is recorded
// as a failed job and the next queued job is tried, so the queue never stalls.
static void promoteQueued() {
    while (true) {
        for (const auto& [id, j] : g_jobs)
            if (j.state == "running") return;
        Job* next = nullptr;
        for (auto& [id, j] : g_jobs)
            if (j.state == "queued") { next = &j; break; } // map order: lowest id first
        if (!next) return;
        next->state     = "running";
        next->startTime = nowISO();
        const int rc = spawnJob(*next);
        next->args.clear();
        if (rc == 0) return;
        finishJob(*next, -1);
    }
}

// Collect finished children (including cancelled ones still exiting), then promote.
static void pollRunning() {
    for (auto& [id, job] : g_jobs) {
#ifdef _WIN32
        if (job.state != "running" || job.hProcess == INVALID_HANDLE_VALUE) continue;
        if (WaitForSingleObject(job.hProcess, 0) != WAIT_OBJECT_0) continue;
        DWORD ec = 0;
        GetExitCodeProcess(job.hProcess, &ec);
        CloseHandle(job.hProcess);
        job.hProcess = INVALID_HANDLE_VALUE;
        if (job.hJob) { CloseHandle(job.hJob); job.hJob = nullptr; }
        finishJob(job, (int)ec);
#else
        if (job.pid <= 0) continue; // never waitpid(-1/0): that could reap unrelated children
        int status = 0;
        const pid_t r = waitpid(job.pid, &status, WNOHANG);
        if (r == 0) continue;
        const bool reaped = (r == job.pid);
        if (!reaped && !(r < 0 && errno == ECHILD)) continue;
        job.pid = -1;
        if (job.state == "running")
            finishJob(job, reaped && WIFEXITED(status) ? WEXITSTATUS(status) : -1);
#endif
    }
    promoteQueued();
}

static json jobToJson(const Job& j) {
    return {{"id", j.id}, {"state", j.state}, {"runMode", j.runMode},
            {"cmdLine", j.cmdLine}, {"outputPrefix", j.outputPrefix},
            {"submitTime", j.submitTime}, {"startTime", j.startTime},
            {"endTime", j.endTime}, {"exitCode", j.exitCode},
            {"reportPath", j.reportPath}};
}

// ---------------------------------------------------------------------------
// Argument builder
// ---------------------------------------------------------------------------

static std::pair<std::vector<std::string>, std::string>
buildArgs(const json& b, std::string& err) {
    auto str = [&](const char* k, const std::string& def = "") -> std::string {
        if (b.contains(k) && b[k].is_string()) return b[k].get<std::string>();
        return def;
    };

    std::string runMode = str("runMode");
    if (runMode.empty()) { err = "Missing runMode"; return {}; }
    if (!kRunModes.count(runMode)) { err = "Unsupported runMode"; return {}; }

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
            if (!rfi.is_string()) { err = "readFilesIn entries must be strings"; return {}; }
            if (hasPathTraversal(rfi.get<std::string>())) {
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
                for (const auto& f : arr) {
                    if (!f.is_string()) { err = std::string(k) + " entries must be strings"; return {}; }
                    args.push_back(f.get<std::string>());
                }
            }
        }
    }

    // Extra verbatim args (e.g. --legacy). These can run commands (--readFilesCommand);
    // the request guard ensures only the local page or an authenticated client gets here.
    if (b.contains("extraArgs") && b["extraArgs"].is_string()) {
        const std::string extra = b["extraArgs"].get<std::string>();
        std::istringstream iss(extra);
        std::string tok;
        while (iss >> tok) args.push_back(tok);
    }

    return {args, outPrefix};
}

// ---------------------------------------------------------------------------
// Request guard
// ---------------------------------------------------------------------------
// Jobs run arbitrary STAR arguments (including commands via --readFilesCommand),
// so only the server's own page may drive it:
//  - Host must name this server, defeating DNS rebinding (loopback binds);
//  - a present Origin must equal this server's origin, defeating cross-site requests;
//  - POST bodies must be application/json, which browsers cannot send cross-site
//    without a CORS preflight this server never approves;
//  - non-loopback binds additionally require a per-launch token (cookie or header).

static bool isLoopbackHost(const std::string& h) {
    return h == "localhost" || h == "::1" || h == "[::1]" || h.rfind("127.", 0) == 0;
}

static std::string randomToken() {
    std::random_device rd;
    static const char* hex = "0123456789abcdef";
    std::string t;
    for (int i = 0; i < 32; ++i) t += hex[rd() & 15];
    return t;
}

static std::string cookieValue(const std::string& cookies, const std::string& name) {
    std::istringstream ss(cookies);
    std::string part;
    while (std::getline(ss, part, ';')) {
        const auto start = part.find_first_not_of(' ');
        if (start == std::string::npos) continue;
        part = part.substr(start);
        if (part.rfind(name + "=", 0) == 0) return part.substr(name.size() + 1);
    }
    return "";
}

namespace {
struct RequestGuard {
    bool loopback = true;
    std::set<std::string> hosts; // accepted Host header values (loopback binds)
    std::string token;           // required on non-loopback binds

    RequestGuard(const std::string& bindHost, int port) {
        loopback = isLoopbackHost(bindHost);
        const std::string p = std::to_string(port);
        for (const char* h : {"127.0.0.1", "localhost", "[::1]"}) {
            hosts.insert(std::string(h) + ":" + p);
            if (port == 80) hosts.insert(h);
        }
        if (loopback) {
            const std::string b = (bindHost.find(':') != std::string::npos && bindHost[0] != '[')
                                  ? "[" + bindHost + "]" : bindHost;
            hosts.insert(b + ":" + p);
        } else {
            token = randomToken();
        }
    }

    // Returns an HTTP status (0 = allowed) and the reason.
    int check(const httplib::Request& req, std::string& why) const {
        const std::string host = req.get_header_value("Host");
        if (loopback && !hosts.count(host)) { why = "Host not allowed"; return 403; }
        if (req.has_header("Origin") && req.get_header_value("Origin") != "http://" + host) {
            why = "Cross-origin request rejected"; return 403;
        }
        if (!loopback) {
            const bool viaPage = req.method == "GET" && req.path == "/"
                                 && req.get_param_value("token") == token;
            if (!viaPage && cookieValue(req.get_header_value("Cookie"), "star_webui_token") != token
                && req.get_header_value("X-STAR-WebUI-Token") != token) {
                why = "Missing or invalid token"; return 401;
            }
        }
        if (req.method == "POST") {
            const std::string type = req.get_header_value("Content-Type");
            if (type.rfind("application/json", 0) != 0) {
                why = "Content-Type must be application/json"; return 415;
            }
        }
        return 0;
    }
};
} // namespace

// ---------------------------------------------------------------------------
// Embedded HTML UI  (content in WebUI_html.inc)
// ---------------------------------------------------------------------------

static const char* g_html =
#include "WebUI_html.inc"
;

static void jsonError(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(json{{"error", message}}.dump(), "application/json");
}

// ---------------------------------------------------------------------------
// WebUI::run
// ---------------------------------------------------------------------------

WebUI::WebUI(Parameters& P) : P(P) {}

void WebUI::run() {
    httplib::Server srv;
    g_exe = starExePath();
    const std::string host = P.webui.host;
    const int         port = P.webui.port;
    const RequestGuard guard(host, port);

    // Set up persistence and restore historical jobs
    {
        std::string outDir = P.outFileNamePrefix;
        if (!outDir.empty() && outDir.back() != '/' && outDir.back() != '\\') outDir += '/';
        g_persistPath = outDir + ".webui_jobs.jsonl";
        std::lock_guard<std::mutex> lk(g_mu);
        loadPersistedJobs(g_persistPath);
    }

    srv.set_pre_routing_handler([&guard](const httplib::Request& req, httplib::Response& res) {
        std::string why;
        const int status = guard.check(req, why);
        if (status == 0) return httplib::Server::HandlerResponse::Unhandled;
        jsonError(res, status, why);
        return httplib::Server::HandlerResponse::Handled;
    });
    srv.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("X-Content-Type-Options", "nosniff");
        res.set_header("X-Frame-Options", "DENY");
    });

    srv.Get("/",  [&guard](const httplib::Request& req, httplib::Response& res) {
        if (!guard.token.empty() && req.get_param_value("token") == guard.token)
            res.set_header("Set-Cookie", "star_webui_token=" + guard.token
                                         + "; HttpOnly; SameSite=Strict; Path=/");
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
            {"webuiVersion", 5},
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

    srv.Post("/jobs", [](const httplib::Request& req, httplib::Response& res) {
        json body;
        try { body = json::parse(req.body); }
        catch (...) { jsonError(res, 400, "Invalid JSON"); return; }
        if (!body.is_object()) { jsonError(res, 400, "Invalid JSON"); return; }
        std::string err;
        std::vector<std::string> args;
        std::string outPrefix;
        try { std::tie(args, outPrefix) = buildArgs(body, err); }
        catch (const json::exception&) { err = "Invalid parameter type"; }
        if (!err.empty()) { jsonError(res, 400, err); return; }

        Job job;
        job.runMode        = body["runMode"].get<std::string>();
        job.outputPrefix   = outPrefix;
        job.submitTime     = nowISO();
        job.generateReport = body.value("generateReport", true);
        job.args           = std::move(args);

        std::lock_guard<std::mutex> lk(g_mu);
        pollRunning();
        int running = 0, queued = 0;
        for (const auto& [jid, jj] : g_jobs) {
            if (jj.state == "running") ++running;
            else if (jj.state == "queued") ++queued;
        }
        if (running >= 1 && queued >= MAX_QUEUED) { jsonError(res, 429, "Job queue full"); return; }
        job.id = g_nextId.fetch_add(1);
        const int jobId = job.id;
        job.state = "queued";
        g_jobs[jobId] = std::move(job);
        promoteQueued(); // starts it now when idle; a failed spawn is recorded, not lost
        Job& stored = g_jobs[jobId];
        if (stored.state == "failed") {
            // Only a failed spawn ends a just-queued job here; keep the record for the table.
            res.status = 500;
            json r = jobToJson(stored);
            r["error"] = "Failed to spawn process";
            res.set_content(r.dump(2), "application/json");
            return;
        }
        res.status = 201;
        res.set_content(jobToJson(stored).dump(2), "application/json");
    });

    srv.Get("/jobs", [](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lk(g_mu);
        pollRunning();
        json arr = json::array();
        for (const auto& [id, j] : g_jobs) arr.push_back(jobToJson(j));
        res.set_content(arr.dump(2), "application/json");
    });

    // Ids are bounded to 9 digits so std::stoi cannot overflow.
    srv.Get(R"(/jobs/(\d{1,9}))", [](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        std::lock_guard<std::mutex> lk(g_mu);
        pollRunning();
        auto it = g_jobs.find(id);
        if (it == g_jobs.end()) { jsonError(res, 404, "Not found"); return; }
        res.set_content(jobToJson(it->second).dump(2), "application/json");
    });

    srv.Post(R"(/jobs/(\d{1,9})/cancel)", [](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        std::lock_guard<std::mutex> lk(g_mu);
        pollRunning();
        auto it = g_jobs.find(id);
        if (it == g_jobs.end()) { jsonError(res, 404, "Not found"); return; }
        Job& j = it->second;
        if (j.state == "queued") {
            j.args.clear();
            j.state = "cancelled"; j.endTime = nowISO();
            persistJob(j);
        } else if (j.state == "running") {
#ifdef _WIN32
            if (j.hProcess != INVALID_HANDLE_VALUE) {
                // The Job Object ends STAR and every helper it started.
                if (!j.hJob || !TerminateJobObject(j.hJob, 1)) TerminateProcess(j.hProcess, 1);
                WaitForSingleObject(j.hProcess, 5000);
                CloseHandle(j.hProcess);
                j.hProcess = INVALID_HANDLE_VALUE;
            }
            if (j.hJob) { CloseHandle(j.hJob); j.hJob = nullptr; }
#else
            // Keep pid: pollRunning reaps the exiting child so no zombie remains.
            if (j.pid > 0 && kill(-j.pid, SIGTERM) != 0) kill(j.pid, SIGTERM);
#endif
            j.state = "cancelled"; j.endTime = nowISO();
            persistJob(j);
        }
        promoteQueued();
        res.set_content(jobToJson(j).dump(2), "application/json");
    });

    srv.Get(R"(/jobs/(\d{1,9})/logs)", [](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        std::string prefix;
        { std::lock_guard<std::mutex> lk(g_mu);
          auto it = g_jobs.find(id);
          if (it == g_jobs.end()) { jsonError(res, 404, "Not found"); return; }
          prefix = it->second.outputPrefix; }
        std::string logs;
        for (const char* f : {"Log.out","Log.progress.out","Log.final.out",kJobConsole}) {
            std::string c = readFileTail(prefix + f);
            if (!c.empty()) { if (!logs.empty()) logs+="\n\n--- "+std::string(f)+" ---\n"; logs+=c; }
        }
        res.set_content(json{{"logs",logs}}.dump(), "application/json");
    });

    srv.Get(R"(/jobs/(\d{1,9})/report)", [](const httplib::Request& req, httplib::Response& res) {
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

    // Artifact discovery endpoint
    srv.Get(R"(/jobs/(\d{1,9})/artifacts)", [](const httplib::Request& req, httplib::Response& res) {
        int id = std::stoi(req.matches[1]);
        std::string prefix;
        { std::lock_guard<std::mutex> lk(g_mu);
          auto it = g_jobs.find(id);
          if (it == g_jobs.end()) { jsonError(res, 404, "Not found"); return; }
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
                                  "Log.final.out", "Log.out", "WebUI_Report.html", kJobConsole}) {
            addFile(name);
        }

        res.set_content(arr.dump(2), "application/json");
    });

    // Advance the queue even when no browser is polling.
    std::atomic<bool> stopPoller{false};
    std::thread poller([&stopPoller] {
        while (!stopPoller.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            std::lock_guard<std::mutex> lk(g_mu);
            pollRunning();
        }
    });

    std::cout << "STAR WebUI listening on http://" << host << ":" << port << "\n";
    if (!guard.token.empty()) {
        std::cout << "Non-loopback bind: open http://<this-host>:" << port << "/?token=" << guard.token
                  << "\n(API clients: send header X-STAR-WebUI-Token). This is not TLS; use a trusted network.\n";
    }
    std::cout << "Press Ctrl+C to stop.\n" << std::flush;
    const bool listened = srv.listen(host, port);
    stopPoller = true;
    poller.join();
    if (!listened) {
        std::cerr << "EXITING: WebUI failed to bind to " << host << ":" << port
                  << " - check that the port is not already in use.\n";
        exit(1);
    }
}
