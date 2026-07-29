#pragma once

#include "signal_event.hpp"
#include <map>
#include <string>

namespace sd {

// What the rule engine sends to an action plugin when a rule's condition matches.
struct ActionRequest {
  std::string type; // matches the name() of the target ActionBase plugin

  // Static parameters set when the rule is registered; each plugin converts
  // them to the types it needs internally.
  std::map<std::string, std::string> params;

  std::string message_template; // human-readable output, supports {field} substitution
};

// Abstract base for everything the system can DO (GPIO, Display, Network, File).
class ActionBase {
public:
  virtual ~ActionBase() = default;

  virtual std::string name() const = 0;        // unique identifier for this action
  virtual std::string description() const = 0; // shown to the LLM in its system prompt
  virtual bool execute(const ActionRequest &req, const SignalEvent &trigger) = 0;

  static std::string resolve_template(const std::string &tmpl, const SignalEvent &trigger) {
    std::string result = tmpl;

    // Replace {value} with the primary signal value
    std::string value_str = std::visit([](const auto &v) -> std::string {
      if constexpr (std::is_same_v<std::decay_t<decltype(v)>, float>) {
        return std::to_string(v);
      } else if constexpr (std::is_same_v<std::decay_t<decltype(v)>, bool>) {
        return v ? "true" : "false";
      } else {
        return v;
      }
    },
                                       trigger.value);

    replace_placeholder(result, "value", value_str);

    // Replace {field_name} with values from metadata
    for (const auto &[key, val] : trigger.metadata) {
      std::string val_str = std::visit([](const auto &v) -> std::string {
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, float>) {
          return std::to_string(v);
        } else if constexpr (std::is_same_v<std::decay_t<decltype(v)>, bool>) {
          return v ? "true" : "false";
        } else {
          return v;
        }
      },
                                       val);
      replace_placeholder(result, key, val_str);
    }

    return result;
  }

private:
  static void replace_placeholder(std::string &str, const std::string &key, const std::string &value) {
    std::string placeholder = "{" + key + "}";
    size_t pos = 0;
    while ((pos = str.find(placeholder, pos)) != std::string::npos) {
      str.replace(pos, placeholder.size(), value);
      pos += value.size();
    }
  }
};

} // namespace sd
