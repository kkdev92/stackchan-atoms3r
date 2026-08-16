#include "responses.hpp"

#include <array>

#include "stackchan/app/envelope.hpp"
#include "stackchan/domain/json_writer.hpp"

namespace stackchan::deviceapi {

esp_err_t send_json(httpd_req_t* request, std::string_view body) {
  httpd_resp_set_type(request, "application/json");
  return httpd_resp_send(request, body.data(), static_cast<ssize_t>(body.size()));
}

esp_err_t send_forbidden(httpd_req_t* request) {
  httpd_resp_set_status(request, "403 Forbidden");
  return send_json(request,
                   R"({"ok":false,"error":"provisioning is only available )"
                   R"(via the setup access point"})");
}

esp_err_t send_transport_error(httpd_req_t* request, domain::ErrorCode code,
                               std::string_view message) {
  // The server runs one task and handlers are serialised, so a
  // function-local static is enough.
  static std::array<char, 512> buffer{};
  domain::JsonWriter writer{buffer.data(), buffer.size()};
  // The id could not be read, because the body never arrived, so it is
  // returned empty.
  app::write_error(writer, {}, code, message);
  if (!writer.valid()) {
    // Even building the envelope failed. Return the minimum that is still
    // valid.
    return send_json(request, R"({"v":1,"kind":"result","ok":false,)"
                              R"("error":{"code":"internal","retryable":false}})");
  }
  return send_json(request, writer.text());
}

}  // namespace stackchan::deviceapi
