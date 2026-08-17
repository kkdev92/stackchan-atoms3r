#include "stackchan/display/m5gfx_face.hpp"

#include <cmath>
#include <string>

namespace stackchan::display {
namespace {

// The geometry of the default Avatar face, on its 320x240 virtual canvas.
constexpr float kEyeRightX = 90.0F;
constexpr float kEyeRightY = 93.0F;
constexpr float kEyeLeftX = 230.0F;
constexpr float kEyeLeftY = 96.0F;
constexpr float kEyeRadius = 8.0F;
constexpr float kMouthX = 163.0F;
constexpr float kMouthY = 148.0F;
constexpr float kMouthMinW = 50.0F;
constexpr float kMouthMaxW = 90.0F;
constexpr float kMouthMinH = 4.0F;
constexpr float kMouthMaxH = 60.0F;

// Scaled and positioned for this 128x128 screen: the canvas centre
// (160,120) maps to (60,48) at 0.45x.
constexpr float kScale = 0.45F;
constexpr float kCenterX = 160.0F;
constexpr float kCenterY = 120.0F;
constexpr float kOffsetX = 60.0F;
constexpr float kOffsetY = 48.0F;

// Animation timing.
constexpr std::uint32_t kFrameMs = 33;
constexpr std::uint32_t kBreathSteps = 100;  // 33 ms x 100, about 3.3 s

// M_PI is a POSIX extension rather than standard C++, so define it here.
constexpr float kPi = 3.14159265358979323846F;

[[nodiscard]] int sx(float canvas_x) noexcept {
  return static_cast<int>(std::lround((canvas_x - kCenterX) * kScale + kOffsetX));
}

[[nodiscard]] int sy(float canvas_y) noexcept {
  return static_cast<int>(std::lround((canvas_y - kCenterY) * kScale + kOffsetY));
}

[[nodiscard]] int sl(float length) noexcept {
  return static_cast<int>(std::lround(length * kScale));
}

// Draw one eye. The expression is made entirely from the eye's shape; the
// default face has no visible brows.
void draw_eye(M5Canvas& canvas, float canvas_x, float canvas_y, bool is_left,
              domain::Expression expression, bool open) {
  const int x = sx(canvas_x);
  const int y = sy(canvas_y);
  const int r = sl(kEyeRadius);

  if (!open) {
    // Blinking: a horizontal bar four units tall on the virtual canvas.
    canvas.fillRect(x - r, y - sl(2.0F), r * 2, sl(4.0F) > 0 ? sl(4.0F) : 2,
                    TFT_WHITE);
    return;
  }

  canvas.fillCircle(x, y, r, TFT_WHITE);

  const bool angry = expression == domain::Expression::angry;
  const bool sad = expression == domain::Expression::sad;
  if (angry || sad) {
    // A triangular lid over the top. Angry drops on the outside, sad on
    // the inside.
    const int x0 = x - r;
    const int y0 = y - r;
    const int x1 = x0 + r * 2;
    // Which corner drops depends on both the eye and the expression.
    const int x2 = (is_left != sad) ? x0 : x1;
    canvas.fillTriangle(x0, y0, x1, y0, x2, y0 + r, TFT_BLACK);
  }

  const bool happy = expression == domain::Expression::happy;
  const bool sleepy = expression == domain::Expression::sleepy;
  if (happy || sleepy) {
    int y0 = y - r;
    if (happy) {
      // Erase the lower half and the centre, leaving an upward crescent.
      y0 += r;
      canvas.fillCircle(x, y, static_cast<int>(std::lround(r / 1.5F)), TFT_BLACK);
    }
    canvas.fillRect(x - r, y0, r * 2 + sl(4.0F), r + sl(2.0F), TFT_BLACK);
  }
}

}  // namespace

bool M5GfxFace::begin(std::uint8_t brightness) {
  if (mutex_ == nullptr) {
    mutex_ = xSemaphoreCreateMutex();
  }
  available_ = mutex_ != nullptr && gfx_.init();
  if (available_) {
    gfx_.setBrightness(brightness);
    canvas_.setColorDepth(16);

    // The canvas stays in **internal RAM**. Do not call setPsram(true).
    //
    // It costs 32 KB of DMA-capable internal RAM, and moving it to PSRAM
    // would give that back — but it would also stop the transfer being a
    // DMA transfer.
    //
    // The check for "can this address be used for DMA" only accepts
    // internal SRAM, so a canvas in PSRAM fails it. The graphics library
    // then pushes the sprite without DMA, and the SPI driver falls back to
    // writing 32 KB sixty-four bytes at a time through the peripheral's
    // registers, waiting for each one.
    //
    // With DMA the write is set up and returns immediately, leaving the CPU
    // free. Without it, the CPU spins for the whole transfer — around 6.5 ms
    // — every frame. At a 33 ms frame interval that is roughly a fifth of a
    // core, given up for memory that is not scarce: the largest free
    // DMA-capable block stays around 96 KB with this canvas allocated.
    if (canvas_.createSprite(gfx_.width(), gfx_.height()) == nullptr) {
      // Without a canvas there is no face. Text screens draw directly, so
      // available stays true and tick simply does nothing.
      canvas_.deleteSprite();
    }
  }
  return available_;
}

int M5GfxFace::width() const noexcept {
  return available_ ? const_cast<M5GFX&>(gfx_).width() : 0;
}

int M5GfxFace::height() const noexcept {
  return available_ ? const_cast<M5GFX&>(gfx_).height() : 0;
}

int M5GfxFace::board_id() const noexcept {
  return available_ ? static_cast<int>(const_cast<M5GFX&>(gfx_).getBoard()) : -1;
}

std::uint32_t M5GfxFace::next_random() noexcept {
  // xorshift32, for the jitter in the blink. Quality is irrelevant here.
  rand_state_ ^= rand_state_ << 13;
  rand_state_ ^= rand_state_ >> 17;
  rand_state_ ^= rand_state_ << 5;
  return rand_state_;
}

void M5GfxFace::tick(std::uint32_t now_ms) {
  if (!available_ || mode_.load(std::memory_order_acquire) != Mode::face) {
    return;
  }
  if (now_ms - last_frame_ms_ < kFrameMs) {
    return;
  }
  last_frame_ms_ = now_ms;

  // Start counting from an open-eyed interval, so the face does not blink
  // the instant it appears.
  if (next_blink_ms_ == 0) {
    next_blink_ms_ = now_ms + 2500;
  }

  // Blinking: eyes open for 2500-4400 ms, closed for 300-490 ms.
  if (now_ms >= next_blink_ms_) {
    eye_open_ = !eye_open_;
    next_blink_ms_ =
        now_ms + (eye_open_ ? 2500 + 100 * (next_random() % 20)
                            : 300 + 10 * (next_random() % 20));
  }

  // The gaze drifts to a new direction every 0.5-2.4 s.
  if (now_ms >= next_saccade_ms_) {
    gaze_h_ = static_cast<float>(next_random() % 2001) / 1000.0F - 1.0F;
    gaze_v_ = static_cast<float>(next_random() % 2001) / 1000.0F - 1.0F;
    next_saccade_ms_ = now_ms + 500 + 100 * (next_random() % 20);
  }

  // Breathing: a sine with a period of about 3.3 s.
  breath_step_ = static_cast<std::uint8_t>((breath_step_ + 1) % kBreathSteps);

  // The mouth opens and closes about every 0.4 s while speaking.
  // Approximated by a cycle rather than driven by the audio's amplitude,
  // which would need the volume plumbed through from playback. It reads as
  // talking either way.
  if (talking_.load(std::memory_order_acquire)) {
    const float phase = static_cast<float>(now_ms % 400) * 2.0F * kPi / 400.0F;
    mouth_ratio_ = 0.35F - 0.35F * std::cos(phase);
  } else {
    mouth_ratio_ = 0.0F;
  }

  draw_face(now_ms);
}

void M5GfxFace::draw_face(std::uint32_t) {
  if (canvas_.getBuffer() == nullptr) {
    return;
  }
  const Lock lock{mutex_};
  // If a text screen appeared while waiting for the lock, do not draw the
  // face over it.
  if (mode_.load(std::memory_order_acquire) != Mode::face) {
    return;
  }

  const auto expression = expression_.load(std::memory_order_acquire);
  const float breath = std::sin(static_cast<float>(breath_step_) * 2.0F * kPi /
                                static_cast<float>(kBreathSteps));

  canvas_.fillSprite(TFT_BLACK);

  // The eyes move with the gaze and with breathing, the mouth with
  // breathing, all measured on the virtual canvas.
  const float eye_dx = gaze_h_ * 3.0F;
  const float eye_dy = gaze_v_ * 3.0F + breath * 3.0F;
  draw_eye(canvas_, kEyeRightX + eye_dx, kEyeRightY + eye_dy, false, expression,
           eye_open_);
  draw_eye(canvas_, kEyeLeftX + eye_dx, kEyeLeftY + eye_dy, true, expression,
           eye_open_);

  // The mouth grows taller and narrower as it opens.
  const float open = mouth_ratio_;
  const int mouth_w = sl(kMouthMinW + (kMouthMaxW - kMouthMinW) * (1.0F - open));
  const int mouth_h = sl(kMouthMinH + (kMouthMaxH - kMouthMinH) * open);
  const int mouth_x = sx(kMouthX) - mouth_w / 2;
  const int mouth_y =
      sy(kMouthY + breath * 5.0F) - mouth_h / 2;
  canvas_.fillRect(mouth_x, mouth_y, mouth_w, mouth_h > 0 ? mouth_h : 1,
                   TFT_WHITE);

  canvas_.pushSprite(0, 0);
}

void M5GfxFace::show(domain::Expression expression) {
  // Only the state is written here; drawing happens in tick() on the main
  // loop. So a call from another task never has to wait for the display,
  // and the next frame is at most 33 ms away, which is not a visible delay.
  expression_.store(expression, std::memory_order_release);
  mode_.store(Mode::face, std::memory_order_release);
}

void M5GfxFace::set_talking(bool talking) {
  talking_.store(talking, std::memory_order_release);
}

void M5GfxFace::show_message(std::string_view text) {
  if (!available_) {
    return;
  }
  mode_.store(Mode::message, std::memory_order_release);
  const Lock lock{mutex_};

  const int w = gfx_.width();
  const int h = gfx_.height();

  gfx_.fillScreen(TFT_DARKGREEN);
  gfx_.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  gfx_.setTextDatum(middle_center);

  // Pick the largest size that fits.
  //
  // A fixed size cannot serve both: sized for "OK" and an address like
  // "198.51.100.17" runs off the screen; sized for the address and short
  // words become hard to read. The default font is 6x8 pixels per
  // character, so at scale n each character is 6n wide.
  const int usable = w - 8;  // leave a small margin at each side
  int size = 4;
  while (size > 1 && static_cast<int>(text.size()) * 6 * size > usable) {
    --size;
  }
  gfx_.setTextSize(static_cast<float>(size));

  // drawString needs a null-terminated string, which string_view is not.
  const std::string owned{text};
  gfx_.drawString(owned.c_str(), w / 2, h / 2);
}

void M5GfxFace::show_pairing(std::string_view ssid, std::string_view password,
                             std::string_view url) {
  if (!available_) {
    return;
  }
  mode_.store(Mode::pairing, std::memory_order_release);
  const Lock lock{mutex_};

  // ASCII only, because the default font has no other glyphs. At 128 px
  // wide that is about 21 characters at scale 1, or 10 at scale 2.
  gfx_.fillScreen(TFT_BLACK);
  gfx_.setTextDatum(top_left);

  gfx_.setTextSize(2);
  gfx_.setTextColor(TFT_CYAN, TFT_BLACK);
  gfx_.drawString("SETUP", 4, 4);

  gfx_.setTextSize(1);
  gfx_.setTextColor(TFT_WHITE, TFT_BLACK);
  gfx_.drawString("Wi-Fi:", 4, 30);
  const std::string owned_ssid{ssid};
  gfx_.drawString(owned_ssid.c_str(), 4, 40);

  gfx_.drawString("KEY:", 4, 58);
  // The passphrase is shown largest: mistyping it means not getting in.
  gfx_.setTextSize(2);
  gfx_.setTextColor(TFT_GREENYELLOW, TFT_BLACK);
  const std::string owned_password{password};
  gfx_.drawString(owned_password.c_str(), 4, 68);

  gfx_.setTextSize(1);
  gfx_.setTextColor(TFT_WHITE, TFT_BLACK);
  gfx_.drawString("open:", 4, 92);
  const std::string owned_url{url};
  gfx_.drawString(owned_url.c_str(), 4, 102);
}

}  // namespace stackchan::display
