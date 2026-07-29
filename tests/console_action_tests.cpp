#include <gtest/gtest.h>

#include "console_action.hpp"

#include <functional>
#include <iostream>
#include <sstream>
#include <string>

using sd::ActionRequest;
using sd::ConsoleAction;
using sd::SignalEvent;

namespace {

SignalEvent make_trigger(float value) {
  return SignalEvent{"dht22", "humidity", value, sd::now_ms(), {}};
}

std::string capture_stdout(const std::function<void()> &fn) {
  std::ostringstream captured;
  std::streambuf *old = std::cout.rdbuf(captured.rdbuf());
  fn();
  std::cout.rdbuf(old);
  return captured.str();
}

} // namespace

TEST(ConsoleActionTest, NameAndDescriptionAreCorrect) {
  ConsoleAction action;
  EXPECT_EQ(action.name(), "log");
  EXPECT_FALSE(action.description().empty());
}

TEST(ConsoleActionTest, ExecuteReturnsTrue) {
  ConsoleAction action;
  ActionRequest req{"log", {}, "humidity={value}"};
  EXPECT_TRUE(action.execute(req, make_trigger(55.5f)));
}

TEST(ConsoleActionTest, ExecutePrintsResolvedTemplateFollowedByNewline) {
  ConsoleAction action;
  ActionRequest req{"log", {}, "humidity={value}%"};
  SignalEvent trigger = make_trigger(55.5f);

  std::string output = capture_stdout([&]() {
    action.execute(req, trigger);
  });

  EXPECT_EQ(output, "humidity=" + std::to_string(55.5f) + "%\n");
}

TEST(ConsoleActionTest, ExecutePrintsMetadataSubstitutions) {
  ConsoleAction action;
  ActionRequest req{"log", {}, "{sensor}: {value}"};
  SignalEvent trigger = make_trigger(42.0f);
  trigger.metadata["sensor"] = std::string("dht22");

  std::string output = capture_stdout([&]() {
    action.execute(req, trigger);
  });

  EXPECT_EQ(output, "dht22: " + std::to_string(42.0f) + "\n");
}
