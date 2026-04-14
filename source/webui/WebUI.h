#pragma once
#include "../Parameters.h"

// Phase 0: HTTP server skeleton.
// Serves GET /health and GET /props; optionally GET /metrics.
// Later phases add job submission and a browser UI.
class WebUI {
public:
    explicit WebUI(Parameters& P);
    void run(); // blocks until Ctrl-C / SIGINT
private:
    Parameters& P;
};
