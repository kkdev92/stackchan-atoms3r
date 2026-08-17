#include "stackchan/app/device_describe.hpp"

namespace stackchan::app {

void write_device_description(domain::JsonWriter& out, const DeviceIdentity& identity,
                             const CommandRegistry& registry,
                             std::uint64_t uptime_ms) noexcept {
  out.begin_object();

  // Who this is. device_id survives restarts; boot_id does not.
  out.member("device_id", identity.device_id);
  out.member("boot_id", identity.boot_id.text());

  // How long it has been running. 64-bit, so it does not wrap. Combined
  // with boot_id, the other end can tell a restart from a long uptime
  // without inferring anything from the number going backwards.
  out.member("uptime_ms", uptime_ms);

  out.key("firmware");
  out.begin_object();
  out.member("version", identity.firmware_version);
  out.member("idf", identity.firmware_idf);
  out.member("build", identity.firmware_build);
  out.end_object();

  out.member("protocol", static_cast<std::uint64_t>(domain::kProtocolVersion));

  // What it can do, generated from what is registered. No hand-written
  // list exists.
  registry.write_capabilities(out);

  out.end_object();
}

}  // namespace stackchan::app
