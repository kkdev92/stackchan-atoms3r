#include "request_origin.hpp"

#include "esp_netif.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "stackchan/net/wifi.hpp"

namespace stackchan::deviceapi {
namespace {

// A network-order 32-bit address to octets in written order.
[[nodiscard]] domain::Ipv4 from_network_order(std::uint32_t addr) {
  return domain::Ipv4::of(
      static_cast<std::uint8_t>(addr), static_cast<std::uint8_t>(addr >> 8),
      static_cast<std::uint8_t>(addr >> 16), static_cast<std::uint8_t>(addr >> 24));
}

}  // namespace

std::optional<domain::Ipv4> request_local_address(httpd_req_t* request) {
  const int fd = httpd_req_to_sockfd(request);
  if (fd < 0) {
    return std::nullopt;
  }
  sockaddr_storage storage = {};
  socklen_t length = sizeof(storage);
  if (getsockname(fd, reinterpret_cast<sockaddr*>(&storage), &length) != 0) {
    return std::nullopt;
  }
  if (storage.ss_family == AF_INET) {
    const auto* v4 = reinterpret_cast<const sockaddr_in*>(&storage);
    return from_network_order(v4->sin_addr.s_addr);
  }
  if (storage.ss_family == AF_INET6) {
    const auto* v6 = reinterpret_cast<const sockaddr_in6*>(&storage);
    if (!IN6_IS_ADDR_V4MAPPED(&v6->sin6_addr)) {
      // A native IPv6 address. Not an interface the configuration routes
      // expect, so the answer is "unknown" rather than a guess.
      return std::nullopt;
    }
    const std::uint8_t* bytes = v6->sin6_addr.s6_addr;
    return domain::Ipv4::of(bytes[12], bytes[13], bytes[14], bytes[15]);
  }
  return std::nullopt;
}

std::optional<domain::Ipv4> access_point_address() {
  esp_netif_t* ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (ap == nullptr) {
    return std::nullopt;
  }
  esp_netif_ip_info_t info = {};
  if (esp_netif_get_ip_info(ap, &info) != ESP_OK || info.ip.addr == 0) {
    return std::nullopt;
  }
  return from_network_order(info.ip.addr);
}

bool provisioning_request_allowed(httpd_req_t* request) {
  const std::optional<domain::Ipv4> ap = access_point_address();
  if (!ap.has_value()) {
    return false;  // with no access point interface there is no way in
  }
  return domain::provisioning_allowed(net::status().access_point_up, *ap,
                                      request_local_address(request));
}

}  // namespace stackchan::deviceapi
