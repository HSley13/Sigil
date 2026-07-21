#pragma once

#include "signal_event.hpp"
#include <chrono>
#include <cstddef>
#include <cstdint>
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
  // @brief EventCallback: callled by the Pushsource whenever a new event occurs
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

/* @brief Interface for "Stream" sources, which are continuous raw byte stream
 * processed into windowed chunks
 * */
class StreamSource : public SourceBase {
public:
  // @brief ChunkCallback: called by the Streamsource each time a window is processed
  using ChunkCallback = std::function<void(SignalEvent)>;

  /* @brief Open the hardware stream and begin processing
   * The source opens the hardware device, starts its internal processing
   * thread, and begins calling cb() once per processed window
   * @param  cb Callback called per processed chunk
   * @return true if hardware opened and stream started,
   *         false if device is not found, permission denied or format not supported
   * */
  virtual bool start(ChunkCallback cb) = 0;

  /* @brief Halts stream, flush buffers and release hardware
   * Must close the hardware device cleanly so another process can open it
   * afterward. Flush any partial windows, don't call cb() with incomplete data.
   * */
  virtual void stop() = 0;

  /* @brief The underlying hardware sample rate
   * Use by the daemon for monitoring and logging.
   * Also used by derived computed souraces taht need to know the time resolutin
   * of the raw stream
   * */
  virtual uint32_t sample_rate_hz() const = 0;

  /* @brief Number of raw samples per processed chunk
   * This determines the time resolution of the SignalEvents
   * */
  virtual size_t window_size() const = 0;
};

// @brief Interface for "bidirectional" source which read AND write to the same physical device
class BidirectionalSource : public SourceBase {
public:
  /* @brief DataCallback: Called when inbound data arrives from the device
   * The plugin is responsible for parsing raw bytes into SignalEvents
   * before calling this. The daemon never sees raw bytes.
   */
  using DataCallback = std::function<void(SignalEvent)>;

  /*
   * @brief Opens the communication channel, begin receiving
   * Opens the device (serial port, I2C bus, SPI bus), configures
   * it (baud rate, byte format, speed), and starts the receive thread.
   * @param cb   Callback called for each received SignalEvent
   * @return     true if device opened and ready
   *             false if device not found, permission denied,
   *             or configuration failed
   */
  virtual bool start(DataCallback cb) = 0;

  /*
   * @brief Closes the channel, halt receive thread
   * Must drain any pending writes before closing.
   * Must join the receive thread before returning.
   * Must close the file descriptor.
   */
  virtual void stop() = 0;

  /*
   * @brief Transmits a command to the device
   * Called by the rule engine (via DeviceCommandAction) when a rule
   * fires and wants to command this specific device.
   * @param command  The raw command string to send
   * @return         true if command was written to the device
   *                 false if write failed (device disconnected,
   *                 buffer full, timeout)
   */
  virtual bool send(const std::string &command) = 0;

  /*
   * @brief Checks if the device is still responsive
   * Called by the daemon health monitor every 60 seconds.
   * If ping() returns false, the daemon logs a warning and
   * attempts to restart the source (stop() then start()).
   *
   * Must not block longer than 2 seconds. Return false on timeout.
   */
  virtual bool ping() = 0;
};

// @brief Interface for "batch" source which trigger a long async operation, receive one large result
class BatchSource : public SourceBase {
public:
  // @brief ResultCallback: It will be Called by the BatchSource once when the batch operation completes
  using ResultCallback = std::function<void(SignalEvent)>;

  /* @brief trigger(): Starts the batch operation asynchronously
   * Starts the operation in a background thread and returns immediately.
   * When the operation finishes, cb() is called exactly once with the result.
   * @param cb   Callback called with the result when complete
   * @return     true if the operation started successfully
   *             false if already running (call cancel() first),
   *             or if hardware is unavailable
   */
  virtual bool trigger(ResultCallback cb) = 0;

  /* @brief is_running(): Checks Whether a batch operation is currently in progress
   * Used by the rule engine to prevent double-triggering.
   * Also exposed to the LLM so rules can check:
   *   "IF noaa_decoder.is_running == false THEN batch_trigger noaa_decoder"
   * Returns true from the moment trigger() starts until cb() is called.
   */
  virtual bool is_running() const = 0;

  /* @brief cancel(): Abort a running batch operation
   * Must stop the operation cleanly:
   *   - Kill any spawned subprocesses (rtl_fm, aptdec)
   *   - Delete incomplete output files
   *   - Join the worker thread
   *   - NOT call the ResultCallback (the result is invalid)
   */
  virtual void cancel() = 0;

  /* @brief estimated_duration(): How long this operation typically takes
   * Used by the daemon to:
   *   - Log a warning if the operation runs longer than expected
   *   - Give the LLM context: "this takes about 15 minutes"
   *   - Decide whether to show a "in progress" status in the web UI
   * Return std::chrono::seconds(0) if duration is unpredictable.
   */
  virtual std::chrono::seconds estimated_duration() const = 0;
};

} // namespace sd
