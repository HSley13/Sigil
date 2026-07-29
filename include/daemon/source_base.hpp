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

// Common interface every source plugin implements.
class SourceBase {
public:
  virtual ~SourceBase() = default;

  virtual std::string name() const = 0; // unique id, used in rules ("dht22.humidity") and by the LLM

  // Field names this source produces. Used by the LLM to know what's queryable
  // and by the AST compiler to validate rules against available fields.
  virtual std::vector<std::string> fields() const = 0;

  virtual std::string description() const = 0;
};

// Polled by the daemon on a schedule (e.g. DHT22 every 30s, BMP280 every 60s).
class PullSource : public SourceBase {
public:
  virtual std::chrono::seconds poll_interval() const = 0;
  virtual std::optional<SignalEvent> read() = 0; // nullopt if no new event is ready
};

// Event-driven; notifies the daemon immediately (e.g. ADS-B detection, GPIO interrupt).
class PushSource : public SourceBase {
public:
  using EventCallback = std::function<void(SignalEvent)>;

  // TODO: bool can't distinguish failure reasons; switch to
  // std::expected<void, std::string> once we need to debug why a specific
  // source failed to start.
  virtual bool start(EventCallback cb) = 0; // called once at startup
  virtual void stop() = 0;                  // called during shutdown
};

// Continuous raw byte stream processed into windowed chunks.
class StreamSource : public SourceBase {
public:
  using ChunkCallback = std::function<void(SignalEvent)>;

  // Opens the hardware device and starts calling cb() once per processed window.
  // Returns false if the device is missing, permission is denied, or the
  // format is unsupported.
  virtual bool start(ChunkCallback cb) = 0;

  // Must close the hardware device cleanly (so another process can reopen it)
  // and flush partial windows without calling cb() on incomplete data.
  virtual void stop() = 0;

  virtual uint32_t sample_rate_hz() const = 0; // underlying hardware sample rate
  virtual size_t window_size() const = 0;      // raw samples per processed chunk
};

// Reads AND writes to the same physical device (e.g. a serial radio, an I2C sensor with commands).
class BidirectionalSource : public SourceBase {
public:
  using DataCallback = std::function<void(SignalEvent)>;

  // Opens and configures the device (baud rate, format, etc.) and starts the
  // receive thread. The plugin parses raw bytes into SignalEvents itself —
  // the daemon never sees raw bytes.
  virtual bool start(DataCallback cb) = 0;

  // Must drain pending writes, join the receive thread, and close the file
  // descriptor before returning.
  virtual void stop() = 0;

  virtual bool send(const std::string &command) = 0;

  // Called by the daemon's health monitor every 60s. Must not block longer
  // than 2s (return false on timeout) — a false result triggers stop()+start().
  virtual bool ping() = 0;
};

// Triggers a long async operation and receives one result when it completes.
class BatchSource : public SourceBase {
public:
  using ResultCallback = std::function<void(SignalEvent)>;

  // Starts the operation in a background thread and returns immediately;
  // cb() is called exactly once when it finishes. Returns false if already
  // running (call cancel() first) or if hardware is unavailable.
  virtual bool trigger(ResultCallback cb) = 0;

  // True from the moment trigger() starts until cb() is called. Lets the rule
  // engine (and the LLM) avoid double-triggering.
  virtual bool is_running() const = 0;

  // Aborts a running operation: kill spawned subprocesses, delete incomplete
  // output, join the worker thread. Must NOT call the ResultCallback.
  virtual void cancel() = 0;

  // Typical duration; used to warn if a run takes longer than expected and to
  // give the LLM context. Return seconds(0) if unpredictable.
  virtual std::chrono::seconds estimated_duration() const = 0;
};

} // namespace sd
