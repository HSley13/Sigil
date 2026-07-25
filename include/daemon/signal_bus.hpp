#pragma once

#include "signal_event.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace sd {

/*
 * Cache line size used to pad ring buffer cells and the hot atomics
 * apart from each other. Without this, two threads writing to
 * *different* cells that happen to share a cache line still bounce
 * that line between cores on every write — a performance cliff known
 * as false sharing. Padding each Cell to a full cache line (and each
 * top-level atomic too) means every producer/consumer touches its own
 * cache line and nothing else.
 */
inline constexpr size_t CACHE_LINE_SIZE = 64;

/*
 * SignalBus — multi-producer, single-consumer, lock-free ring buffer
 *
 * This is the one component every source plugin in the system talks
 * to, and the only component the rule engine reads from. It is the
 * sole meeting point between the "source" world (source_base.hpp)
 * and the "processing" world (rule_engine, built next) — and
 * deliberately knows nothing about either.
 *
 * WHY LOCK-FREE, WHY NOT A MUTEX:
 * A daemon on this project can easily have 5-10 source threads alive
 * at once (pull threads for dht22/bmp280, push threads for adsb,
 * a stream thread for a microphone, a bidirectional thread for a
 * UART device...). A plain `std::mutex`-guarded queue would force
 * every one of those threads to serialize through a single lock on
 * every single event — including the microphone's StreamSource,
 * which wants to publish roughly every 46ms. Lock-free means threads
 * only ever spin briefly against each other on a slot claim, never
 * block, and never wait on the OS scheduler to hand a lock back.
 *
 * WHY THIS ISN'T JUST "ONE CAS ON A SHARED HEAD POINTER":
 * A naive multi-producer buffer that does
 *     head_.compare_exchange_weak(head, head + 1);
 *     buf_[head] = std::move(event);
 * has a real bug: the CAS makes head_ visible to the consumer BEFORE
 * the data write into buf_[head] has actually happened. A consumer
 * checking "tail_ != head_" can see the advanced head_ and read the
 * cell while the producer is still mid-write — a torn read, and a
 * genuine data race that ThreadSanitizer will catch.
 *
 * THE FIX — PER-CELL SEQUENCE NUMBERS (Vyukov's bounded MPMC design,
 * simplified here for a single consumer):
 * Every cell owns its own atomic "sequence" number, initialized to
 * its own index. A cell's sequence number IS its readiness signal —
 * completely separate from the shared enqueue_pos_/dequeue_pos_
 * counters, which exist only to hand out ticket numbers atomically.
 * A producer only writes to cells_[pos].data after it has exclusively
 * won that exact ticket via CAS, and only makes that write visible by
 * releasing the cell's own sequence number afterward. The consumer
 * only reads a cell once it has acquire-observed that release. This
 * is what makes concurrent publish() calls provably safe: two threads
 * racing for the same ticket can never both win, and neither can ever
 * be read from before its write completes.
 *
 * USAGE:
 *   sd::SignalBus<1024> bus;
 *
 *   // from any number of producer threads, any time:
 *   bus.publish(SignalEvent{...});
 *
 *   // from exactly one consumer thread (the rule engine), forever:
 *   if (auto event = bus.consume()) { ... }
 */
template <size_t N = 1024>
class SignalBus {
  static_assert(N >= 2, "SignalBus needs at least 2 slots to be meaningful");
  static_assert((N & (N - 1)) == 0,
                "N must be a power of 2 — this lets the buffer use the "
                "fast 'pos & (N - 1)' index instead of the slower "
                "'pos % N' modulo operator on every publish/consume call");

public:
  SignalBus() {
    // Every cell starts "ready for the ticket equal to its own
    // index" — this is lap 0 of the ring. Single-threaded here
    // (the constructor runs before any producer/consumer thread
    // exists), so no synchronization is needed for this loop.
    for (size_t i = 0; i < N; ++i)
      cells_[i].sequence.store(i, std::memory_order_relaxed);
  }

  // The bus owns a fixed buffer and fixed atomics tied to specific
  // memory addresses — copying or moving it would be meaningless.
  SignalBus(const SignalBus &) = delete;
  SignalBus &operator=(const SignalBus &) = delete;

  /*
   * publish(): call from ANY producer thread, any number of them,
   * at the same time — this is the whole point of the class.
   *
   * Every source plugin's callback (EventCallback, ChunkCallback,
   * DataCallback, ResultCallback from source_base.hpp — all four
   * are the same std::function<void(SignalEvent)> shape) ultimately
   * does nothing but call this method.
   *
   * Returns false only when the buffer is completely full, meaning
   * the consumer (rule engine) has fallen behind by N events. This
   * should be rare in practice — N defaults to 1024 — but when it
   * happens the event is dropped and the daemon should log it as a
   * warning rather than block the producer thread.
   */
  bool publish(SignalEvent event) {
    size_t pos = enqueue_pos_.load(std::memory_order_relaxed);

    for (;;) {
      Cell &cell = cells_[pos & (N - 1)];

      // Acquire: if this cell was previously used (wraparound),
      // this pairs with the consumer's release-store when it
      // last freed this cell — guaranteeing we don't overwrite
      // cell.data while the consumer might still be reading it.
      size_t seq = cell.sequence.load(std::memory_order_acquire);

      auto diff = static_cast<std::intptr_t>(seq) -
                  static_cast<std::intptr_t>(pos);

      if (diff == 0) {
        // This cell is exactly ready for our ticket. Try to
        // claim it. Weak, not strong: we're already in a
        // retry loop, so a spurious failure costs nothing —
        // and on ARM (this project's Pi target), strong CAS
        // loops internally anyway to guard against exactly
        // that, so weak avoids paying for a hidden retry
        // loop inside our own retry loop.
        if (enqueue_pos_.compare_exchange_weak(
                pos, pos + 1,
                std::memory_order_relaxed,
                std::memory_order_relaxed)) {
          // We now exclusively own this cell. No other
          // thread can also win this exact CAS — that's
          // the guarantee compare_exchange gives us.
          cell.data = std::move(event);

          // Release: makes the write above visible to
          // whichever thread later acquire-loads this
          // exact value. This is the signal "my data is
          // safely written, you may read it now" — and it
          // is what makes the naive design's torn read
          // impossible here.
          cell.sequence.store(pos + 1, std::memory_order_release);
          return true;
        }
        // CAS failed: another producer won this ticket first.
        // compare_exchange_weak already refreshed `pos` to
        // the current real value — loop and try again with it.
      } else if (diff < 0) {
        // seq is BEHIND our ticket: the consumer hasn't freed
        // this cell yet. The buffer is genuinely full.
        return false;
      } else {
        // diff > 0: someone else already claimed and moved
        // past this exact position while we were reading it.
        // Reload the real position and try again.
        pos = enqueue_pos_.load(std::memory_order_relaxed);
      }
    }
  }

  /*
   * consume(): call ONLY from the single rule engine thread.
   *
   * This class intentionally does not support multiple consumers —
   * dequeue_pos_ is a plain size_t, not an atomic, specifically
   * because the daemon only ever has one rule engine thread reading
   * from the bus. Supporting multiple consumers safely would need
   * dequeue_pos_ to also be a CAS-guarded atomic (the full Vyukov
   * MPMC design) — unnecessary complexity for a single-reader system.
   */
  std::optional<SignalEvent> consume() {
    Cell &cell = cells_[dequeue_pos_ & (N - 1)];

    // Acquire: pairs with the producer's release-store above,
    // guaranteeing that once we observe this sequence value, the
    // write to cell.data is fully visible to us.
    size_t seq = cell.sequence.load(std::memory_order_acquire);

    auto diff = static_cast<std::intptr_t>(seq) -
                static_cast<std::intptr_t>(dequeue_pos_ + 1);

    if (diff != 0)
      return std::nullopt; // nothing published here yet

    SignalEvent event = std::move(cell.data);

    // Release: frees this cell for the NEXT lap around the ring
    // (ticket dequeue_pos_ + N) and pairs with a future producer's
    // acquire-load on this same cell, so that producer knows our
    // read is fully complete before it overwrites cell.data.
    cell.sequence.store(dequeue_pos_ + N, std::memory_order_release);
    ++dequeue_pos_;

    return event;
  }

  // Number of slots in the ring. Useful for daemon-side logging —
  // e.g. warning when publish() starts returning false often.
  static constexpr size_t capacity() { return N; }

private:
  struct alignas(CACHE_LINE_SIZE) Cell {
    std::atomic<size_t> sequence;
    SignalEvent data;
  };

  alignas(CACHE_LINE_SIZE) std::array<Cell, N> cells_{};
  alignas(CACHE_LINE_SIZE) std::atomic<size_t> enqueue_pos_{0};
  alignas(CACHE_LINE_SIZE) size_t dequeue_pos_{0};
};

} // namespace sd
