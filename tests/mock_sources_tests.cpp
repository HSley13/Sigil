#include <gtest/gtest.h>

#include "mock_sources.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <variant>
#include <vector>

using sd::MockHumiditySource;
using sd::MockMotionSource;
using sd::SignalEvent;

TEST(MockHumiditySourceTest, IdentityFieldsAreCorrect) {
  MockHumiditySource source;
  EXPECT_EQ(source.name(), "dht22");
  EXPECT_EQ(source.fields(), std::vector<std::string>{"humidity"});
  EXPECT_EQ(source.poll_interval(), std::chrono::seconds(5));
}

TEST(MockHumiditySourceTest, ReadAlwaysReturnsAValue) {
  MockHumiditySource source;
  for (int i = 0; i < 1000; i++) {
    EXPECT_TRUE(source.read().has_value());
  }
}

TEST(MockHumiditySourceTest, ReadStaysWithinValidHumidityRange) {
  MockHumiditySource source;
  for (int i = 0; i < 10000; i++) {
    auto event = source.read();
    ASSERT_TRUE(event.has_value());
    float value = std::get<float>(event->value);
    EXPECT_GE(value, 0.0f);
    EXPECT_LE(value, 100.0f);
  }
}

TEST(MockHumiditySourceTest, ReadProducesEventsMatchingSourceIdentity) {
  MockHumiditySource source;
  auto event = source.read();
  ASSERT_TRUE(event.has_value());
  EXPECT_EQ(event->source_name, "dht22");
  EXPECT_EQ(event->field_name, "humidity");
}

TEST(MockMotionSourceTest, IdentityFieldsAreCorrect) {
  MockMotionSource source;
  EXPECT_EQ(source.name(), "motion");
  EXPECT_EQ(source.fields(), std::vector<std::string>{"detected"});
}

TEST(MockMotionSourceTest, StartReturnsTrueOnFirstCall) {
  MockMotionSource source;
  EXPECT_TRUE(source.start([](SignalEvent) {}));
  source.stop();
}

TEST(MockMotionSourceTest, SecondStartWhileRunningReturnsFalse) {
  MockMotionSource source;
  ASSERT_TRUE(source.start([](SignalEvent) {}));
  EXPECT_FALSE(source.start([](SignalEvent) {}));
  source.stop();
}

TEST(MockMotionSourceTest, StopWithoutStartIsSafe) {
  MockMotionSource source;
  EXPECT_NO_THROW(source.stop());
}

TEST(MockMotionSourceTest, FiresAtLeastOneEventWithinDelayBound) {
  MockMotionSource source;
  std::atomic<int> fired{0};

  ASSERT_TRUE(source.start([&](SignalEvent) { fired++; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(5500)); // max delay is 5000ms
  source.stop();

  EXPECT_GE(fired.load(), 1);
}

TEST(MockMotionSourceTest, FiredEventsMatchSourceIdentity) {
  MockMotionSource source;
  SignalEvent last{};
  std::atomic<int> fired{0};

  ASSERT_TRUE(source.start([&](SignalEvent event) {
    last = event;
    fired++;
  }));
  std::this_thread::sleep_for(std::chrono::milliseconds(5500));
  source.stop();

  ASSERT_GE(fired.load(), 1);
  EXPECT_EQ(last.source_name, "motion");
  EXPECT_EQ(last.field_name, "detected");
  EXPECT_TRUE(std::get<bool>(last.value));
}

TEST(MockMotionSourceTest, StopJoinsBeforeReturningSoNoCallbacksFireAfter) {
  MockMotionSource source;
  std::atomic<int> fired{0};

  ASSERT_TRUE(source.start([&](SignalEvent) { fired++; }));
  std::this_thread::sleep_for(std::chrono::milliseconds(5500));
  source.stop();

  int count_after_stop = fired.load();
  std::this_thread::sleep_for(std::chrono::milliseconds(500));
  EXPECT_EQ(fired.load(), count_after_stop);
}

TEST(MockMotionSourceTest, CanRestartAfterStop) {
  MockMotionSource source;
  ASSERT_TRUE(source.start([](SignalEvent) {}));
  source.stop();

  EXPECT_TRUE(source.start([](SignalEvent) {}));
  source.stop();
}
