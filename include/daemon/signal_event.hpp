#pragma once

#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <string>
#include <variant>

namespace sd {

/*
 * Signal Value: The universal Value
 * std::variant is used to allow a single variable to hold different types
 * - float: For Sensors (temp, humidity, altitude, distance)
 * - std::string: For Metadata (callsings, vessel names, messages)
 * - bool: For binary states (button pressed, ship in zone, motion detected)
 * */
using SignalValue = std::variant<float, std::string, bool>;

/*
 * SignalEvent: The universal Packet
 * This struct is what flows through the Signal Bus
 * Every source plugin must package its data into this format
 * */
struct SignalEvent {

  // Identity: Where did this come from? ("adsb". "dht22")
  std::string source_name;

  // Field: What specific data point is this? ("altitude_ft", "humidity")
  std::string field_name;

  // Data: The actual reading
  SignalValue value;

  // Timing: Unix milliseconds, Critical for cooldowns and time-sensitive rules
  int64_t timestamp_ms;

  /*
   * Metadata: The Context
   * Often, a signal has primary data (altitude) but also secondary data (callsign)
   * By Storing it in a map, "Template Substitution" can be perform ( "Aircraft {callsign} detected at {altitude} km ")
   * */
  std::map<std::string, SignalValue> metadata;
};

} // namespace sd
