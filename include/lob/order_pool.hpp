#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "lob/order.hpp"

namespace lob {

// Chunked, free-list-backed pool for Order objects: after warm-up (growing
// to the working-set size), Acquire()/Release() perform zero heap
// allocation. Not thread-safe -- single-threaded matching-thread use only;
// a future feed/matching thread split (see SpscRingBuffer) must not share
// a pool instance across threads.
class OrderPool {
 public:
  explicit OrderPool(std::size_t initial_capacity);

  // Returns a slot reset to a clean default state -- not reliant on the
  // caller doing a full struct copy-assignment afterward to erase any
  // prior occupant's data.
  Order* Acquire();

  // Returns a slot to the free list. In debug builds, poisons the slot's
  // economic fields first: pooling removes ASan's use-after-free detection
  // for this object type (a stale Order* now reads valid, reused memory
  // instead of freed memory), so a latent stale-pointer bug fails loudly
  // via a wrong-value assertion instead of silently "working."
  void Release(Order* order);

  // Testability: proves "zero allocation after warm-up" empirically in
  // benchmarks/tests -- how many times Acquire() had to grow the pool.
  std::size_t chunk_count() const;

 private:
  void AddChunk(std::size_t size);

  std::size_t chunk_size_;
  // Each chunk's Order[] array is stable/never moved once allocated; only
  // this outer vector of handles can reallocate as it grows, which doesn't
  // move the arrays themselves -- same reasoning as order_index_'s
  // rehash-safety (include/lob/order_book.hpp).
  std::vector<std::unique_ptr<Order[]>> chunks_;
  Order* free_list_ = nullptr;  // singly-linked via Order::next
};

}  // namespace lob
