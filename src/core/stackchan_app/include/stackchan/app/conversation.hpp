#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "stackchan/domain/expression.hpp"
#include "stackchan/domain/protocol.hpp"
#include "stackchan/domain/speech.hpp"
#include "stackchan/domain/sse.hpp"
#include "stackchan/ports/audio.hpp"

// Interpreting the response stream of one conversation.
//
// What it does
// ------------
// Feed it the raw bytes of the gateway's event stream and instructions for
// playback and display come out:
//
//   bytes -> SseReader -> envelope checks -> reply.audio decoded
//         -> sentences and expressions + PCM -> Listener
//
// It knows nothing about how the bytes arrived. Received over HTTP or typed
// into a test, the behaviour is identical — which is what puts the most
// breakable part of the pipeline, the interpretation, within reach of the
// host tests.
//
// How the contract is enforced
// ----------------------------
// - a gap in the sequence numbers aborts the turn: missing audio is not
//   played as though nothing were wrong
// - audio at any rate other than the expected one is counted and dropped
// - an event that cannot be interpreted is counted and dropped while the
//   stream continues, so that new event types do not break old firmware
// - a stream that closes without a completion event is a contract
//   violation and fails the turn

namespace stackchan::app {

class ConversationTurn {
 public:
  enum class Outcome : std::uint8_t {
    running,    // still streaming
    completed,  // finished normally
    cancelled,  // stopped, by this side or the other
    failed,     // ended in an error; code() says which
  };

  // Receives the instructions. On the device this is implemented by the
  // wiring in main; tests use a recording stub.
  //
  // The call order is the order things should happen in: when on_sentence
  // arrives, switch to that expression, then play the on_audio that follows.
  class Listener {
   public:
    virtual ~Listener() = default;
    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;
    Listener(Listener&&) = delete;
    Listener& operator=(Listener&&) = delete;

    // What the speaker was heard to say, for showing on screen.
    virtual void on_recognized(std::string_view text, bool final) = 0;

    // The sentence about to be spoken and its expression, markers removed.
    virtual void on_sentence(domain::Expression expression,
                             std::string_view text) = 0;

    // PCM to play, in order.
    virtual void on_audio(const ports::Sample* samples, std::size_t count) = 0;

    // The end. Called exactly once.
    virtual void on_finished(Outcome outcome, domain::ErrorCode code) = 0;

   protected:
    Listener() = default;
  };

  explicit ConversationTurn(Listener& listener) noexcept : listener_(listener) {}

  ~ConversationTurn() = default;
  ConversationTurn(const ConversationTurn&) = delete;
  ConversationTurn& operator=(const ConversationTurn&) = delete;
  ConversationTurn(ConversationTurn&&) = delete;
  ConversationTurn& operator=(ConversationTurn&&) = delete;

    // Feed a received block. Any split is fine; SseReader absorbs it.
  void feed(std::string_view bytes) noexcept;

  // The stream closed. Without a completion event this fails the turn, as
  // the contract requires.
  void finish_input() noexcept;

  // Abort from this side: a deadline, an emergency stop, or the user.
  void abort(domain::ErrorCode code) noexcept;

  [[nodiscard]] Outcome outcome() const noexcept { return outcome_; }
  [[nodiscard]] domain::ErrorCode code() const noexcept { return code_; }

  // Events dropped because they could not be interpreted — malformed JSON,
  // an unsupported sample rate. Anything but zero is worth investigating at
  // the other end.
  [[nodiscard]] std::size_t dropped_events() const noexcept { return dropped_; }

 private:
  void handle_event(std::string_view data) noexcept;
  void handle_audio(std::string_view payload) noexcept;
  void finish(Outcome outcome, domain::ErrorCode code) noexcept;
  void drain_segments() noexcept;

  Listener& listener_;
  domain::SseReader sse_;
  domain::SpeechSegmenter segmenter_{60};

  Outcome outcome_ = Outcome::running;
  domain::ErrorCode code_ = domain::ErrorCode::none;
  // A reason that arrived early, used if the stream then closes without a
  // completion event.
  domain::ErrorCode reported_error_ = domain::ErrorCode::none;

  std::int64_t expected_seq_ = 0;
  std::size_t dropped_ = 0;

  // The contract caps raw PCM per event. Holding it as an array of Sample
  // means no reinterpretation is needed when handing it to playback.
  std::array<ports::Sample, 2048> pcm_{};
  // A sentence, plus room for the markers.
  std::array<char, 512> text_{};
};

}  // namespace stackchan::app
