// CPPHTTPLIB_IMPLEMENTATION must be defined in exactly one TU that includes httplib.h.
#define CPPHTTPLIB_IMPLEMENTATION
#include "httplib.h"
#include <nlohmann/json.hpp>
#include "WebUI.h"
#include "../IncludeDefine.h"
#include <iostream>
#include <string>

using json = nlohmann::json;

WebUI::WebUI(Parameters& P) : P(P) {}

void WebUI::run() {
    httplib::Server srv;

    // GET /health — liveness probe
    srv.Get("/health", [](const httplib::Request&, httplib::Response& res) {
        json body = {{"status", "ok"}};
        res.set_content(body.dump(), "application/json");
    });

    // GET /props — static server metadata
    srv.Get("/props", [this](const httplib::Request&, httplib::Response& res) {
        json body = {
            {"version",       STAR_VERSION},
            {"build",         COMPILATION_TIME_PLACE},
            {"git",           GIT_BRANCH_COMMIT_DIFF},
#ifdef _WIN32
            {"platform",      "windows"},
#elif defined(__APPLE__)
            {"platform",      "macos"},
#else
            {"platform",      "linux"},
#endif
            {"runModes",      json::array({"alignReads", "genomeGenerate", "soloCellFiltering", "webui"})},
            {"webuiVersion",  0},  // phase number
        };
        res.set_content(body.dump(2), "application/json");
    });

    // GET /metrics — optional Prometheus-style stub (Phase 0: empty)
    if (P.webui.metrics) {
        srv.Get("/metrics", [](const httplib::Request&, httplib::Response& res) {
            // Phase 0: expose nothing; hook is in place for Phase 4.
            res.set_content("# STAR WebUI metrics\n", "text/plain; version=0.0.4");
        });
    }

    std::string host = P.webui.host;
    int         port = P.webui.port;

    std::cout << "STAR WebUI listening on http://" << host << ":" << port << "\n"
              << "  GET /health\n"
              << "  GET /props\n";
    if (P.webui.metrics)
        std::cout << "  GET /metrics\n";
    std::cout << "Press Ctrl+C to stop.\n" << std::flush;

    if (!srv.listen(host, port)) {
        std::cerr << "EXITING: WebUI failed to bind to " << host << ":" << port
                  << " — check that the port is not already in use.\n";
        exit(1);
    }
}
