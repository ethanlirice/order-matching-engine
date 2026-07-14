#pragma once

#include "lob/order.hpp"

namespace lob {

// A single price level: a FIFO queue of resting orders backed by an
// intrusive doubly-linked list (append-at-tail / unlink are O(1), no
// per-operation heap allocation). Interface only for M0 -- the linked-list
// logic is implemented in M1.
class Level {
 public:
  explicit Level(Price price);

  Price price() const;
  bool empty() const;

  // Walks the list to sum resting quantity. Not O(1) -- deliberate M1
  // simplification (correctness before speed); revisit only if M3
  // profiling shows it matters.
  Quantity total_quantity() const;

  // Peeks the order at the head of the queue without unlinking it (used by
  // matching to inspect/partially-fill the maker in place).
  Order* front() const;

  // Appends to the tail of the FIFO queue (time priority = arrival order).
  void push_back(Order* order);

  // Removes and returns the order at the head of the queue, or nullptr if
  // empty.
  Order* pop_front();

  // Unlinks an arbitrary order from this level (used by cancel/modify).
  void remove(Order* order);

 private:
  Price price_;
  Order* head_ = nullptr;
  Order* tail_ = nullptr;
};

}  // namespace lob
