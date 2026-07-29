#pragma once

#include "action_base.hpp"
#include "signal_event.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace sd {

enum class Op { LessThan,
                GreaterThan,
                Equals,
                NotEquals };

// One comparison against one field of one source
struct Condition {
  std::string source_name;
  std::string field_name;
  Op op;
  SignalValue threshold;

  bool matches(const SignalEvent &event) const {
    if (event.source_name != source_name || event.field_name != field_name) {
      return false;
    }

    return compare(event.value, threshold, op);
  }

private:
  // A type mismatch (e.g. threshold is a float but the event carries a
  // string) can never satisfy the rule -- returns false instead of
  // throwing, so one misconfigured rule can't crash event processing.
  static bool compare(const SignalValue &lhs, const SignalValue &rhs, Op op) {
    if (lhs.index() != rhs.index()) {
      return false;
    }

    return std::visit([&](const auto &l) -> bool {
      using T = std::decay_t<decltype(l)>;
      const T &r = std::get<T>(rhs);

      if constexpr (std::is_same_v<T, float>) {
        switch (op) {
        case Op::LessThan:
          return l < r;
        case Op::GreaterThan:
          return l > r;
        case Op::Equals:
          return l == r;
        case Op::NotEquals:
          return l != r;
        }
      } else {
        // bool/string: ordering isn't meaningful, only equality is.
        switch (op) {
        case Op::Equals:
          return l == r;
        case Op::NotEquals:
          return l != r;
        default:
          return false;
        }
      }
      return false;
    },
                      lhs);
  }
};

// A condition plus what to do when it matches.
struct Rule {
  std::string id;
  Condition condition;
  std::string action_type; // must match a registered ActionBase::name()

  std::map<std::string, std::string> params;
  std::string message_template;
  std::chrono::milliseconds cooldown{0}; // 0 = fire every time the condition matches
};

// Evaluates incoming SignalEvents against registered rules and dispatches
// ActionRequests to the matching ActionBase plugin. Deliberately knows
// nothing about SignalBus -- callers pull events off the bus themselves
// and hand them to process(), which keeps this testable without any
// concurrency in play.
class RuleEngine {
public:
  void add_rule(Rule rule) {
    _rules.push_back(std::move(rule));
  }

  void register_action(std::unique_ptr<ActionBase> action) {
    _actions[action->name()] = std::move(action);
  }

  void process(const SignalEvent &event) {
    for (const Rule &rule : _rules) {
      if (!rule.condition.matches(event)) {
        continue;
      }

      if (!cooldown_elapsed(rule, event)) {
        continue;
      }

      auto it = _actions.find(rule.action_type);
      if (it == _actions.end()) {
        continue; // no plugin registered for this action_type -- no dispatch
                  // for now TODO:
      }

      ActionRequest request{rule.action_type, rule.params, rule.message_template};
      if (it->second->execute(request, event)) {
        _last_fired[rule.id] = event.timestamp_ms;
      }
    }
  }

private:
  bool cooldown_elapsed(const Rule &rule, const SignalEvent &event) const {
    if (rule.cooldown.count() == 0) {
      return true;
    }

    auto last = _last_fired.find(rule.id);
    if (last == _last_fired.end()) {
      return true;
    }

    return (event.timestamp_ms - last->second) >= rule.cooldown.count();
  }

  std::vector<Rule> _rules;
  std::unordered_map<std::string, std::unique_ptr<ActionBase>> _actions;
  std::unordered_map<std::string, int64_t> _last_fired;
};

} // namespace sd
