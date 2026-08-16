#pragma once

#include "esp_http_server.h"

// The three configuration routes: GET /, GET /scan and POST /save.
//
// One responsibility between them: all three sit behind the same gate and
// open and close for the same reason — a request that arrived on the access
// point's interface. They accept and return minimal JSON.

namespace stackchan::deviceapi {

// Install all three.
[[nodiscard]] bool install_provisioning_routes(httpd_handle_t server);

}  // namespace stackchan::deviceapi
