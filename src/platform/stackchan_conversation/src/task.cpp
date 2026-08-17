#include "stackchan/conversation/task.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "stackchan/app/conversation.hpp"
#include "stackchan/app/playback_stager.hpp"
#include "stackchan/conversation/gateway_client.hpp"
#include "stackchan/domain/ids.hpp"
#include "stackchan/identity/device.hpp"
#include "stackchan/runtime/deadline.hpp"

namespace stackchan::conversation {
namespace {

constexpr char kTag[] = "conversation";

// Fixed by the contract, matching what the audio ports declare.
constexpr std::uint32_t kSampleRate = 16000;

// How long one utterance is recorded for. Fixed, for now: recording while
// the button is held, or stopping at silence, would build on top of this.
constexpr std::uint32_t kListenMs = 3000;
constexpr std::size_t kCaptureSamples = kSampleRate * kListenMs / 1000;

// One read of the recording, 100 ms. Cancellation takes effect at this
// granularity.
constexpr std::size_t kCaptureSlice = 1600;

// Where a sentence accumulates until it is complete: twelve seconds, in
// PSRAM.
//
// Why sentences are buffered at all is decided in core, by
// app::PlaybackStager, whose host tests fix both that a slower-than-real
// supply does not cause gaps and that the expression changes with the
// sound. This file only provides the memory and does what the stager says.
constexpr std::size_t kStageSamples = kSampleRate * 12;

// The conversation holds an HTTP client and an event-stream parser, so 8 KB
// would be uncomfortably tight.
constexpr std::uint32_t kTaskStackBytes = 12288;
// Below the HTTP server's priority, so that the device keeps answering
// while a conversation runs. This is what makes "the API does not stop"
// true in scheduling terms rather than just in intent.
constexpr UBaseType_t kTaskPriority = 4;

// How much audio the hardware still holds after a write is accepted. Keep
// this in step with the I2S buffer configuration; changing one without the
// other makes the end of a conversation clip or drag.
constexpr std::uint32_t kSinkDepthMs = 240;

TaskDeps g_deps{};
TaskHandle_t g_task = nullptr;
ports::Sample* g_capture = nullptr;  // PSRAM, allocated once in begin()
ports::Sample* g_stage = nullptr;    // PSRAM, allocated once in begin()

// Words to speak, written by request_start and read by the task.
//
// The phase handover is what makes this safe: request_start only writes
// while idle, and notifies afterwards, so the task never reads a partial
// value.
std::array<char, kMaxSpokenTextBytes + 1> g_spoken_text{};

std::atomic<domain::ConversationPhase> g_phase{domain::ConversationPhase::idle};
std::atomic<std::uint32_t> g_completed{0};
std::atomic<std::uint32_t> g_cancelled{0};
std::atomic<std::uint32_t> g_failed{0};

[[nodiscard]] std::uint32_t now_ms() {
  return static_cast<std::uint32_t>(esp_timer_get_time() / 1000);
}

// Carries out the stager's instructions: it asks core what to do and moves
// the speaker and the face accordingly.
class Player final : public app::ConversationTurn::Listener {
 public:
  explicit Player(app::PlaybackStager& stager) noexcept : stager_(stager) {}

  void on_recognized(std::string_view text, bool final) override {
    ESP_LOGI(kTag, "heard%s: %.*s", final ? "" : " (partial)",
             static_cast<int>(text.size()), text.data());
  }

  void on_sentence(domain::Expression expression, std::string_view text) override {
    // If audio from the previous sentence is still staged, the answer is
    // "play that first, under the old expression". Changing the face before
    // then would run a sentence ahead of the sound.
    speak(stager_.begin_sentence(expression));
    ESP_LOGI(kTag, "say [%.*s] %.*s",
             static_cast<int>(domain::to_string(expression).size()),
             domain::to_string(expression).data(), static_cast<int>(text.size()),
             text.data());
  }

  void on_audio(const ports::Sample* samples, std::size_t count) override {
    // With nowhere to buffer and nowhere to play, buffering is pointless.
    // Refuse before entering the loop below, which would otherwise spin:
    // accept returns 0 forever and there is no way to drain it.
    if (samples == nullptr || g_deps.sink == nullptr || g_stage == nullptr) {
      return;
    }

    // Remember how loud the audio was. When nothing is heard, this is what
    // separates "nothing arrived" from "it arrived and did not play"
    // without needing someone to listen.
    for (std::size_t i = 0; i < count; ++i) {
      const std::int32_t magnitude = samples[i] < 0 ? -samples[i] : samples[i];
      if (magnitude > peak_) {
        peak_ = magnitude;
      }
    }
    received_ += count;

    // Buffer it. If it does not fit, the answer is "play first", so play
    // and offer the rest again — this is how an unusually long sentence
    // starts playing before it is complete.
    std::size_t offset = 0;
    while (offset < count) {
      const std::size_t taken = stager_.accept(samples + offset, count - offset);
      if (taken != 0) {
        offset += taken;
        continue;
      }
      // Either full, or waiting for a sentence boundary to be flushed.
      // Both are cleared by playing what is staged.
      ESP_LOGW(kTag, "the sentence is longer than the stage; playing early");
      const std::size_t staged = stager_.staged_count();
      speak(staged);
      if (staged == 0 || stager_.staged_count() != 0) {
        // Nothing to play, or playing did not free anything. There is no
        // way forward, so discard the rest rather than loop with no outlet.
        ESP_LOGW(kTag, "the stage could not be drained; dropping %u sample(s)",
                 static_cast<unsigned>(count - offset));
        return;
      }
    }
  }

  void on_finished(app::ConversationTurn::Outcome outcome,
                   domain::ErrorCode code) override {
    // The last sentence has no successor to mark its boundary, so on a
    // clean finish it is played out here. After a cancellation or a
    // failure, the stager discards what is left.
    speak(stager_.finish(outcome == app::ConversationTurn::Outcome::completed));
    ESP_LOGI(kTag, "turn finished: %s (%.*s)",
             outcome == app::ConversationTurn::Outcome::completed   ? "completed"
             : outcome == app::ConversationTurn::Outcome::cancelled ? "cancelled"
                                                                    : "failed",
             static_cast<int>(domain::to_string(code).size()),
             domain::to_string(code).data());
  }

  // How much audio is still expected to be sounding, so that winding down
  // can wait for it.
  [[nodiscard]] std::uint32_t remaining_ms() const noexcept {
    return stager_.remaining_ms(now_ms());
  }

  // Reset for each conversation.
  void reset_audio_counters() noexcept {
    received_ = 0;
    played_ = 0;
    peak_ = 0;
    stager_.reset();
  }

  void log_audio_counters() const {
    ESP_LOGI(kTag, "audio: %u samples received, %u written, peak=%d",
             static_cast<unsigned>(received_), static_cast<unsigned>(played_),
             static_cast<int>(peak_));
  }

 private:
  // Carry out one instruction: set the expression, start playback, write
  // every sample, then report it done.
  void speak(std::size_t count) {
    if (count == 0 || g_deps.sink == nullptr) {
      return;
    }
    if (!g_deps.sink->playing() && !g_deps.sink->start_playback(kSampleRate)) {
      ESP_LOGW(kTag, "playback could not be started; dropping audio");
      stager_.flushed(now_ms());  // free the buffer rather than leave it stuck
      return;
    }
    g_phase.store(domain::ConversationPhase::speaking, std::memory_order_release);
    if (g_deps.face != nullptr) {
      g_deps.face->show(stager_.expression_to_speak());
      g_deps.face->set_talking(true);
    }

    // Handed over in one go. Writing proceeds at the speed the hardware
    // accepts, so there is no gap regardless of how the audio arrived.
    const std::size_t written = write_all(stager_.staged_samples(), count);
    played_ += written;
    ESP_LOGI(kTag, "played %ums of audio", static_cast<unsigned>(written / 16));
    stager_.flushed(now_ms());
  }

  // Write everything, under a deadline. Give up if it cannot be written.
  [[nodiscard]] std::size_t write_all(const ports::Sample* samples,
                                      std::size_t count) {
    std::size_t written = 0;
    while (written < count) {
      if (g_deps.cancellation != nullptr &&
          g_deps.cancellation->token().cancelled()) {
        break;
      }
      const std::uint32_t t = now_ms();
      const std::size_t wrote = g_deps.sink->write(
          samples + written, count - written, runtime::Deadline::after(t, 1000), t);
      if (wrote == 0) {
        ESP_LOGW(kTag, "playback stalled; dropping the rest of this chunk");
        break;
      }
      written += wrote;
    }
    return written;
  }

  app::PlaybackStager& stager_;
  std::size_t received_ = 0;
  std::size_t played_ = 0;
  std::int32_t peak_ = 0;
};

// The stager, and the executor of its instructions. Built once in begin();
// only one conversation task runs, so one of each suffices.
alignas(app::PlaybackStager) std::uint8_t g_stager_storage[sizeof(app::PlaybackStager)];
app::PlaybackStager* g_stager = nullptr;
alignas(Player) std::uint8_t g_player_storage[sizeof(Player)];
Player* g_player = nullptr;

// Record, returning how many samples were read. Returns early on
// cancellation.
[[nodiscard]] std::size_t record(runtime::CancellationToken token) {
  // Stop playback first, since the two cannot overlap.
  //
  // Playback is deliberately left running after a sound finishes, because
  // switching the amplifier clicks. So after the startup tone or a button
  // beep, recording would be refused here and return no samples unless it
  // is stopped first.
  if (g_deps.sink != nullptr && g_deps.sink->playing()) {
    g_deps.sink->stop_playback();
  }
  if (!g_deps.source->start_capture(kSampleRate)) {
    ESP_LOGW(kTag, "capture could not be started");
    return 0;
  }

  // The recording as a whole is bounded too (invariant 4).
  //
  // Each read is capped at 300 ms, which is not enough on its own. If reads
  // keep returning no samples — I2S enabled but nothing arriving — this
  // loop never ends: the total never grows and no cancellation is set, so
  // it spins every 300 ms and stays in listening forever. The only escape
  // would be the user holding the button, which is precisely the hang this
  // is meant to prevent.
  //
  // Bounded at the recording length plus a second of slack, because the
  // codec can be slow to deliver the first block after startup. Whatever
  // was captured before the deadline is still sent; zero samples is what
  // the caller treats as an abort.
  const std::uint32_t started = now_ms();
  const auto overall = runtime::Deadline::after(started, kListenMs + 1000);

  std::size_t total = 0;
  while (total < kCaptureSamples && !token.cancelled()) {
    const std::uint32_t t = now_ms();
    if (overall.expired(t)) {
      ESP_LOGW(kTag, "capture timed out with %u of %u sample(s)",
               static_cast<unsigned>(total), static_cast<unsigned>(kCaptureSamples));
      break;
    }
    const std::size_t want = kCaptureSamples - total < kCaptureSlice
                                 ? kCaptureSamples - total
                                 : kCaptureSlice;
    // The slice never outlives the overall deadline; earlier_of propagates
    // it.
    total += g_deps.source->read(
        g_capture + total, want,
        overall.earlier_of(runtime::Deadline::after(t, 300), t), t);
  }
  g_deps.source->stop_capture();
  return total;
}

void run_one_turn() {
  const auto token = g_deps.cancellation->token();

  // Clear a stale cancellation before starting. A cancellation refers to
  // the conversation that was running at the time, and one raised while
  // none was would otherwise stop the next one the instant it began. An
  // emergency stop persists until it is explicitly released.
  if (token.cancelled() && !token.emergency()) {
    g_deps.cancellation->reset();
  }

  // Given words to say, do not record: speak as though they had been heard.
  const std::string_view spoken{g_spoken_text.data()};

  std::size_t captured = 0;
  if (spoken.empty()) {
    ESP_LOGI(kTag, "listening for %ums", static_cast<unsigned>(kListenMs));
    captured = record(token);
  } else {
    ESP_LOGI(kTag, "asked in text: %.*s", static_cast<int>(spoken.size()),
             spoken.data());
  }

  const bool aborted = token.cancelled() || (spoken.empty() && captured == 0);

  if (!aborted) {
    g_phase.store(domain::ConversationPhase::thinking, std::memory_order_release);

    static domain::IdGenerator id_generator{identity::collect().boot_id};
    const domain::Id conversation_id = id_generator.next();

    g_player->reset_audio_counters();
    // A fresh interpreter is constructed in the same storage each time, so
    // no heap is involved.
    alignas(app::ConversationTurn) static std::uint8_t
        turn_storage[sizeof(app::ConversationTurn)];
    auto* turn = new (turn_storage) app::ConversationTurn(*g_player);

    ConverseRequest request{};
    if (spoken.empty()) {
      request.pcm = reinterpret_cast<const std::uint8_t*>(g_capture);
      request.pcm_bytes = captured * sizeof(ports::Sample);
    } else {
      request.text = spoken;
    }
    request.conversation_id = conversation_id.text();

    ESP_LOGI(kTag, "sending %s as %.*s", spoken.empty() ? "recorded audio" : "text",
             static_cast<int>(conversation_id.text().size()),
             conversation_id.text().data());
    (void)converse(request, *turn, token);

    switch (turn->outcome()) {
      case app::ConversationTurn::Outcome::completed:
        g_completed.fetch_add(1, std::memory_order_relaxed);
        break;
      case app::ConversationTurn::Outcome::cancelled:
        g_cancelled.fetch_add(1, std::memory_order_relaxed);
        break;
      default:
        g_failed.fetch_add(1, std::memory_order_relaxed);
        break;
    }
    g_player->log_audio_counters();
    if (turn->dropped_events() != 0) {
      ESP_LOGW(kTag, "%u event(s) could not be interpreted",
               static_cast<unsigned>(turn->dropped_events()));
    }
    turn->~ConversationTurn();
  } else {
    g_cancelled.fetch_add(1, std::memory_order_relaxed);
  }

  // Winding down. The order matters: play out, then stop, then reset the
  // state.
  if (g_deps.sink->playing()) {
    // Wait for what the hardware still holds. stop_playback cuts the
    // amplifier without waiting, so calling it immediately clips the end of
    // the last sentence.
    const std::uint32_t remaining = g_player->remaining_ms();
    if (remaining > 0) {
      vTaskDelay(pdMS_TO_TICKS(remaining + 50));
    }
    g_deps.sink->stop_playback();
  }
  if (g_deps.face != nullptr) {
    g_deps.face->set_talking(false);
  }

  // Anything but an emergency stop has served its purpose by now. An
  // emergency stop stays until it is explicitly released.
  if (token.cancelled() && !token.emergency()) {
    g_deps.cancellation->reset();
  }

  // The words are used once. Left in place, the next conversation started
  // from the button would send them again instead of recording.
  g_spoken_text[0] = 0;

  g_phase.store(domain::ConversationPhase::idle, std::memory_order_release);
}

void task_main(void*) {
  ESP_LOGI(kTag, "conversation task ready");
  while (true) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    run_one_turn();
  }
}

}  // namespace

bool begin(const TaskDeps& deps) {
  if (g_task != nullptr) {
    return true;
  }
  if (deps.source == nullptr || deps.sink == nullptr ||
      deps.cancellation == nullptr) {
    return false;
  }
  g_deps = deps;

  // Three seconds at 16 kHz and two bytes a sample is 96 KB, too much for
  // internal RAM. Allocated once: doing it per conversation would fragment
  // the heap.
  g_capture = static_cast<ports::Sample*>(
      heap_caps_malloc(kCaptureSamples * sizeof(ports::Sample), MALLOC_CAP_SPIRAM));
  if (g_capture == nullptr) {
    ESP_LOGE(kTag, "capture buffer could not be allocated");
    return false;
  }

  // Where a sentence accumulates: twelve seconds, 384 KB, also in PSRAM.
  g_stage = static_cast<ports::Sample*>(
      heap_caps_malloc(kStageSamples * sizeof(ports::Sample), MALLOC_CAP_SPIRAM));
  if (g_stage == nullptr) {
    ESP_LOGE(kTag, "stage buffer could not be allocated");
    heap_caps_free(g_capture);
    g_capture = nullptr;
    return false;
  }

  // The stager and its executor, built once around the buffer.
  g_stager = new (g_stager_storage)
      app::PlaybackStager(g_stage, kStageSamples, kSinkDepthMs);
  g_player = new (g_player_storage) Player(*g_stager);

  // Pinned away from the core the main loop and Wi-Fi share. The display is
  // protected by its own lock.
  //
  // The stack size here is in bytes, not words — unlike plain FreeRTOS.
  const BaseType_t created =
      xTaskCreatePinnedToCore(&task_main, "conversation", kTaskStackBytes, nullptr,
                              kTaskPriority, &g_task, 1);
  if (created != pdPASS) {
    g_task = nullptr;
    heap_caps_free(g_capture);
    g_capture = nullptr;
    heap_caps_free(g_stage);
    g_stage = nullptr;
    return false;
  }
  return true;
}

bool request_start(std::string_view text) {
  if (g_task == nullptr || text.size() > kMaxSpokenTextBytes) {
    return false;
  }
  // Only startable from idle, which is what rejects a second start.
  auto expected = domain::ConversationPhase::idle;
  if (!g_phase.compare_exchange_strong(expected, domain::ConversationPhase::listening,
                                       std::memory_order_acq_rel)) {
    return false;
  }

  // Copy the words: the caller's buffer is reused immediately. Written
  // after the phase moves but before the notification, while the task is
  // still parked.
  std::memcpy(g_spoken_text.data(), text.data(), text.size());
  g_spoken_text[text.size()] = 0;

  xTaskNotifyGive(g_task);
  return true;
}

domain::ConversationPhase phase() {
  return g_phase.load(std::memory_order_acquire);
}

Counters counters() {
  return Counters{g_completed.load(std::memory_order_relaxed),
                  g_cancelled.load(std::memory_order_relaxed),
                  g_failed.load(std::memory_order_relaxed)};
}

}  // namespace stackchan::conversation
