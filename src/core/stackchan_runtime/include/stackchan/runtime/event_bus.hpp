#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Where events go: state changes delivered to whoever wants them.
//
// Why it exists
// -------------
// So a publisher does not know its subscribers. Code that announces "the
// button was pressed" has no business knowing whether the conversation
// task, the UI, or the gateway consumes it — if it did, every new consumer
// would mean editing the publisher.
//
// Why delivery is synchronous
// ---------------------------
// No OS primitives are allowed here; they would put this out of reach of
// the host tests. publish() simply calls the registered handlers in place.
// To cross a task boundary a subscriber registers a handler that posts to a
// Mailbox, which makes the boundary visible in the code rather than hidden
// inside the bus.
//
// Why there is no unsubscribe
// ---------------------------
// Subscribers are wired up at startup and do not come and go. Supporting
// removal would introduce the awkward case of unsubscribing during
// delivery. It can be added when something actually needs it.

namespace stackchan::runtime {

// Handlers are plain function pointers, which cannot capture.
//
// Allowing capturing lambdas would mean managing their lifetimes. A context
// pointer is passed instead; callers put their own object in it.
template <typename Event>
class EventBus {
 public:
  using Handler = void (*)(void* context, const Event& event);

  EventBus() noexcept = default;
  ~EventBus() = default;

  // The bus is wired at startup and stays put. A copy would leave it
  // unclear which instance a subscriber is attached to.
  EventBus(const EventBus&) = delete;
  EventBus& operator=(const EventBus&) = delete;
  EventBus(EventBus&&) = delete;
  EventBus& operator=(EventBus&&) = delete;

  // Subscribe. False if the handler could not be registered.
  //
  // The limit is sized for the subscribers wired up at startup. Exceeding
  // it is a reason to question the design, not to raise the limit.
  [[nodiscard]] bool subscribe(Handler handler, void* context = nullptr) noexcept;

  // Deliver, in registration order.
  //
  // The order is fixed to keep faults reproducible — nothing should work
  // only because it happened to be called first.
  void publish(const Event& event) noexcept;

  [[nodiscard]] std::size_t subscriber_count() const noexcept { return count_; }

  // How many events were delivered. Kept so that "nothing is arriving"
  // becomes observable.
  [[nodiscard]] std::size_t published_count() const noexcept { return published_; }

  static constexpr std::size_t kMaxSubscribers = 8;

 private:
  struct Subscription {
    Handler handler = nullptr;
    void* context = nullptr;
  };

  std::array<Subscription, kMaxSubscribers> subscriptions_{};
  std::size_t count_ = 0;
  std::size_t published_ = 0;
};

// -------------------------------------------------------- implementation

template <typename Event>
bool EventBus<Event>::subscribe(Handler handler, void* context) noexcept {
  if (handler == nullptr || count_ == kMaxSubscribers) {
    return false;
  }
  subscriptions_[count_] = Subscription{handler, context};
  ++count_;
  return true;
}

template <typename Event>
void EventBus<Event>::publish(const Event& event) noexcept {
  ++published_;
  // Iterate by index against the count taken on entry, so a subscription
  // added during delivery is not called this round.
  const std::size_t at_entry = count_;
  for (std::size_t i = 0; i < at_entry; ++i) {
    subscriptions_[i].handler(subscriptions_[i].context, event);
  }
}

}  // namespace stackchan::runtime
