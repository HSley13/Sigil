#pragma once

#include "action_base.hpp"

#include <iostream>

namespace sd {

class ConsoleAction : public ActionBase {
public:
  std::string name() const override { return "log"; }
  std::string description() const override { return "Prints the resolved message to stdout"; }

  bool execute(const ActionRequest &req, const SignalEvent &trigger) override {
    std::cout << resolve_template(req.message_template, trigger) << "\n";
    return true;
  }
};

} // namespace sd
