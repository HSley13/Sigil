#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>

namespace sd {

// Holds a sensor reading (float), text metadata (string), or a binary state (bool).
using SignalValue = std::variant<float, std::string, bool>;

// The universal packet that flows through the Signal Bus. Every source plugin
// packages its data into this format so the rule engine can treat all sources identically.
struct SignalEvent {
  std::string source_name; // e.g. "adsb", "dht22"
  std::string field_name;  // e.g. "altitude_ft", "humidity"
  SignalValue value;
  int64_t timestamp_ms; // unix milliseconds

  // Secondary data (e.g. callsign alongside altitude), used for template
  // substitution: "Aircraft {callsign} detected at {altitude} km".
  std::map<std::string, SignalValue> metadata;
};

inline int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

} // namespace sd
