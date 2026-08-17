#include "command_route.hpp"

#include <array>
#include <string_view>

#include "responses.hpp"
#include "stackchan/runtime/receive_exact.hpp"

namespace stackchan::deviceapi {
namespace {

// Arrives through the server's user context. Not owned; begin() guarantees
// its lifetime.
[[nodiscard]] app::RequestRouter& router_of(httpd_req_t* request) {
  return *static_cast<app::RequestRouter*>(request->user_ctx);
}

esp_err_t handle_command(httpd_req_t* request) {
  app::RequestRouter& router = router_of(request);

  // Take the authorisation header first, and pass it even when the body
  // turns out to be unusable. Otherwise "the body was too large" comes
  // back wearing the envelope for "the token was wrong".
  std::array<char, 48> token{};
  (void)httpd_req_get_hdr_value_str(request, "X-StackChan-Token", token.data(),
                                    token.size());
  const std::string_view presented{token.data()};

  // The body, capped at the same size the router enforces; this limits how
  // much is read in the first place.
  if (request->content_len == 0 ||
      static_cast<std::size_t>(request->content_len) >
          app::RequestRouter::kMaxBodyBytes) {
    // An empty body makes the router produce the "empty or too large"
    // envelope.
    return send_json(request, router.route(presented, {}));
  }

  // A static buffer is enough: the server runs one task and handlers are
  // serialised.
  static std::array<char, app::RequestRouter::kMaxBodyBytes + 1> body{};
  const std::size_t content_length =
      static_cast<std::size_t>(request->content_len);
  const runtime::ReceiveExactResult receive_result = runtime::receive_exact(
      body.data(), content_length,
      [request](char* out, std::size_t remaining) {
        return httpd_req_recv(request, out, remaining);
      });
  if (receive_result == runtime::ReceiveExactResult::error) {
    return ESP_FAIL;
  }
  if (receive_result == runtime::ReceiveExactResult::incomplete) {
    // Interpreting a prefix could change the request's meaning. Do not pass it
    // to the router as an empty or shortened body.
    return send_transport_error(request, domain::ErrorCode::bad_request,
                                "body truncated");
  }

  return send_json(request,
                   router.route(presented,
                                std::string_view{body.data(), content_length}));
}

}  // namespace

bool install_command_route(httpd_handle_t server, app::RequestRouter& router) {
  const httpd_uri_t route = {"/api/v1/command", HTTP_POST, &handle_command, &router};
  return httpd_register_uri_handler(server, &route) == ESP_OK;
}

}  // namespace stackchan::deviceapi
