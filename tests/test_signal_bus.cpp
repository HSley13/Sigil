#include <gtest/gtest.h>

#include "signal_bus.hpp"
#include "signal_event.hpp"

#include <algorithm>
#include <numeric>
#include <thread>
#include <vector>

using sd::SignalBus;
using sd::SignalEvent;
using sd::SignalValue;

namespace {

SignalEvent make_event(std::string source, std::string field, SignalValue value) {
  return SignalEvent{std::move(source), std::move(field), std::move(value), sd::now_ms(), {}};
}

} // namespace

TEST(SignalBusTest, ConsumeOnEmptyBufferReturnsNullopt) {
  SignalBus<8> bus;
  EXPECT_EQ(bus.consume(), std::nullopt);
}

TEST(SignalBusTest, PublishThenConsumeRoundTripsAllFields) {
  SignalBus<8> bus;
  SignalEvent sent = make_event("dht22", "humidity", 42.5f);
  sent.metadata["unit"] = std::string("%RH");

  ASSERT_TRUE(bus.publish(sent));

  std::optional<SignalEvent> received = bus.consume();
  ASSERT_TRUE(received.has_value());
  EXPECT_EQ(received->source_name, sent.source_name);
  EXPECT_EQ(received->field_name, sent.field_name);
  EXPECT_EQ(received->value, sent.value);
  EXPECT_EQ(received->timestamp_ms, sent.timestamp_ms);
  EXPECT_EQ(received->metadata, sent.metadata);
}

TEST(SignalBusTest, PreservesFifoOrderAcrossPublishes) {
  SignalBus<8> bus;
  ASSERT_TRUE(bus.publish(make_event("s", "a", 1.0f)));
  ASSERT_TRUE(bus.publish(make_event("s", "b", 2.0f)));
  ASSERT_TRUE(bus.publish(make_event("s", "c", 3.0f)));

  auto first = bus.consume();
  auto second = bus.consume();
  auto third = bus.consume();

  ASSERT_TRUE(first && second && third);
  EXPECT_EQ(first->field_name, "a");
  EXPECT_EQ(second->field_name, "b");
  EXPECT_EQ(third->field_name, "c");
}

TEST(SignalBusTest, RejectsPublishWhenBufferIsFull) {
  SignalBus<4> bus;
  for (int i = 0; i < 4; i++) {
    ASSERT_TRUE(bus.publish(make_event("s", std::to_string(i), static_cast<float>(i))));
  }

  // 5th publish must fail without disturbing the 4 events already queued.
  EXPECT_FALSE(bus.publish(make_event("s", "overflow", -1.0f)));

  for (int i = 0; i < 4; i++) {
    auto ev = bus.consume();
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->field_name, std::to_string(i));
  }
}

TEST(SignalBusTest, ConsumingFreesSlotForWraparoundReuse) {
  SignalBus<4> bus;
  for (int i = 0; i < 4; i++) {
    ASSERT_TRUE(bus.publish(make_event("s", std::to_string(i), static_cast<float>(i))));
  }

  // Free slot 0 by consuming "0", then confirm publish can reuse it.
  auto first = bus.consume();
  ASSERT_TRUE(first.has_value());
  EXPECT_EQ(first->field_name, "0");

  ASSERT_TRUE(bus.publish(make_event("s", "4", 4.0f))); // wraps into freed slot 0

  for (int i = 1; i <= 4; i++) {
    auto ev = bus.consume();
    ASSERT_TRUE(ev.has_value());
    EXPECT_EQ(ev->field_name, std::to_string(i));
  }
}

TEST(SignalBusTest, ConsumeReturnsNulloptAgainOnceDrained) {
  SignalBus<4> bus;
  ASSERT_TRUE(bus.publish(make_event("s", "only", 1.0f)));
  ASSERT_TRUE(bus.consume().has_value());

  EXPECT_EQ(bus.consume(), std::nullopt);
  EXPECT_EQ(bus.consume(), std::nullopt); // repeated calls must stay empty, not resurface stale data
}

TEST(SignalBusTest, ConcurrentProducersNeverLoseOrDuplicateEvents) {
  constexpr int producers = 8;
  constexpr int per_producer = 4000;
  constexpr int total = producers * per_producer;

  SignalBus<128> bus;
  std::vector<std::thread> writers;
  writers.reserve(producers);

  for (int p = 0; p < producers; p++) {
    writers.emplace_back([&bus, p]() {
      for (int i = 0; i < per_producer; i++) {
        SignalEvent ev = make_event("producer", "id", static_cast<float>(p * per_producer + i));
        while (!bus.publish(ev)) {
          // buffer momentarily full; spin until the consumer drains a slot
        }
      }
    });
  }

  std::vector<size_t> received;
  received.reserve(total);
  while (static_cast<int>(received.size()) < total) {
    if (auto ev = bus.consume()) {
      received.push_back(static_cast<size_t>(std::get<float>(ev->value)));
    }
  }

  for (auto &t : writers) t.join();

  ASSERT_EQ(received.size(), static_cast<size_t>(total));
  std::sort(received.begin(), received.end());
  std::vector<size_t> expected(total);
  std::iota(expected.begin(), expected.end(), 0);
  EXPECT_EQ(received, expected);
}
