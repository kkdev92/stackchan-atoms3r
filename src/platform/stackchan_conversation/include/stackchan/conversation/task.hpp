#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "stackchan/app/conversation_start.hpp"
#include "stackchan/domain/device_state.hpp"
#include "stackchan/ports/audio.hpp"
#include "stackchan/ports/face.hpp"
#include "stackchan/runtime/cancellation.hpp"

// Runs a conversation: record, send, play, on a task of its own.
//
// Why a separate task
// -------------------
// A conversation takes seconds and involves several blocking waits. Run it
// on the main loop and the device stops answering its API for the duration,
// which is exactly when someone is most likely to want to interrupt it.
// Isolated here, the HTTP server and the main loop keep running throughout
// — changing the expression mid-conversation works, and so does cancelling.
//
// The half-duplex order
// ---------------------
// Recording stops before sending, and playback follows. Nothing is ever
// recorded and played at once, because the codec cannot do both.
//
// How the state is published
// --------------------------
// phase() returns a copy of an atomic. The main loop copies it into the
// device state each time round, which is what the API reports. Owned by
// this task, read as a copy.

namespace stackchan::conversation {

struct TaskDeps {
  ports::AudioSource* source = nullptr;
  ports::AudioSink* sink = nullptr;
  ports::Face* face = nullptr;
  // Where cancellation comes from: the emergency stop and the user's own
  // interruption both arrive through this.
  runtime::CancellationSource* cancellation = nullptr;
};

// Start the task, once, at boot. False means conversation is unavailable.
[[nodiscard]] bool begin(const TaskDeps& deps);

// Longest utterance accepted as text. The command handler rejects a larger
// value.
inline constexpr std::size_t kMaxSpokenTextBytes =
    app::kMaxConversationStartTextBytes;

// Start one conversation. True if it was accepted, false if one is already
// running or the task never started. Whether starting is sensible is
// decided by the caller through DeviceState::can_start_conversation(), so
// that the rule lives in one place.
//
// Passing text skips recording and sends those words as though they had
// been heard. It is how the whole path can be exercised without speaking
// into the microphone. Empty means record as usual.
[[nodiscard]] bool request_start(std::string_view text = {});

// The current phase. Anything but idle means a conversation is in flight.
[[nodiscard]] domain::ConversationPhase phase();

// Outcomes so far, for watching the device's health.
struct Counters {
  std::uint32_t completed = 0;
  std::uint32_t cancelled = 0;
  std::uint32_t failed = 0;
};
[[nodiscard]] Counters counters();

}  // namespace stackchan::conversation
