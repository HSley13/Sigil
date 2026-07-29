#include <cstdint>
#include <gtest/gtest.h>

#include "action_base.hpp"
#include "rule_engine.hpp"
#include "signal_event.hpp"

#include <memory>
#include <utility>

using sd::ActionBase;
using sd::ActionRequest;
using sd::Condition;
using sd::Op;
using sd::Rule;
using sd::RuleEngine;
using sd::SignalEvent;
using sd::SignalValue;

namespace {
SignalEvent make_event(std::string source, std::string field, SignalValue value, int64_t timestamp_ms) {
  return SignalEvent{std::move(source), std::move(field), std::move(value), timestamp_ms, {}};
}

class FakeAction : public ActionBase {
public:
  explicit FakeAction(std::string name = "fake") : _name(std::move(name)) {}

  std::string name() const override { return _name; }
  std::string description() const override { return "test double"; }

  bool execute(const ActionRequest &req, const SignalEvent &trigger) override {
    call_count++;
    last_request = req;
    last_trigger = trigger;
    return succeeds;
  }

  bool succeeds = true;
  int call_count = 0;
  ActionRequest last_request{};
  SignalEvent last_trigger{};

private:
  std::string _name;
};

} // namespace

TEST(RuleEngineTest, MatchingConditionFiresRegisteredAction) {
  RuleEngine engine;
  auto action = std::make_unique<FakeAction>("notify");
  FakeAction *action_ptr = action.get();
  engine.register_action(std::move(action));

  engine.add_rule(Rule{
      "high_humidity",
      Condition{"dht22", "humidity", Op::GreaterThan, 70.0f},
      "notify",
      {{"channel", "email"}},
      "humidity high: {value}%",
  });

  engine.process(make_event("dht22", "humidity", 72.3f, 1000));

  EXPECT_EQ(action_ptr->call_count, 1);
  EXPECT_EQ(action_ptr->last_request.type, "notify");
  EXPECT_EQ(action_ptr->last_request.params.at("channel"), "email");
  EXPECT_EQ(action_ptr->last_trigger.field_name, "humidity");
}

TEST(RuleEngineTest, WrongSourceOrFileNeverFires) {
  RuleEngine engine;
  auto action = std::make_unique<FakeAction>("notify");
  FakeAction *action_ptr = action.get();
  engine.register_action(std::move(action));

  engine.add_rule(Rule{"r", Condition{"dht22", "humidity", Op::GreaterThan, 70.0f}, "notify", {}, "msg"});

  engine.process(make_event("bmp280", "humidity", 90.0f, 1000)); // wrong source
  engine.process(make_event("dht22", "pressure", 90.0f, 1000));  // wrong field

  EXPECT_EQ(action_ptr->call_count, 0);
}

TEST(RuleEngineTest, TypeMismatchNeverMatchesAndNeverCrashes) {
  RuleEngine engine;
  auto action = std::make_unique<FakeAction>("notify");
  FakeAction *action_ptr = action.get();
  engine.register_action(std::move(action));

  // Threshold is a float; event carries a bool for the same field.
  engine.add_rule(Rule{"r", Condition{"gpio", "motion", Op::Equals, 1.0f}, "notify", {}, "msg"});

  engine.process(make_event("gpio", "motion", true, 1000));

  EXPECT_EQ(action_ptr->call_count, 0);
}

TEST(RuleEngineTest, AllFourFloadOperatorsEvaluateCorrectly) {
  RuleEngine engine;
  auto gt = std::make_unique<FakeAction>("gt");
  auto lt = std::make_unique<FakeAction>("lt");
  auto eq = std::make_unique<FakeAction>("eq");
  auto ne = std::make_unique<FakeAction>("ne");

  FakeAction *gt_ptr = gt.get();
  FakeAction *lt_ptr = lt.get();
  FakeAction *eq_ptr = eq.get();
  FakeAction *ne_ptr = ne.get();

  engine.register_action(std::move(gt));
  engine.register_action(std::move(lt));
  engine.register_action(std::move(eq));
  engine.register_action(std::move(ne));

  engine.add_rule(Rule{"lt", Condition{"s", "v", Op::LessThan, 10.0f}, "lt", {}, "m"});
  engine.add_rule(Rule{"gt", Condition{"s", "v", Op::GreaterThan, 10.0f}, "gt", {}, "m"});
  engine.add_rule(Rule{"eq", Condition{"s", "v", Op::Equals, 10.0f}, "eq", {}, "m"});
  engine.add_rule(Rule{"ne", Condition{"s", "v", Op::NotEquals, 10.0f}, "ne", {}, "m"});

  engine.process(make_event("s", "v", 5.0f, 1000));

  EXPECT_EQ(lt_ptr->call_count, 1); // 5 < 10
  EXPECT_EQ(gt_ptr->call_count, 0); // 5 > 10 is false
  EXPECT_EQ(eq_ptr->call_count, 0); // 5 == 10 is false
  EXPECT_EQ(ne_ptr->call_count, 1); // 5 != 10
}

TEST(RuleEngineTest, EqualityWorksForBoolAndString) {
  RuleEngine engine;
  auto action = std::make_unique<FakeAction>("notify");
  FakeAction *action_ptr = action.get();
  engine.register_action(std::move(action));

  engine.add_rule(Rule{"r", Condition{"gpio", "pressed", Op::Equals, true}, "notify", {}, "m"});
  engine.process(make_event("gpio", "pressed", true, 1000));
  EXPECT_EQ(action_ptr->call_count, 1);

  engine.add_rule(Rule{"r2", Condition{"adsb", "callsign", Op::Equals, std::string("CI201")}, "notify", {}, "m"});
  engine.process(make_event("adsb", "callsign", std::string("CI201"), 1000));
  EXPECT_EQ(action_ptr->call_count, 2);
}

TEST(RuleEngineTest, OrderingOperatorsIgnoredForNonFloatTypes) {
  RuleEngine engine;
  auto action = std::make_unique<FakeAction>("notify");
  FakeAction *action_ptr = action.get();
  engine.register_action(std::move(action));

  engine.add_rule(Rule{"r", Condition{"gpio", "pressed", Op::LessThan, true}, "notify", {}, "m"});
  engine.process(make_event("gpio", "pressed", true, 1000));

  EXPECT_EQ(action_ptr->call_count, 0);
}

TEST(RuleEngineTest, UnregisteredActionTypeIsSkippedWithoutCrashing) {
  RuleEngine engine; // no actions registered at all

  engine.add_rule(Rule{"r", Condition{"dht22", "humidity", Op::GreaterThan, 70.0f}, "notify", {}, "m"});

  EXPECT_NO_THROW(engine.process(make_event("dht22", "humidity", 90.0f, 1000)));
}

TEST(RuleEngineTest, OneEventCanFireMultipleIndependentRules) {
  RuleEngine engine;
  auto warn = std::make_unique<FakeAction>("warn");
  auto page = std::make_unique<FakeAction>("page");

  FakeAction *warn_ptr = warn.get();
  FakeAction *page_ptr = page.get();

  engine.register_action(std::move(warn));
  engine.register_action(std::move(page));

  engine.add_rule(Rule{"w", Condition{"dht22", "humidity", Op::GreaterThan, 60.0f}, "warn", {}, "m"});
  engine.add_rule(Rule{"p", Condition{"dht22", "humidity", Op::GreaterThan, 90.0f}, "page", {}, "m"});

  engine.process(make_event("dht22", "humidity", 95.0f, 1000)); // satisfies both

  EXPECT_EQ(warn_ptr->call_count, 1);
  EXPECT_EQ(page_ptr->call_count, 1);
}

TEST(RuleEngineTest, CooldownSuppressesImmediateRefire) {
  RuleEngine engine;
  auto action = std::make_unique<FakeAction>("notify");
  FakeAction *action_ptr = action.get();
  engine.register_action(std::move(action));

  engine.add_rule(Rule{
      "r",
      Condition{"dht22", "humidity", Op::GreaterThan, 70.0f},
      "notify",
      {},
      "m",
      std::chrono::seconds(300)});

  engine.process(make_event("dht22", "humidity", 75.0f, 0));      // fires, starts cooldown
  engine.process(make_event("dht22", "humidity", 76.0f, 30'000)); // 30s later, still within 300s window

  EXPECT_EQ(action_ptr->call_count, 1);
}

TEST(RuleEngineTest, CooldownAllowsRefireOnceWindowElapses) {
  RuleEngine engine;
  auto action = std::make_unique<FakeAction>("notify");
  FakeAction *action_ptr = action.get();
  engine.register_action(std::move(action));

  engine.add_rule(Rule{
      "r",
      Condition{"dht22", "humidity", Op::GreaterThan, 70.0f},
      "notify",
      {},
      "m",
      std::chrono::seconds(300)});

  engine.process(make_event("dht22", "humidity", 75.0f, 0));
  engine.process(make_event("dht22", "humidity", 76.0f, 300'001)); // just past 300s

  EXPECT_EQ(action_ptr->call_count, 2);
}

TEST(RuleEngineTest, FirstMatchFiresImmediatelyEvenWithCooldownSet) {
  RuleEngine engine;
  auto action = std::make_unique<FakeAction>("notify");
  FakeAction *action_ptr = action.get();
  engine.register_action(std::move(action));

  engine.add_rule(Rule{
      "r",
      Condition{"dht22", "humidity", Op::GreaterThan, 70.0f},
      "notify",
      {},
      "m",
      std::chrono::seconds(300)});

  engine.process(make_event("dht22", "humidity", 75.0f, 1000));

  EXPECT_EQ(action_ptr->call_count, 1);
}

TEST(RuleEngineTest, FailedExecuteDoesNotStartCooldown) {
  RuleEngine engine;
  auto action = std::make_unique<FakeAction>("notify");
  FakeAction *action_ptr = action.get();
  action_ptr->succeeds = false;
  engine.register_action(std::move(action));

  engine.add_rule(Rule{
      "r",
      Condition{"dht22", "humidity", Op::GreaterThan, 70.0f},
      "notify",
      {},
      "m",
      std::chrono::seconds(300)});

  engine.process(make_event("dht22", "humidity", 75.0f, 0));   // fails, no cooldown recorded
  engine.process(make_event("dht22", "humidity", 76.0f, 100)); // should retry immediately

  EXPECT_EQ(action_ptr->call_count, 2);
}

TEST(RuleEngineTest, ZeroCooldownFiresOnEveryMatchingEvent) {
  RuleEngine engine;
  auto action = std::make_unique<FakeAction>("notify");
  FakeAction *action_ptr = action.get();
  engine.register_action(std::move(action));

  engine.add_rule(Rule{"r", Condition{"dht22", "humidity", Op::GreaterThan, 70.0f}, "notify", {}, "m"}); // cooldown{0}

  engine.process(make_event("dht22", "humidity", 75.0f, 0));
  engine.process(make_event("dht22", "humidity", 76.0f, 1));

  EXPECT_EQ(action_ptr->call_count, 2);
}
