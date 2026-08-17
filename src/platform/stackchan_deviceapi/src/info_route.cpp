#include "info_route.hpp"

#include <array>
#include <string_view>

#include "request_origin.hpp"
#include "responses.hpp"
#include "stackchan/domain/json_writer.hpp"
#include "stackchan/net/wifi.hpp"

namespace stackchan::deviceapi {
namespace {

[[nodiscard]] const app::DeviceIdentity& identity_of(httpd_req_t* request) {
  return *static_cast<const app::DeviceIdentity*>(request->user_ctx);
}

esp_err_t handle_info(httpd_req_t* request) {
  if (!provisioning_request_allowed(request)) {
    return send_forbidden(request);
  }
  const app::DeviceIdentity& identity = identity_of(request);
  const net::Status& wifi = net::status();

  static std::array<char, 256> buffer{};
  domain::JsonWriter writer{buffer.data(), buffer.size()};
  writer.begin_object();
  writer.member("device_id", identity.device_id);
  writer.member("version", identity.firmware_version);
  writer.member("ap", wifi.access_point_up);
  writer.member("ip", std::string_view{wifi.ip});
  writer.end_object();
  return send_json(request, writer.valid() ? writer.text() : "{}");
}

}  // namespace

bool install_info_route(httpd_handle_t server, const app::DeviceIdentity& identity) {
  // The user context is a writable void*, and the identity is only read,
  // so the const is cast away here.
  const httpd_uri_t route = {"/info", HTTP_GET, &handle_info,
                             const_cast<app::DeviceIdentity*>(&identity)};
  return httpd_register_uri_handler(server, &route) == ESP_OK;
}

}  // namespace stackchan::deviceapi
