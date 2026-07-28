#pragma once
#include "signal_event.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <utility>

namespace sd {

// Cache line size, used to align data so threads don't false-share a line.
inline constexpr size_t CACHE_LINE_SIZE = 64;

// Bounded lock-free MPSC ring buffer (Vyukov-style): many producer threads call
// publish(), one consumer thread calls consume(). Each cell's sequence number
// doubles as its state — sequence == position means the slot is free for that
// position — which is how producers and the consumer hand slots off without locks.
template <size_t N = 1024>
class SignalBus {
  static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of 2, at least 2");

public:
  SignalBus() {
    for (size_t i{0}; i < N; i++) {
      _cells[i].sequence.store(i, std::memory_order_relaxed);
    }
  }

  SignalBus(const SignalBus &) = delete;
  SignalBus &operator=(const SignalBus &) = delete;

  // Safe to call from multiple producer threads concurrently.
  bool publish(SignalEvent event) {
    size_t pos = _enqueue_pos.load(std::memory_order_relaxed);

    for (;;) {
      Cell &cell = _cells[pos & (N - 1)];
      size_t seq = cell.sequence.load(std::memory_order_acquire);
      std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(pos);

      if (diff == 0) {
        // Slot is free for this position; try to claim it.
        if (_enqueue_pos.compare_exchange_weak(pos, pos + 1, std::memory_order_relaxed, std::memory_order_relaxed)) {
          cell.data = std::move(event);
          // release: publishes the write above before the slot is seen as ready.
          cell.sequence.store(pos + 1, std::memory_order_release);
          return true;
        }
        // Lost the CAS race for this slot; retry with the updated pos.

      } else if (diff < 0) {
        // Slot hasn't been consumed yet -> buffer is full.
        return false;

      } else {
        // Another producer already moved ahead; reload and retry.
        pos = _enqueue_pos.load(std::memory_order_relaxed);
      }
    }
  }

  // Only safe to call from a single consumer thread.
  std::optional<SignalEvent> consume() {
    Cell &cell = _cells[_dequeue_pos & (N - 1)];
    size_t seq = cell.sequence.load(std::memory_order_acquire);
    std::intptr_t diff = static_cast<std::intptr_t>(seq) - static_cast<std::intptr_t>(_dequeue_pos + 1);

    if (diff != 0) {
      return std::nullopt; // producer hasn't published this slot yet
    }

    SignalEvent event = std::move(cell.data);
    // release: hands the slot back for its next cycle only after the read completes.
    cell.sequence.store(_dequeue_pos + N, std::memory_order_release);
    _dequeue_pos++;

    return event;
  }

private:
  // alignas prevents false sharing between adjacent cells.
  struct alignas(CACHE_LINE_SIZE) Cell {
    std::atomic<size_t> sequence;
    SignalEvent data;
  };

  alignas(CACHE_LINE_SIZE) std::array<Cell, N> _cells{};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> _enqueue_pos{0};
  alignas(CACHE_LINE_SIZE) size_t _dequeue_pos{0}; // not atomic: single consumer only
};

} // namespace sd
