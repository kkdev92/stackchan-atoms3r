#pragma once

#include <array>
#include <cstdio>
#include <optional>
#include <string_view>

#include "esp_log.h"
#include "esp_timer.h"
#include "stackchan/app/playback_stager.hpp"
#include "stackchan/app/request_router.hpp"
#include "stackchan/domain/access_token.hpp"
#include "stackchan/domain/audio_direction.hpp"
#include "stackchan/domain/provisioning_scope.hpp"

// Self-checks run at startup.
//
// They confirm on the device that decisions fixed by the host tests behave
// the same when built with the target toolchain. Their output is also what
// the emulator's boot check greps for, which is how the whole decision
// layer gets exercised on a machine that has neither a display nor audio.
//
// Details of any failure belong to the host tests. These touch only the
// essentials.

namespace stackchan::selftest {
namespace detail {

[[nodiscard]] inline bool contains(std::string_view text, std::string_view needle) {
  return text.find(needle) != std::string_view::npos;
}

// RequestRouter: authorise, interpret, dispatch, wrap.
//
// The command used is device.describe, which does not depend on hardware.
// Anything that does — the display, audio — is advertised as unavailable on
// a unit without it, which is invariant 3 working correctly rather than a
// failed check.
[[nodiscard]] inline bool router_ok(app::RequestRouter& router,
                                    std::string_view token) {
  static std::array<char, 96> body{};
  std::snprintf(body.data(), body.size(), R"({"id":"st-1","name":"device.describe"})");
  const std::string_view ok = router.route(token, std::string_view{body.data()});
  if (!contains(ok, R"("ok":true)") || !contains(ok, R"("device_id")") ||
      !contains(ok, R"("capabilities")")) {
    ESP_LOGE("selftest", "router ok-path failed: %.*s", static_cast<int>(ok.size()),
             ok.data());
    return false;
  }
  const std::string_view denied =
      router.route("WRONGTOKEN", std::string_view{body.data()});
  if (!contains(denied, R"("code":"bad_request")")) {
    return false;
  }
  const std::string_view unknown =
      router.route(token, R"({"id":"st-2","name":"no.such"})");
  return contains(unknown, R"("code":"unknown_command")");
}

// AudioDirection: the half-duplex rule.
[[nodiscard]] inline bool direction_ok() {
  domain::AudioDirection direction;
  using domain::AudioTransition;
  if (direction.begin_capture() != AudioTransition::proceed) {
    return false;
  }
  if (direction.begin_playback() != AudioTransition::denied) {
    return false;  // the half-duplex rule has been broken
  }
  direction.end_capture();
  return direction.begin_playback() == AudioTransition::proceed;
}

// ProvisioningScope: only requests arriving on the access point pass.
[[nodiscard]] inline bool scope_ok() {
  using domain::Ipv4;
  const Ipv4 ap = Ipv4::of(192, 168, 4, 1);
  const bool allow = domain::provisioning_allowed(true, ap, ap);
  const bool deny_sta =
      domain::provisioning_allowed(true, ap, Ipv4::of(192, 168, 1, 7));
  const bool deny_unknown = domain::provisioning_allowed(true, ap, std::nullopt);
  const bool deny_down = domain::provisioning_allowed(false, ap, ap);
  return allow && !deny_sta && !deny_unknown && !deny_down;
}

// PlaybackStager: nothing is played until a whole sentence is ready, and
// the expression changes with the sound.
[[nodiscard]] inline bool stager_ok() {
  using domain::Expression;
  static std::array<ports::Sample, 32> stage{};
  app::PlaybackStager stager{stage.data(), stage.size(), 240};
  std::array<ports::Sample, 10> chunk{};

  if (stager.begin_sentence(Expression::happy) != 0) {
    return false;
  }
  if (stager.accept(chunk.data(), chunk.size()) != chunk.size()) {
    return false;
  }
  // At the second sentence's boundary, the answer must be "play the first
  // one, still wearing happy".
  if (stager.begin_sentence(Expression::sad) != chunk.size()) {
    return false;
  }
  if (stager.expression_to_speak() != Expression::happy) {
    return false;  // the expression is running a sentence ahead
  }
  stager.flushed(static_cast<std::uint32_t>(esp_timer_get_time() / 1000));
  if (stager.expression_to_speak() != Expression::sad) {
    return false;
  }
  if (stager.accept(chunk.data(), 5) != 5) {
    return false;
  }
  return stager.finish(true) == 5;
}

}  // namespace detail

// Run them all and log the results. The emulator's boot check greps for
// these five lines.
[[nodiscard]] inline bool run(app::RequestRouter& router,
                                        const domain::AccessToken& token) {
  const bool router_ok = token.is_set() && detail::router_ok(router, token.text());
  const bool direction_ok = detail::direction_ok();
  const bool scope_ok = detail::scope_ok();
  const bool stager_ok = detail::stager_ok();
  ESP_LOGI("selftest", "selftest router: %s", router_ok ? "ok" : "NG");
  ESP_LOGI("selftest", "selftest audio-direction: %s", direction_ok ? "ok" : "NG");
  ESP_LOGI("selftest", "selftest provisioning-scope: %s", scope_ok ? "ok" : "NG");
  ESP_LOGI("selftest", "selftest playback-stager: %s", stager_ok ? "ok" : "NG");
  const bool all = router_ok && direction_ok && scope_ok && stager_ok;
  if (all) {
    ESP_LOGI("selftest", "selftest: all ok");
  } else {
    ESP_LOGE("selftest", "selftest: FAILED");
  }
  return all;
}

}  // namespace stackchan::selftest
