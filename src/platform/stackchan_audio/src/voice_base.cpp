#include "stackchan/audio/voice_base.hpp"

#include <array>
#include <cmath>
#include <cstring>

#include "driver/i2c_master.h"
#include "driver/i2s_std.h"
#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace stackchan::audio {
namespace {

constexpr char kTag[] = "audio";

// Serialises changes of direction.
//
// Recording and playback cannot both be open. start_capture and
// start_playback each refuse while the other is running — but called from
// two tasks at once, both can look, both see the other stopped, and both
// proceed. The lock keeps the half-duplex transition atomic.
//
// There really are two callers: the conversation task, which records and
// speaks, and the main loop, which plays short confirmation tones for the
// button. Only the change of direction needs protecting, so read and write
// do not take the lock — by then the caller owns the direction.
//
// The lock is recursive because play_tone calls start_playback.
SemaphoreHandle_t g_direction_lock = nullptr;

struct DirectionLock {
  DirectionLock() {
    if (g_direction_lock != nullptr) {
      xSemaphoreTakeRecursive(g_direction_lock, portMAX_DELAY);
    }
  }
  ~DirectionLock() {
    if (g_direction_lock != nullptr) {
      xSemaphoreGiveRecursive(g_direction_lock);
    }
  }
  DirectionLock(const DirectionLock&) = delete;
  DirectionLock& operator=(const DirectionLock&) = delete;
  DirectionLock(DirectionLock&&) = delete;
  DirectionLock& operator=(DirectionLock&&) = delete;
};

constexpr int kI2cScl = 39;
constexpr int kI2cSda = 38;
constexpr int kI2sDin = 5;   // device to codec (playback)
constexpr int kI2sLrck = 6;
constexpr int kI2sDout = 7;  // codec to device (recording)
constexpr int kI2sBclk = 8;

// The codec's address, which is its default when the chip-enable pin is low.
constexpr std::uint8_t kCodecAddress = 0x18;

// This shares the same I2C bus that the startup scan uses. The chip has
// only two, and the other one belongs to the display driver for backlight
// control.
//
// It uses the newer I2C API, as the scan does. Old and new cannot coexist
// in one binary: mixing them aborts during startup with "driver_ng is not
// allowed to be used with this old driver", which turns into a boot loop.
constexpr i2c_port_num_t kI2cPort = I2C_NUM_0;

// No MCLK. This board does not wire it.

// I2C addresses, both confirmed by scanning a real board.
//
//   0x18  the ES8311 codec (its version registers read 0x83 / 0x11)
//   0x43  a PI4IOE I/O expander, which switches the speaker amplifier
constexpr std::uint8_t kExpanderAddress = 0x43;

// Codec register sequences. This board has no MCLK line, so the codec uses
// BCLK as its clock source. At 16 kHz with 16-bit slots BCLK is 512 kHz; the
// sequences below configure that combination with a fixed pre-multiplier.

// For recording. Note register 0x01.
constexpr std::uint8_t kCodecForCapture[][2] = {
    {0x00, 0x80},  // reset, power up the state machine
    {0x01, 0xBA},  // clock manager: MCLK taken from BCLK
    {0x02, 0x18},  // clock manager: pre-multiplier of 3
    {0x0D, 0x01},  // power up the analogue section
    {0x0E, 0x02},  // enable the analogue PGA and the ADC modulator
    {0x14, 0x10},  // select the differential microphone input.
                   // Without this line nothing is connected to the ADC
    {0x17, 0xFF},  // ADC gain at maximum (0xBF would be unity)
    {0x1C, 0x6A},  // bypass the equaliser, remove the DC component
};

// For playback. Register 0x01 differs from the recording value, and that
// difference is what "cannot be used at the same time" really means.
constexpr std::uint8_t kCodecForPlayback[][2] = {
    {0x00, 0x80},  // reset, power up the state machine
    {0x01, 0xB5},  // clock manager: MCLK taken from BCLK
    {0x02, 0x18},  // clock manager: pre-multiplier of 3
    {0x0D, 0x01},  // power up the analogue section
    {0x12, 0x00},  // power up the DAC
    {0x13, 0x10},  // route the output to the headphone driver
    {0x32, 0xFF},  // DAC volume at maximum; set_volume lowers it
    {0x37, 0x08},  // bypass the equaliser
};

// Switch on the speaker amplifier. Without this, nothing is audible.
constexpr std::uint8_t kExpanderForPlayback[][2] = {
    {0x03, 0xFF},  // direction: output
    {0x05, 0xFF},  // drive the outputs high
    {0x07, 0x00},  // push-pull
    {0x0B, 0x00},  // no pull-up or pull-down
};

std::uint32_t g_sample_rate = kVoiceSampleRate;

i2c_master_bus_handle_t g_i2c = nullptr;
i2c_master_dev_handle_t g_codec = nullptr;
i2c_master_dev_handle_t g_expander = nullptr;
i2s_chan_handle_t g_tx = nullptr;
i2s_chan_handle_t g_rx = nullptr;

[[nodiscard]] bool open_i2c() {
  if (g_i2c != nullptr) {
    return true;
  }
  i2c_master_bus_config_t config = {};
  config.i2c_port = kI2cPort;
  config.scl_io_num = static_cast<gpio_num_t>(kI2cScl);
  config.sda_io_num = static_cast<gpio_num_t>(kI2cSda);
  config.clk_source = I2C_CLK_SRC_DEFAULT;
  config.glitch_ignore_cnt = 7;
  config.flags.enable_internal_pullup = true;

  const esp_err_t err = i2c_new_master_bus(&config, &g_i2c);
  if (err != ESP_OK) {
    ESP_LOGE(kTag, "i2c for the codec could not be opened: %s", esp_err_to_name(err));
    g_i2c = nullptr;
    return false;
  }
  return true;
}

void close_i2s() {
  if (g_rx != nullptr) {
    (void)i2s_channel_disable(g_rx);
    (void)i2s_del_channel(g_rx);
    g_rx = nullptr;
  }
  if (g_tx != nullptr) {
    (void)i2s_channel_disable(g_tx);
    (void)i2s_del_channel(g_tx);
    g_tx = nullptr;
  }
}

// Create a channel for the direction in use, and only that one.
//
// The microphone and speaker do not run together, so create only the channel
// for the active direction.
[[nodiscard]] bool open_i2s(bool for_capture) {
  close_i2s();

  i2s_chan_config_t channel = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);

  // Buffer 240 ms: eight descriptors of 480 samples each, at 16 kHz.
  //
  // Playback can only write what has arrived. Audio comes in events of
  // roughly 128 ms, sent at about the speed they are spoken. With only 90 ms
  // buffered, the buffer empties before the next event lands; silence gets
  // substituted, and the result crackles audibly. Holding two events' worth
  // absorbs the jitter.
  //
  // A single descriptor cannot exceed 4092 bytes. 480 samples across two
  // slots at two bytes each is 1920, which fits. The whole set costs about
  // 15 KB of internal RAM.
  channel.dma_desc_num = 8;
  channel.dma_frame_num = 480;
  channel.auto_clear = true;  // emit silence when nothing is written

  const esp_err_t created =
      for_capture ? i2s_new_channel(&channel, nullptr, &g_rx)
                  : i2s_new_channel(&channel, &g_tx, nullptr);
  if (created != ESP_OK) {
    ESP_LOGE(kTag, "i2s channel could not be created: %s", esp_err_to_name(created));
    g_tx = nullptr;
    g_rx = nullptr;
    return false;
  }

  i2s_std_config_t std_config = {};
  std_config.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(g_sample_rate);
  std_config.slot_cfg =
      I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
  // 16-bit slots, which makes the bit clock 16000 * 16 * 2 = 512000 Hz.
  std_config.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
  std_config.slot_cfg.ws_width = 16;
  // left_align is set to match what this codec expects.
  //
  // The related msb_right setting cannot be used here: it exists only on
  // the older I2S hardware and is absent from this chip's structure.
  std_config.slot_cfg.left_align = true;
  // Recording takes the left slot only.
  //
  // The default macro on this chip leaves the slot mask set to both even
  // when mono is requested. Left as both, the codec's identical sample in
  // each slot is read twice, so the stream runs at double rate: three
  // seconds of speech ends after 1.5, with every sample duplicated.
  //
  // Playback wants both, so that mono is copied to each slot.
  if (for_capture) {
    std_config.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;
  }
  // No MCLK is emitted, because this board does not wire it.
  std_config.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  std_config.gpio_cfg.bclk = static_cast<gpio_num_t>(kI2sBclk);
  std_config.gpio_cfg.ws = static_cast<gpio_num_t>(kI2sLrck);
  std_config.gpio_cfg.dout =
      for_capture ? I2S_GPIO_UNUSED : static_cast<gpio_num_t>(kI2sDin);
  std_config.gpio_cfg.din =
      for_capture ? static_cast<gpio_num_t>(kI2sDout) : I2S_GPIO_UNUSED;

  const esp_err_t configured =
      i2s_channel_init_std_mode(for_capture ? g_rx : g_tx, &std_config);
  if (configured != ESP_OK) {
    ESP_LOGE(kTag, "i2s channel could not be configured: %s",
             esp_err_to_name(configured));
    close_i2s();
    return false;
  }
  return true;
}

// Root mean square: the loudness of what was recorded, as one number.
[[nodiscard]] bool add_device(std::uint8_t address, i2c_master_dev_handle_t* out) {
  i2c_device_config_t config = {};
  config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
  config.device_address = address;
  config.scl_speed_hz = 100000;
  return i2c_master_bus_add_device(g_i2c, &config, out) == ESP_OK;
}

// Write a sequence of registers, stopping at the first failure.
template <std::size_t N>
[[nodiscard]] bool write_registers(i2c_master_dev_handle_t device,
                                   const std::uint8_t (&pairs)[N][2]) {
  if (device == nullptr) {
    return false;
  }
  for (std::size_t i = 0; i < N; ++i) {
    if (i2c_master_transmit(device, pairs[i], 2, 100) != ESP_OK) {
      ESP_LOGE(kTag, "register 0x%02x could not be written",
               static_cast<unsigned>(pairs[i][0]));
      return false;
    }
  }
  return true;
}

// Floating point here is **single precision only**.
//
// This chip's FPU does single precision and nothing else. Write a double
// and the compiler emits calls into libgcc's software implementations,
// which are enormously slower than the instruction they replace. The same
// expression written in float compiles to hardware arithmetic.
[[nodiscard]] std::uint16_t rms_of(const ports::Sample* samples, std::size_t count) {
  if (count == 0) {
    return 0;
  }
  std::uint64_t total = 0;
  for (std::size_t i = 0; i < count; ++i) {
    const std::int32_t value = samples[i];
    // One term peaks at 32768 squared, which still fits in 32 bits;
    // the accumulator is 64.
    total += static_cast<std::uint64_t>(value * value);
  }
  // The mean square reaches 2^30, beyond float's exact integer range, but
  // this is a loudness indication and the low digits do not matter.
  return static_cast<std::uint16_t>(std::sqrt(static_cast<float>(total / count)));
}

}  // namespace

bool VoiceBase::begin() {
  if (available_) {
    return true;
  }
  if (g_direction_lock == nullptr) {
    g_direction_lock = xSemaphoreCreateRecursiveMutex();
    if (g_direction_lock == nullptr) {
      ESP_LOGE(kTag, "the direction lock could not be created");
      return false;
    }
  }
  if (!open_i2c()) {
    return false;
  }
  if (!add_device(kCodecAddress, &g_codec)) {
    ESP_LOGE(kTag, "codec at 0x%02x could not be registered",
             static_cast<unsigned>(kCodecAddress));
    return false;
  }

  // Confirm the codec is actually there.
  //
  // Registering an address does not test device presence. Read the ES8311
  // version registers before advertising audio as available.
  std::uint8_t id_high = 0;
  std::uint8_t id_low = 0;
  const std::uint8_t reg_high = 0xFD;
  const std::uint8_t reg_low = 0xFE;
  const bool answered =
      i2c_master_transmit_receive(g_codec, &reg_high, 1, &id_high, 1, 100) == ESP_OK &&
      i2c_master_transmit_receive(g_codec, &reg_low, 1, &id_low, 1, 100) == ESP_OK;
  if (!answered || id_high != 0x83 || id_low != 0x11) {
    ESP_LOGW(kTag, "no es8311 at 0x%02x (read %02x%02x); audio is unavailable",
             static_cast<unsigned>(kCodecAddress), static_cast<unsigned>(id_high),
             static_cast<unsigned>(id_low));
    (void)i2c_master_bus_rm_device(g_codec);
    g_codec = nullptr;
    return false;
  }
  // Some units may have no expander, so carry on if it is missing. Such a
  // unit can record but not play.
  if (!add_device(kExpanderAddress, &g_expander)) {
    ESP_LOGW(kTag, "expander at 0x%02x not present; playback may be silent",
             static_cast<unsigned>(kExpanderAddress));
    g_expander = nullptr;
  }

  // I2S is not created here. It is created once a direction is chosen.
  available_ = true;
  ESP_LOGI(kTag, "voice base ready (es8311 at 0x%02x, %u Hz, half duplex)",
           static_cast<unsigned>(kCodecAddress), static_cast<unsigned>(sample_rate_));
  return true;
}

bool VoiceBase::start_capture(std::uint32_t sample_rate) {
  if (!available_ || sample_rate != sample_rate_) {
    return false;
  }
  const DirectionLock lock;
  // What is permitted is decided by the state table. When it refuses, do
  // not quietly stop playback: a caller that started recording mid-sentence
  // would then believe both directions are running.
  switch (direction_.begin_capture()) {
    case domain::AudioTransition::already_active:
      return true;
    case domain::AudioTransition::denied:
      ESP_LOGW(kTag, "cannot capture while playing; stop playback first");
      return false;
    case domain::AudioTransition::proceed:
      break;
  }
  // Reconfigure the codec for recording; the register values differ from
  // playback.
  if (!write_registers(g_codec, kCodecForCapture)) {
    direction_.end_capture();  // could not proceed, so undo the transition
    return false;
  }
  g_sample_rate = sample_rate;
  if (!open_i2s(true)) {
    direction_.end_capture();
    return false;
  }
  if (i2s_channel_enable(g_rx) != ESP_OK) {
    ESP_LOGE(kTag, "capture could not be started");
    close_i2s();
    direction_.end_capture();
    return false;
  }
  return true;
}

void VoiceBase::stop_capture() {
  const DirectionLock lock;
  if (!direction_.capturing()) {
    return;
  }
  close_i2s();
  direction_.end_capture();
}

std::size_t VoiceBase::read(ports::Sample* out, std::size_t count,
                            runtime::Deadline deadline, std::uint32_t now_ms) {
  if (!direction_.capturing() || out == nullptr || count == 0) {
    return 0;
  }
  // Wait only until the deadline. No unbounded wait exists (invariant 4).
  const std::uint32_t wait_ms = deadline.remaining(now_ms);
  const TickType_t ticks =
      wait_ms == runtime::Deadline::kUnbounded ? portMAX_DELAY : pdMS_TO_TICKS(wait_ms);

  std::size_t read_bytes = 0;
  const esp_err_t err = i2s_channel_read(g_rx, out, count * sizeof(ports::Sample),
                                         &read_bytes, ticks);
  if (err != ESP_OK) {
    // Report what went wrong: zero samples from a timeout and zero samples
    // from a wrong state need different fixes.
    ESP_LOGW(kTag, "read: %s (asked %u samples, wait %ums, got %u bytes)",
             esp_err_to_name(err), static_cast<unsigned>(count),
             static_cast<unsigned>(wait_ms), static_cast<unsigned>(read_bytes));
  }
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
    return 0;
  }
  return read_bytes / sizeof(ports::Sample);
}

bool VoiceBase::start_playback(std::uint32_t sample_rate) {
  if (!available_ || sample_rate != sample_rate_) {
    return false;
  }
  const DirectionLock lock;
  switch (direction_.begin_playback()) {
    case domain::AudioTransition::already_active:
      return true;
    case domain::AudioTransition::denied:
      ESP_LOGW(kTag, "cannot play while capturing; stop capture first");
      return false;
    case domain::AudioTransition::proceed:
      break;
  }
  // Reconfigure the codec for playback; register 0x01 differs from
  // recording.
  if (!write_registers(g_codec, kCodecForPlayback)) {
    direction_.end_playback();
    return false;
  }
  // Power the amplifier: after the codec, before I2S.
  //
  // This order matters and is the one the reference driver uses.
  if (g_expander != nullptr && !write_registers(g_expander, kExpanderForPlayback)) {
    ESP_LOGW(kTag, "amplifier could not be enabled");
  }

  // The register sequence sets maximum gain, so restore the configured
  // volume here rather than amplifying.
  set_volume(volume_);

  g_sample_rate = sample_rate;
  if (!open_i2s(false)) {
    direction_.end_playback();
    return false;
  }
  if (i2s_channel_enable(g_tx) != ESP_OK) {
    ESP_LOGE(kTag, "playback could not be started");
    close_i2s();
    direction_.end_playback();
    return false;
  }
  return true;
}

void VoiceBase::stop_playback() {
  const DirectionLock lock;
  if (!direction_.playing()) {
    return;
  }
  // Cut the amplifier first. Stopping I2S first would send whatever
  // undefined output follows straight to the speaker.
  if (g_expander != nullptr) {
    const std::uint8_t off[2] = {0x05, 0x00};
    (void)i2c_master_transmit(g_expander, off, 2, 100);
    vTaskDelay(pdMS_TO_TICKS(10));
  }
  close_i2s();
  direction_.end_playback();
}

std::size_t VoiceBase::write(const ports::Sample* samples, std::size_t count,
                             runtime::Deadline deadline, std::uint32_t now_ms) {
  if (!direction_.playing() || samples == nullptr || count == 0) {
    return 0;
  }
  const std::uint32_t wait_ms = deadline.remaining(now_ms);
  const TickType_t ticks =
      wait_ms == runtime::Deadline::kUnbounded ? portMAX_DELAY : pdMS_TO_TICKS(wait_ms);

  std::size_t written_bytes = 0;
  const esp_err_t err = i2s_channel_write(g_tx, samples, count * sizeof(ports::Sample),
                                          &written_bytes, ticks);
  if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
    return 0;
  }
  return written_bytes / sizeof(ports::Sample);
}

void VoiceBase::set_volume(std::uint8_t percent) {
  volume_ = percent > 100 ? 100 : percent;
  if (g_codec == nullptr) {
    return;
  }

  // The DAC volume register, in half-decibel steps, where 0xBF is unity
  // gain. It goes up to 0xFF, but everything past 0xBF is amplification and
  // is not used here.
  //
  // 100% maps to unity gain. The register scale is logarithmic, so volume is
  // mapped across the attenuation range rather than across the full register.
  constexpr std::uint8_t kUnityGain = 0xBF;
  const std::uint8_t level =
      static_cast<std::uint8_t>(kUnityGain * static_cast<int>(volume_) / 100);
  const std::uint8_t pair[2] = {0x32, level};
  (void)i2c_master_transmit(g_codec, pair, 2, 100);
}

std::size_t VoiceBase::play_tone(std::uint32_t hz, std::uint32_t duration_ms) {
  if (!available_) {
    return 0;
  }
  // Held for the whole tone. Without it, a recording request part-way
  // through would tear down I2S while it is still sounding — and there are
  // two callers that can do that.
  const DirectionLock lock;
  if (!start_playback(sample_rate_)) {
    return 0;
  }

  // Build one cycle's worth and repeat it, rather than allocating for the
  // whole duration. Single precision, for the reason given above rms_of.
  constexpr std::size_t kChunk = 800;  // 50 ms at 16 kHz
  constexpr float kTwoPi = 6.28318530717958647692F;
  static std::array<ports::Sample, kChunk> chunk{};
  const float step = kTwoPi * static_cast<float>(hz) / static_cast<float>(sample_rate_);
  for (std::size_t i = 0; i < chunk.size(); ++i) {
    // Modest amplitude: driving it to full scale distorts.
    chunk[i] =
        static_cast<ports::Sample>(std::sin(step * static_cast<float>(i)) * 6000.0F);
  }

  const auto now = [] {
    return static_cast<std::uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  };

  // Ramp the first and last block. Starting or stopping abruptly leaves a
  // step in the waveform, which is audible as a click.
  static std::array<ports::Sample, kChunk> shaped{};

  const std::size_t rounds = (duration_ms * sample_rate_ / 1000) / kChunk;
  std::size_t written = 0;
  for (std::size_t i = 0; i < rounds; ++i) {
    const bool fade_in = (i == 0);
    const bool fade_out = !fade_in && (i + 1 == rounds);

    // The blocks in between are played as they are. Copying them through a
    // scaling loop that multiplies by 1.0 would be 800 samples of
    // arithmetic per block that changes nothing.
    const ports::Sample* out = chunk.data();
    if (fade_in || fade_out) {
      const float inv = 1.0F / static_cast<float>(kChunk);
      for (std::size_t n = 0; n < kChunk; ++n) {
        const float ramp = static_cast<float>(n) * inv;
        const float scale = fade_in ? ramp : 1.0F - ramp;
        shaped[n] = static_cast<ports::Sample>(static_cast<float>(chunk[n]) * scale);
      }
      out = shaped.data();
    }

    const std::uint32_t at = now();
    written += write(out, kChunk, runtime::Deadline::after(at, 500), at);
  }

  // Let it finish sounding. Returning immediately cuts the tone short.
  vTaskDelay(pdMS_TO_TICKS(120));

  // Playback remains active to avoid amplifier switching clicks. Callers must
  // stop playback before starting capture.
  return written;
}

VoiceBase::MicrophoneCheck VoiceBase::check_microphone() {
  MicrophoneCheck result;
  if (!available_) {
    return result;
  }

  constexpr std::size_t kFrames = 1600;  // 100 ms at 16 kHz
  static std::array<ports::Sample, kFrames> buffer{};

  // Stop playback first if it is running: the two cannot overlap.
  stop_playback();

  if (!start_capture(sample_rate_)) {
    return result;
  }

  const auto now = [] {
    return static_cast<std::uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
  };

  // Discard the first read, which returns whatever had accumulated.
  const std::uint32_t first = now();
  (void)read(buffer.data(), buffer.size(), runtime::Deadline::after(first, 300), first);

  const std::uint32_t second = now();
  const std::size_t got =
      read(buffer.data(), buffer.size(), runtime::Deadline::after(second, 300), second);

  stop_capture();

  result.ran = true;
  result.samples = got;
  result.rms = rms_of(buffer.data(), got);
  return result;
}

}  // namespace stackchan::audio
