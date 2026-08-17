#include "stackchan/deviceapi/server.hpp"

#include "command_route.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "info_route.hpp"
#include "provisioning_routes.hpp"

// Starting the server and installing the routes. Each route's body lives in
// its own file:
//
//   command_route        POST /api/v1/command; decisions in RequestRouter
//   provisioning_routes  GET /, GET /scan, POST /save; access point only
//   info_route           GET /info; likewise
//
// To add a route, add a file and one install line here.

namespace stackchan::deviceapi {
namespace {

constexpr char kTag[] = "deviceapi";

httpd_handle_t g_server = nullptr;

}  // namespace

bool begin(app::RequestRouter& router, const app::DeviceIdentity& identity) {
  if (g_server != nullptr) {
    return true;
  }

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.lru_purge_enable = true;
  config.max_uri_handlers = 6;
  config.stack_size = 8192;

  if (httpd_start(&g_server, &config) != ESP_OK) {
    ESP_LOGE(kTag, "http server could not be started");
    g_server = nullptr;
    return false;
  }

  bool installed = install_command_route(g_server, router);
  installed = install_provisioning_routes(g_server) && installed;
  installed = install_info_route(g_server, identity) && installed;
  if (!installed) {
    ESP_LOGE(kTag, "some routes could not be registered");
  }

  ESP_LOGI(kTag, "listening on port %d", config.server_port);
  return true;
}

bool running() { return g_server != nullptr; }

}  // namespace stackchan::deviceapi
