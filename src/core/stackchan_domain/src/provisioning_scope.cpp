#include "stackchan/domain/provisioning_scope.hpp"

namespace stackchan::domain {

bool provisioning_allowed(bool access_point_up, const Ipv4& access_point_address,
                          const std::optional<Ipv4>& request_local_address) noexcept {
  // With the access point down, refuse even a matching address: that would
  // be a connection left over from before it closed.
  if (!access_point_up) {
    return false;
  }
  // A request whose arrival interface is unknown is refused.
  if (!request_local_address.has_value()) {
    return false;
  }
  return *request_local_address == access_point_address;
}

}  // namespace stackchan::domain
