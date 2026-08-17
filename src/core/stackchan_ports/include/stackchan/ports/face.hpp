#pragma once

#include <string_view>

#include "stackchan/domain/expression.hpp"

// How the face is shown.
//
// Callers know nothing about pixels or drivers. They only say "show this
// expression". The implementation lives in platform/stackchan_display and
// uses M5GFX.
//
// Keeping this abstraction in core while the implementation sits in
// platform is what holds the layering together. The dependency points from
// the implementation to Face, so the display driver can be replaced without
// touching code above it.

namespace stackchan::ports {

class Face {
 public:
  virtual ~Face() = default;

  // Implementations own hardware (the screen), so neither copying nor
  // moving is allowed. Pass by reference. All four have to be declared
  // explicitly to satisfy the rule of five.
  Face(const Face&) = delete;
  Face& operator=(const Face&) = delete;
  Face(Face&&) = delete;
  Face& operator=(Face&&) = delete;

  // Show an expression.
  virtual void show(domain::Expression expression) = 0;

  // Tell the face whether speech is in progress; the implementation moves
  // the mouth accordingly. The conversation task toggles this as playback
  // starts and finishes.
  virtual void set_talking(bool talking) = 0;

  // Show a short string, such as an acknowledgement.
  virtual void show_message(std::string_view text) = 0;

  // Show the setup screen: the access point's name, its passphrase, and
  // where to go.
  virtual void show_pairing(std::string_view ssid, std::string_view password,
                            std::string_view url) = 0;

 protected:
  Face() = default;
};

}  // namespace stackchan::ports
