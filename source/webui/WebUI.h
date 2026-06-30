#pragma once
#include "../Parameters.h"

// Phase 0-2: HTTP server with job runner and browser UI.
// Serves GET /health, GET /props, POST /jobs, GET /jobs,
// GET /jobs/:id, POST /jobs/:id/cancel, GET /jobs/:id/logs, GET /.
class WebUI {
public:
    explicit WebUI(Parameters& P);
    void run(); // blocks until Ctrl-C / SIGINT
private:
    Parameters& P;
};
