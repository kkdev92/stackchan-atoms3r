#pragma once

#include "esp_http_server.h"
#include "stackchan/app/request_router.hpp"

// Wiring for POST /api/v1/command (internal to this component).
//
// The decisions are app::RequestRouter's. What is here is extracting the
// header and the body, and sending the result.

namespace stackchan::deviceapi {

// Install the route. The caller must keep the router alive.
[[nodiscard]] bool install_command_route(httpd_handle_t server,
                                         app::RequestRouter& router);

}  // namespace stackchan::deviceapi
