#pragma once

#include "signal_event.hpp"
#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace sd {
/*
 * @brief Base class for all signal sources
 * This defines the common interface that every source plugin must implement
 * */

class SourceBase {
public:
  virtual ~SourceBase() = default;
  /* @brief Returns the unique name of the source ("dht22", "adsb")
   * This name is used in rules ("dht22.humidity") and by the LLM
   * */
  virtual std::string name() const = 0;

  /* @brief  Returns a list of field names this source produces
   * This is crucial for the LLM to know what fields are "speakable" and queryable
   * Also for the AST Compiler to validate rules against available fields
   * */
  virtual std::vector<std::string> fields() const = 0;

  virtual std::string description() const = 0;
};

/* @brief Interface for "Pull" sources, which are polled by the daemon on a schedule
 * Examples: DHT22(temperature every 30s), BMP280(pressure every 60s)
 * */
class PullSource : public SourceBase {
public:
  /* @brief How often the daemon should call the read() method
   * @return The polling interval as a std::chrono::seconds
   * */
  virtual std::chrono::seconds poll_interval() const = 0;

  /* @brief Called by the daemon on its schedule to get a new SignalEvent
   * @return An optional SignalEvent. Returns std::nullopt if no new event is ready
   * */
  virtual std::optional<SignalEvent> read() = 0;
};

/* @brief Interface for "Push" sources, which are event-driven and notify the daemon immediately
 * Examples: ADS-B(new aircraft detected), GPIO interrupt(button pressed)
 * */
class PushSource : public SourceBase {
public:
  /* @brief Callback function type for push sources to deliver events
   * The PushSource will call this function whenever a new event occurs
   * */
  using EventCallback = std::function<void(SignalEvent)>;

  /* @brief Start the push source's internal event generation mechanism
   * The daemon call this once at startup. The source then calls the provided
   * callback funciton whenever a new event needs to be pushed to the Signal Bus
   * @param cb The callback function to use for pushing events
   * @return true if the source started successfully, false otherwise
   * */

  /*  NOTE : // I Will need to change the return value to: std::expected<void, std::string> cause a bool is fine for now but 2 events might fail for different reasons and I will need to know why in order to  debug/fix it
   */
  virtual bool start(EventCallback cb) = 0;

  /* @brief Stops the push source's internal event generation
   * The daemon calls this during shutdown
   * */
  virtual void stop() = 0;
};

} // namespace sd
