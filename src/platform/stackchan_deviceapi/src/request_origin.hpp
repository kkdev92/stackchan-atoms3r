#pragma once

#include <optional>

#include "esp_http_server.h"
#include "stackchan/domain/provisioning_scope.hpp"

// Gathering the facts about where a request arrived (internal to this
// component).
//
// The decision belongs to domain::provisioning_allowed, in core, where it
// is tested. This only extracts the facts from the socket and the network
// interface.

namespace stackchan::deviceapi {

// Which of this device's interfaces the request arrived on, or nullopt if
// it could not be determined.
//
// IPv6 is enabled, so the listener is AF_INET6 and IPv4 connections appear
// as v4-mapped addresses, with the four bytes in the last part of the
// address in network order.
[[nodiscard]] std::optional<domain::Ipv4> request_local_address(httpd_req_t* request);

// The access point interface's address, or nullopt when it is down.
// Looked up each time rather than cached, so nothing has to track it as the
// access point comes and goes.
[[nodiscard]] std::optional<domain::Ipv4> access_point_address();

// Whether a configuration route may be entered: gather the facts and hand
// them to the decision in core.
[[nodiscard]] bool provisioning_request_allowed(httpd_req_t* request);

}  // namespace stackchan::deviceapi
