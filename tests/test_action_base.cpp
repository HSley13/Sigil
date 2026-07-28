#include <gtest/gtest.h>

#include "action_base.hpp"
#include "signal_event.hpp"

using sd::ActionBase;
using sd::SignalEvent;
using sd::SignalValue;

namespace {

SignalEvent make_trigger(SignalValue value) {
  return SignalEvent{"adsb", "altitude_ft", std::move(value), sd::now_ms(), {}};
}

} // namespace

TEST(ActionBaseTest, ReplacesValuePlaceholderForFloat) {
  SignalEvent trigger = make_trigger(1500.0f);
  std::string result = ActionBase::resolve_template("Altitude: {value} ft", trigger);
  EXPECT_EQ(result, "Altitude: " + std::to_string(1500.0f) + " ft");
}

TEST(ActionBaseTest, ReplacesValuePlaceholderForBool) {
  SignalEvent on = make_trigger(true);
  SignalEvent off = make_trigger(false);
  EXPECT_EQ(ActionBase::resolve_template("state={value}", on), "state=true");
  EXPECT_EQ(ActionBase::resolve_template("state={value}", off), "state=false");
}

TEST(ActionBaseTest, ReplacesValuePlaceholderForString) {
  SignalEvent trigger = make_trigger(std::string("N12345"));
  EXPECT_EQ(ActionBase::resolve_template("callsign={value}", trigger), "callsign=N12345");
}

TEST(ActionBaseTest, ReplacesMetadataPlaceholders) {
  SignalEvent trigger = make_trigger(1500.0f);
  trigger.metadata["callsign"] = std::string("N12345");

  std::string result = ActionBase::resolve_template("Aircraft {callsign} at {value} ft", trigger);
  EXPECT_EQ(result, "Aircraft N12345 at " + std::to_string(1500.0f) + " ft");
}

TEST(ActionBaseTest, LeavesUnknownPlaceholderUntouched) {
  SignalEvent trigger = make_trigger(1500.0f);
  std::string result = ActionBase::resolve_template("{unknown} stays literal", trigger);
  EXPECT_EQ(result, "{unknown} stays literal");
}

TEST(ActionBaseTest, ReplacesEveryOccurrenceOfARepeatedPlaceholder) {
  SignalEvent trigger = make_trigger(true);
  std::string result = ActionBase::resolve_template("{value}-{value}-{value}", trigger);
  EXPECT_EQ(result, "true-true-true");
}
