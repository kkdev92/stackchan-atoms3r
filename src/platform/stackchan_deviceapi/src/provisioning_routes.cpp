#include "provisioning_routes.hpp"

#include <array>
#include <string_view>

#include "request_origin.hpp"
#include "responses.hpp"
#include "stackchan/domain/json_scan.hpp"
#include "stackchan/domain/json_writer.hpp"
#include "stackchan/net/wifi.hpp"
#include "stackchan/runtime/receive_exact.hpp"

namespace stackchan::deviceapi {
namespace {

// A minimal page, enough to enter credentials.
esp_err_t handle_root(httpd_req_t* request) {
  if (!provisioning_request_allowed(request)) {
    return send_forbidden(request);
  }
  httpd_resp_set_type(request, "text/plain; charset=utf-8");
  constexpr char kPage[] =
      "StackChan provisioning\n"
      "GET  /scan : list nearby networks\n"
      "POST /save : {\"ssid\":\"...\",\"pass\":\"...\"}\n";
  return httpd_resp_send(request, kPage, static_cast<ssize_t>(sizeof(kPage) - 1));
}

esp_err_t handle_scan(httpd_req_t* request) {
  if (!provisioning_request_allowed(request)) {
    return send_forbidden(request);
  }
  const net::ScanEntry* entries = nullptr;
  const std::size_t count = net::scan_results(&entries);

  static std::array<char, 1536> buffer{};
  domain::JsonWriter writer{buffer.data(), buffer.size()};
  writer.begin_array();
  for (std::size_t i = 0; i < count; ++i) {
    writer.begin_object();
    writer.member("ssid", std::string_view{entries[i].ssid});
    writer.member("rssi", static_cast<std::int64_t>(entries[i].rssi));
    writer.end_object();
  }
  writer.end_array();
  return send_json(request, writer.valid() ? writer.text() : "[]");
}

esp_err_t handle_save(httpd_req_t* request) {
  if (!provisioning_request_allowed(request)) {
    return send_forbidden(request);
  }
  if (request->content_len == 0 ||
      static_cast<std::size_t>(request->content_len) > 512) {
    return send_json(request, R"({"ok":false,"error":"bad request"})");
  }
  std::array<char, 513> body{};
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
    return send_json(request, R"({"ok":false,"error":"truncated"})");
  }
  const std::string_view text{body.data(), content_length};

  // Parsed with the same scanner as everything else. The input comes from
  // this device's own page, so the contract's escaping rules suffice.
  std::string_view raw_ssid;
  if (!domain::json_find_raw_string(text, "ssid", raw_ssid)) {
    return send_json(request, R"({"ok":false,"error":"not json"})");
  }
  std::array<char, 33> ssid{};
  const std::size_t ssid_len =
      domain::json_unescape(raw_ssid, ssid.data(), ssid.size() - 1);
  if (ssid_len == SIZE_MAX || ssid_len == 0) {
    return send_json(request, R"({"ok":false,"error":"bad ssid"})");
  }

  std::array<char, 65> password{};
  std::size_t password_len = 0;
  std::string_view raw_password;
  if (domain::json_find_raw_string(text, "pass", raw_password)) {
    password_len =
        domain::json_unescape(raw_password, password.data(), password.size() - 1);
    if (password_len == SIZE_MAX) {
      return send_json(request, R"({"ok":false,"error":"bad pass"})");
    }
  }

  const bool saved =
      net::save_credentials(std::string_view{ssid.data(), ssid_len},
                            std::string_view{password.data(), password_len});
  return send_json(request, saved ? R"({"ok":true})"
                                  : R"({"ok":false,"error":"could not store"})");
}

}  // namespace

bool install_provisioning_routes(httpd_handle_t server) {
  const std::array<httpd_uri_t, 3> routes = {{
      {"/", HTTP_GET, &handle_root, nullptr},
      {"/scan", HTTP_GET, &handle_scan, nullptr},
      {"/save", HTTP_POST, &handle_save, nullptr},
  }};
  for (const auto& route : routes) {
    if (httpd_register_uri_handler(server, &route) != ESP_OK) {
      return false;
    }
  }
  return true;
}

}  // namespace stackchan::deviceapi
