#pragma once

#include "esp_http_server.h"
#include "stackchan/app/envelope.hpp"

// GET /info — a small window onto the device's identity.
//
// Only served on the configuration interface. The device id and address do
// not need to be readable by an arbitrary web page.

namespace stackchan::deviceapi {

// The caller must keep the identity alive.
[[nodiscard]] bool install_info_route(httpd_handle_t server,
                                      const app::DeviceIdentity& identity);

}  // namespace stackchan::deviceapi
