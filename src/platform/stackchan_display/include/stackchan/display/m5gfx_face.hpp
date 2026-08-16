#pragma once

#include <M5GFX.h>

#include <atomic>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "stackchan/ports/face.hpp"

// The face, drawn with M5GFX. This is the ports::Face implementation.
//
// It is also the layer boundary: nothing above this knows the name M5GFX,
// and identifying which LCD controller is fitted is left to it as well.
//
// The look follows M5Stack-Avatar (Copyright (c) 2018 Shinya Ishikawa, MIT;
// see NOTICE). The library itself is not used — it brings Arduino and two
// drawing tasks with it — so the same geometry and timing are reproduced
// here and driven from the main loop through tick().
//
//   geometry    the default Avatar face on a 320x240 virtual canvas: eyes
//               at (90,93) and (230,96) with radius 8, mouth at (163,148)
//               varying 50-90 wide and 4-60 tall. Mapped to this screen as
//               screen = (canvas - (160,120)) * 0.45 + (60,48)
//   expression  made entirely from the shape of the eyes: a half-moon for
//               happy and sleepy, a triangular lid for angry and sad.
//               White on black
//   motion      a blink every 2.5-4.4 s lasting 0.3-0.49 s, the gaze
//               drifting +/-3 px every 0.5-2.4 s, and breathing moving
//               things 3 px over about 3.3 s
//
// Three tasks reach this: the main loop (tick, and switching screens), the
// HTTP server (setting an expression), and the conversation (per-sentence
// expression and the mouth). M5GFX is not safe against concurrent use, so
// drawing is serialised behind a lock. Everything else only writes atomics;
// the drawing itself happens solely in tick(), on the main loop.

namespace stackchan::display {

class M5GfxFace final : public ports::Face {
 public:
  M5GfxFace() = default;

  // Initialise the display. False if no panel was found — in which case
  // the firmware carries on, simply without a face.
  [[nodiscard]] bool begin(std::uint8_t brightness = 96);

  [[nodiscard]] bool available() const noexcept { return available_; }

  // Which panel was identified, for diagnostics.
  [[nodiscard]] int width() const noexcept;
  [[nodiscard]] int height() const noexcept;
  [[nodiscard]] int board_id() const noexcept;

  // Call periodically from the main loop. Advances the blink, gaze,
  // breathing and mouth, and redraws only while the face is on screen.
  // Calling it more often than the 33 ms frame interval is harmless; the
  // extra calls are ignored.
  void tick(std::uint32_t now_ms);

  void show(domain::Expression expression) override;
  void set_talking(bool talking) override;
  void show_message(std::string_view text) override;
  void show_pairing(std::string_view ssid, std::string_view password,
                    std::string_view url) override;

 private:
  enum class Mode : std::uint8_t { face, message, pairing };

  // Wraps one redraw. Nothing else is ever held at the same time, so it
  // cannot deadlock.
  struct Lock {
    explicit Lock(SemaphoreHandle_t handle) : handle_(handle) {
      xSemaphoreTake(handle_, portMAX_DELAY);
    }
    ~Lock() { xSemaphoreGive(handle_); }
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    Lock(Lock&&) = delete;
    Lock& operator=(Lock&&) = delete;

   private:
    SemaphoreHandle_t handle_;
  };

  void draw_face(std::uint32_t now_ms);
  [[nodiscard]] std::uint32_t next_random() noexcept;

  M5GFX gfx_{};
  M5Canvas canvas_{&gfx_};
  bool available_ = false;
  SemaphoreHandle_t mutex_ = nullptr;

  // Other tasks only write these. Drawing happens in tick().
  std::atomic<domain::Expression> expression_{domain::Expression::neutral};
  std::atomic<bool> talking_{false};
  std::atomic<Mode> mode_{Mode::face};

  // The animation state, touched only by tick().
  std::uint32_t last_frame_ms_ = 0;
  std::uint32_t next_blink_ms_ = 0;
  bool eye_open_ = true;
  std::uint32_t next_saccade_ms_ = 0;
  float gaze_h_ = 0.0F;
  float gaze_v_ = 0.0F;
  std::uint8_t breath_step_ = 0;
  float mouth_ratio_ = 0.0F;

  // Any non-zero value will do: xorshift32 only requires that it is not zero,
  // and this drives nothing but the jitter between blinks. Every device starts
  // from the same seed, which is fine — two of them side by side blinking in
  // step is not a fault anyone has reported minding.
  std::uint32_t rand_state_ = 0x5EED1234u;
};

}  // namespace stackchan::display
