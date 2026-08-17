#include <unity.h>

#include <cstdint>
#include <string>
#include <vector>

#include "stackchan/runtime/event_bus.hpp"
#include "stackchan/runtime/mailbox.hpp"

using stackchan::runtime::EventBus;
using stackchan::runtime::Mailbox;

namespace {

struct ButtonEvent {
  std::uint8_t clicks;
};

using Bus = EventBus<ButtonEvent>;

// A subscriber that accumulates what it receives.
struct Recorder {
  std::vector<std::uint8_t> seen;

  static void handle(void* context, const ButtonEvent& event) {
    static_cast<Recorder*>(context)->seen.push_back(event.clicks);
  }
};

void test_starts_with_no_subscribers() {
  const Bus bus;
  TEST_ASSERT_EQUAL_UINT32(0, bus.subscriber_count());
  TEST_ASSERT_EQUAL_UINT32(0, bus.published_count());
}

void test_publishing_with_no_subscribers_is_safe() {
  // Happens during startup, or when a feature is disabled. Nothing crashes.
  Bus bus;
  bus.publish(ButtonEvent{1});
  TEST_ASSERT_EQUAL_UINT32(1, bus.published_count());
}

void test_a_subscriber_receives_the_event() {
  Bus bus;
  Recorder recorder;
  TEST_ASSERT_TRUE(bus.subscribe(&Recorder::handle, &recorder));
  TEST_ASSERT_EQUAL_UINT32(1, bus.subscriber_count());

  bus.publish(ButtonEvent{3});
  TEST_ASSERT_EQUAL_UINT32(1, recorder.seen.size());
  TEST_ASSERT_EQUAL_UINT8(3, recorder.seen[0]);
}

void test_every_subscriber_receives_the_event() {
  // The publisher does not know how many subscribers there are, which is
  // the whole purpose.
  Bus bus;
  Recorder a;
  Recorder b;
  Recorder c;
  TEST_ASSERT_TRUE(bus.subscribe(&Recorder::handle, &a));
  TEST_ASSERT_TRUE(bus.subscribe(&Recorder::handle, &b));
  TEST_ASSERT_TRUE(bus.subscribe(&Recorder::handle, &c));

  bus.publish(ButtonEvent{2});
  TEST_ASSERT_EQUAL_UINT32(1, a.seen.size());
  TEST_ASSERT_EQUAL_UINT32(1, b.seen.size());
  TEST_ASSERT_EQUAL_UINT32(1, c.seen.size());
  TEST_ASSERT_EQUAL_UINT8(2, c.seen[0]);
}

void test_delivery_follows_registration_order() {
  // The order is fixed, so nothing ever works only because it happened to
  // be called first.
  Bus bus;
  std::vector<int> order;
  struct Marker {
    std::vector<int>* order;
    int id;
    static void handle(void* context, const ButtonEvent&) {
      auto* self = static_cast<Marker*>(context);
      self->order->push_back(self->id);
    }
  };
  Marker first{&order, 1};
  Marker second{&order, 2};
  Marker third{&order, 3};

  TEST_ASSERT_TRUE(bus.subscribe(&Marker::handle, &first));
  TEST_ASSERT_TRUE(bus.subscribe(&Marker::handle, &second));
  TEST_ASSERT_TRUE(bus.subscribe(&Marker::handle, &third));
  bus.publish(ButtonEvent{1});

  TEST_ASSERT_EQUAL_UINT32(3, order.size());
  TEST_ASSERT_EQUAL_INT(1, order[0]);
  TEST_ASSERT_EQUAL_INT(2, order[1]);
  TEST_ASSERT_EQUAL_INT(3, order[2]);
}

void test_repeated_publishing_reaches_the_subscriber_each_time() {
  Bus bus;
  Recorder recorder;
  TEST_ASSERT_TRUE(bus.subscribe(&Recorder::handle, &recorder));

  bus.publish(ButtonEvent{1});
  bus.publish(ButtonEvent{2});
  bus.publish(ButtonEvent{3});

  TEST_ASSERT_EQUAL_UINT32(3, recorder.seen.size());
  TEST_ASSERT_EQUAL_UINT8(1, recorder.seen[0]);
  TEST_ASSERT_EQUAL_UINT8(2, recorder.seen[1]);
  TEST_ASSERT_EQUAL_UINT8(3, recorder.seen[2]);
  TEST_ASSERT_EQUAL_UINT32(3, bus.published_count());
}

void test_a_null_handler_is_rejected() {
  Bus bus;
  TEST_ASSERT_FALSE(bus.subscribe(nullptr, nullptr));
  TEST_ASSERT_EQUAL_UINT32(0, bus.subscriber_count());
  // Delivery does not crash when nothing is registered.
  bus.publish(ButtonEvent{1});
}

void test_a_subscriber_without_context_works() {
  // A subscriber with no state, for something that only logs.
  static int calls = 0;
  calls = 0;
  Bus bus;
  TEST_ASSERT_TRUE(bus.subscribe([](void*, const ButtonEvent&) { ++calls; }));
  bus.publish(ButtonEvent{1});
  TEST_ASSERT_EQUAL_INT(1, calls);
}

void test_subscribing_beyond_the_limit_is_rejected() {
  Bus bus;
  Recorder recorder;
  for (std::size_t i = 0; i < Bus::kMaxSubscribers; ++i) {
    TEST_ASSERT_TRUE(bus.subscribe(&Recorder::handle, &recorder));
  }
  TEST_ASSERT_EQUAL_UINT32(Bus::kMaxSubscribers, bus.subscriber_count());

  // Full means false, never a silent drop.
  TEST_ASSERT_FALSE(bus.subscribe(&Recorder::handle, &recorder));
  TEST_ASSERT_EQUAL_UINT32(Bus::kMaxSubscribers, bus.subscriber_count());
}

void test_crossing_a_task_boundary_via_a_mailbox() {
  // How this is actually used: to cross a task boundary, register a handler
  // that posts to a mailbox. The boundary is then visible in the code.
  Bus bus;
  Mailbox<ButtonEvent, 4> inbox;

  TEST_ASSERT_TRUE(bus.subscribe(
      [](void* context, const ButtonEvent& event) {
        auto* box = static_cast<Mailbox<ButtonEvent, 4>*>(context);
        (void)box->push(event);
      },
      &inbox));

  bus.publish(ButtonEvent{1});
  bus.publish(ButtonEvent{2});

  // The receiving task takes them when it suits.
  TEST_ASSERT_EQUAL_UINT32(2, inbox.size());
  TEST_ASSERT_EQUAL_UINT8(1, inbox.pop().value().clicks);
  TEST_ASSERT_EQUAL_UINT8(2, inbox.pop().value().clicks);
}

void test_a_full_mailbox_does_not_break_delivery_to_others() {
  // One blocked subscriber does not stop the others. If congestion spread,
  // it would eventually reach the emergency stop path.
  Bus bus;
  Mailbox<ButtonEvent, 1> small;
  Recorder recorder;

  TEST_ASSERT_TRUE(bus.subscribe(
      [](void* context, const ButtonEvent& event) {
        auto* box = static_cast<Mailbox<ButtonEvent, 1>*>(context);
        (void)box->push(event);
      },
      &small));
  TEST_ASSERT_TRUE(bus.subscribe(&Recorder::handle, &recorder));

  bus.publish(ButtonEvent{1});
  bus.publish(ButtonEvent{2});
  bus.publish(ButtonEvent{3});

  // The small mailbox holds one and discards two.
  TEST_ASSERT_EQUAL_UINT32(1, small.size());
  TEST_ASSERT_EQUAL_UINT32(2, small.dropped());
  // The other still receives everything.
  TEST_ASSERT_EQUAL_UINT32(3, recorder.seen.size());
}

void test_events_carrying_owned_memory() {
  // Real events carry strings; the references must survive.
  struct TextEvent {
    std::string text;
  };
  EventBus<TextEvent> bus;
  std::string received;
  TEST_ASSERT_TRUE(bus.subscribe(
      [](void* context, const TextEvent& event) {
        *static_cast<std::string*>(context) = event.text;
      },
      &received));

  bus.publish(TextEvent{"conversation.finished"});
  TEST_ASSERT_EQUAL_STRING("conversation.finished", received.c_str());
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_starts_with_no_subscribers);
  RUN_TEST(test_publishing_with_no_subscribers_is_safe);
  RUN_TEST(test_a_subscriber_receives_the_event);
  RUN_TEST(test_every_subscriber_receives_the_event);
  RUN_TEST(test_delivery_follows_registration_order);
  RUN_TEST(test_repeated_publishing_reaches_the_subscriber_each_time);
  RUN_TEST(test_a_null_handler_is_rejected);
  RUN_TEST(test_a_subscriber_without_context_works);
  RUN_TEST(test_subscribing_beyond_the_limit_is_rejected);
  RUN_TEST(test_crossing_a_task_boundary_via_a_mailbox);
  RUN_TEST(test_a_full_mailbox_does_not_break_delivery_to_others);
  RUN_TEST(test_events_carrying_owned_memory);
  return UNITY_END();
}
