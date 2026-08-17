#pragma once

#include "stackchan/app/envelope.hpp"
#include "stackchan/app/request_router.hpp"

// The HTTP entry point. It carries bytes and nothing else.
//
// No decisions live here. Authorisation, parsing the envelope, dispatching
// and building the response all belong to app::RequestRouter, in core,
// where they are covered by tests. What remains is pulling the body and the
// headers out of the request and sending back what the router returned.
//
// Two things this layer is nonetheless responsible for
// ----------------------------------------------------
// The configuration routes are restricted to requests that arrived on the
// access point's own interface, which is a fact only obtainable from the
// socket. IPv6 is enabled, so the server listens on AF_INET6 and an IPv4
// connection appears as a v4-mapped address.
//
// No Access-Control-Allow-Origin is sent. Requests are authorised, and
// there is no reason to let an arbitrary web page read anything here.
//
// There is no configuration UI
// ----------------------------
// The setup routes accept and return minimal JSON. Anything richer is a
// separate concern from what this component guarantees.

namespace stackchan::deviceapi {

// Open the entry point, after Wi-Fi is up. The router and the identity are
// held by reference, so the caller must keep them alive.
[[nodiscard]] bool begin(app::RequestRouter& router,
                         const app::DeviceIdentity& identity);

[[nodiscard]] bool running();

}  // namespace stackchan::deviceapi
