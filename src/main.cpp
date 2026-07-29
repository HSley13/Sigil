#include "console_action.hpp"
#include "mock_sources.hpp"
#include "rule_engine.hpp"
#include "signal_bus.hpp"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <memory>
#include <thread>

namespace {
std::atomic<bool> running{true};
void handle_sigint(int) { running.store(false); }
} // namespace

int main(void) {
  std::signal(SIGINT, handle_sigint);

  using namespace sd;

  SignalBus<1024> bus;
  RuleEngine engine;

  engine.register_action(std::make_unique<ConsoleAction>());

  engine.add_rule(Rule{
      "high_humidity",
      Condition{"dht22", "humidity", Op::GreaterThan, 70.0f},
      "log",
      {},
      "Humidity high: {value}%",
      std::chrono::seconds(30)});

  engine.add_rule(Rule{
      "motion_detected",
      Condition{"motion", "detected", Op::Equals, true},
      "log",
      {},
      "Motion detected!"});

  MockHumiditySource dht22;
  MockMotionSource motion;

  motion.start([&bus](SignalEvent event) {
    bus.publish(event);
  });

  std::cout << "sigil daemon running -- press Ctrl+C to stop\n";

  auto last_poll = std::chrono::steady_clock::now();

  while (running.load()) {
    auto now = std::chrono::steady_clock::now();
    if (now - last_poll >= dht22.poll_interval()) {
      if (auto event = dht22.read()) {
        bus.publish(*event);
      }
      last_poll = now;
    }

    if (auto event = bus.consume()) {
      engine.process(*event);
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
  }

  std::cout << "shutting down...\n";
  motion.stop();

  return 0;
}
