#pragma once
#include "signal_event.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace sd {

/*
Cache line size (usually 64 bytes).
We use this to align data so different threads don't fight over the same cache line
(false sharing).
*/
inline constexpr size_t CACHE_LINE_SIZE = 64;

template <size_t N = 1024>
class SignalBus {
  // N must be a power of 2 so we can replace modulo with bitwise AND
  static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of 2, at least 2");

public:
  SignalBus() {
    /*
    Initialize each slot's sequence number to its index.

    Meaning:
    sequence == position → slot is EMPTY and ready for that position.
    */
    for (size_t i{0}; i < N; i++) {
      _cells[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  SignalBus(const SignalBus &) = delete;
  SignalBus &operator=(const SignalBus &) = delete;

  /*
  publish(): called by MULTIPLE producer threads

  Goal:
  - Find a free slot
  - Claim it safely
  - Write data
  - Mark it as ready
  */
  bool publish(SignalEvent event) {
    // Load current global write position (no sync needed yet)
    size_t pos = _enqueue_pos.load(std::memory_order_relaxed);

    for (;;) {
      // Map global position → ring buffer index
      Cell &cell = _cells[pos & (N - 1)];

      // Read the sequence of this slot
      size_t seq = cell.sequence.load(std::memory_order_acquire);

      // Difference tells us the state of the slot
      std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

      if (diff == 0) {
        /*
        seq == pos → slot is EMPTY and belongs to this position
        Try to claim it by advancing enqueue_pos
        */
        if (_enqueue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {

          // We successfully claimed this slot

          // Write the actual event data
          cell.data = std::move(event);

          /*
          Publish the data:
          sequence = pos + 1 means "data is ready"

          release ensures:
          - data write happens BEFORE this store becomes visible
          */
          cell.sequence.store(pos + 1, std::memory_order_release);

          return true;
        }

        // CAS failed → another thread moved enqueue_pos
        // retry with updated pos

      } else if (diff < 0) {
        /*
        seq < pos → this slot has not been consumed yet
        buffer is FULL (producer would overwrite unread data)
        */
        return false;

      } else {
        /*
        seq > pos → another producer already moved ahead
        reload latest position and retry
        */
        pos = _enqueue_pos.load(std::memory_order_relaxed);
      }
    }
  }

  /*
  consume(): called by SINGLE consumer thread

  Goal:
  - Check if next slot is ready
  - Read it
  - Mark slot reusable
  */
  std::optional<SignalEvent> consume() {
    // Map dequeue position → index
    Cell &cell = _cells[_dequeue_pos & (N - 1)];

    // Load sequence
    size_t seq = cell.sequence.load(std::memory_order_acquire);

    // Check if data is ready
    std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(_dequeue_pos + 1);

    if (diff != 0) {
      /*
      Not ready:
      - producer hasn't written yet
      OR
      - slot belongs to a future cycle
      */
      return std::nullopt;
    }

    // Data is ready → read it
    SignalEvent event = std::move(cell.data);

    /*
    Mark slot as reusable.

    sequence = dequeue_pos + N means:
    "this slot is now free for the NEXT cycle"

    release ensures:    consumer finishes reading before slot is reused
    */
    cell.sequence.store(_dequeue_pos + N, std::memory_order_release);

    // Move to next position
    _dequeue_pos++;

    return event;
  }

private:
  /*
  Each slot in the ring buffer.

  alignas ensures each Cell starts on a new cache line
  → prevents false sharing between threads.
  */
  struct alignas(CACHE_LINE_SIZE) Cell {
    std::atomic<size_t> sequence; // controls state of slot
    SignalEvent data;             // actual payload
  };

  /*
  Ring buffer storage.
  Also aligned to avoid sharing cache lines with other variables.
  */
  alignas(CACHE_LINE_SIZE) std::array<Cell, N> _cells{};

  /*
  Global write position (multi-producer)
  atomic → multiple threads modify it safely
  */
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> _enqueue_pos{0};

  /*
  Global read position (single consumer)
  does NOT need to be atomic because only one thread updates it
  */
  alignas(CACHE_LINE_SIZE) size_t _dequeue_pos{0};
};

} // namespace sd
