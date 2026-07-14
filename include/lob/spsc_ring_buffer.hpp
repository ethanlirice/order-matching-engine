#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace lob {

// Single-producer/single-consumer lock-free ring buffer (LMAX Disruptor
// pattern, PROJECT_SPEC.md §6) -- decouples a feed thread (producer) from
// the matching thread (consumer) without mutex jitter. TryPush is producer-
// thread-only; TryPop is consumer-thread-only. Holds Capacity - 1 usable
// slots by design (the standard way to disambiguate empty from full with
// only two indices and no separate count field) -- not an off-by-one bug.
//
// Correctness before speed: this is the straightforwardly-correct version
// (plain acquire/release on both indices). A well-known further
// optimization -- each side caching a local snapshot of the other's index
// to cut redundant atomic loads -- is deliberately not done here; revisit
// only if a benchmark shows contention on these loads actually matters.
template <typename T, std::size_t Capacity>
class SpscRingBuffer {
  static_assert(Capacity >= 2, "Capacity must be at least 2");
  static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");

 public:
  // Producer thread only. Returns false if the buffer is full.
  bool TryPush(const T& item) {
    std::size_t w = write_index_.load(std::memory_order_relaxed);
    std::size_t next = Advance(w);
    if (next == read_index_.load(std::memory_order_acquire)) {
      return false;  // full
    }
    buffer_[w] = item;
    write_index_.store(next, std::memory_order_release);
    return true;
  }

  // Consumer thread only. Returns false if the buffer is empty.
  bool TryPop(T& out) {
    std::size_t r = read_index_.load(std::memory_order_relaxed);
    if (r == write_index_.load(std::memory_order_acquire)) {
      return false;  // empty
    }
    out = buffer_[r];
    read_index_.store(Advance(r), std::memory_order_release);
    return true;
  }

 private:
  static std::size_t Advance(std::size_t index) { return (index + 1) & (Capacity - 1); }

  // Separate cache lines: this is the textbook false-sharing fix, and
  // where alignment actually matters most in this design -- the producer
  // writes write_index_ every push, the consumer writes read_index_ every
  // pop, and each side also reads the other's index, so without this the
  // two threads would constantly invalidate a shared line between cores.
  alignas(64) std::atomic<std::size_t> write_index_{0};
  alignas(64) std::atomic<std::size_t> read_index_{0};
  std::array<T, Capacity> buffer_{};
};

}  // namespace lob
