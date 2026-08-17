#pragma once

#include <string_view>

#include "esp_http_server.h"
#include "stackchan/domain/protocol.hpp"

// Sending responses (internal to this component).
//
// Just the two things every route needs. Not in the public header, because
// this is HTTP mechanics rather than anything this component promises.

namespace stackchan::deviceapi {

// Send JSON. No CORS wildcard: requests are authorised in the router, and
// there is no reason to open this to arbitrary pages.
esp_err_t send_json(httpd_req_t* request, std::string_view body);

// Refuse a configuration route that arrived on the wrong interface.
esp_err_t send_forbidden(httpd_req_t* request);

// Report a transport-level failure as a proper envelope.
//
// Used when the body never reached the router. The router decides about the
// contents of a body; "the body did not arrive" is a fact about the
// transport, so it is answered here.
//
// Flattening the reason and passing an empty body instead would mislead the
// sender: a connection cut mid-body would read as "empty or too large".
esp_err_t send_transport_error(httpd_req_t* request, domain::ErrorCode code,
                               std::string_view message);

}  // namespace stackchan::deviceapi
