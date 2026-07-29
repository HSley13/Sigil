#pragma once

#include "source_base.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <random>
#include <thread>

namespace sd {
// synthetic humidity sensor: no real hardware, value random-walks each poll

class MockHumiditySource : public PullSource {
public:
  std::string name() const override { return "dht22"; }
  std::vector<std::string> fields() const override { return {"humidity"}; }
  std::string description() const override { return "Mock DHT22 humidity sensor (synthetic data)"; }
  std::chrono::seconds poll_interval() const override { return std::chrono::seconds(5); }

  std::optional<SignalEvent> read() override {
    std::uniform_real_distribution<float> step(-3.0f, 3.0f);
    _value += step(_rng);
    _value = std::clamp(_value, 0.0f, 100.0f);

    return SignalEvent{name(), "humidity", _value, now_ms(), {}};
  }

private:
  std::mt19937 _rng{std::random_device{}()};
  float _value = 50.0f;
};

class MockMotionSource : public PushSource {
public:
  std::string name() const override { return "motion"; }
  std::vector<std::string> fields() const override { return {"detected"}; }
  std::string description() const override { return "Mock motion sensor (synthetic events on its own thread)"; }

  bool start(EventCallback cb) override {
    if (_running.exchange(true)) {
      return false;
    }

    _thread = std::thread([this, cb = std::move(cb)]() {
      std::mt19937 rng{std::random_device{}()};
      std::uniform_int_distribution<int> delay_ms(1000, 5000);

      while (_running.load(std::memory_order_relaxed)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms(rng)));
        if (_running.load(std::memory_order_relaxed)) {
          cb(SignalEvent{name(), "detected", true, now_ms(), {}});
        }
      }
    });

    return true;
  }

  void stop() override {
    _running.store(false, std::memory_order_relaxed);
    if (_thread.joinable()) {
      _thread.join();
    }
  }

private:
  std::atomic<bool> _running{false};
  std::thread _thread;
};
} // namespace sd
