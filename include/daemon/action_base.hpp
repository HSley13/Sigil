#pragma once

#include "signal_event.hpp"
#include <map>
#include <string>

namespace sd {

/* ActionRequest: What the rule engine sends to an action plugin
 * When a rule's condition matches, the rule engine builds an ActionRequest
 * and passes it to the appropriate ActionBase plugin for execution
 * */
struct ActionRequest {
  // Which action to invoke, it maches the name() of an ActionBase plugin
  std::string type;

  /* Static parameters defined in the rule. They are set when the user registers
   * the rule and don't change and laso are converted by each action plugin to
   * the appropriate type internally.
   * */
  std::map<std::string, std::string> params;

  // Message template for human-readable output
  std::string message_template;
};

/* ActionBase: Abstract base class for all the action plugins
 * Everything the system can DO is an ActionBase plugin(GPIO, Display, Network,File)
 * */
class ActionBase {
public:
  virtual ~ActionBase() = default;

  //  name(): Unique identifier for this action
  virtual std::string name() const = 0;

  // description(): human-readable description for the LLM system prompt
  virtual std::string description() const = 0;

  // execute(): Called by the rule engine to perform the action
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
