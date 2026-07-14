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
  Quantity total_quantity() const;

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
  [[maybe_unused]] Order* tail_ = nullptr;
  Quantity total_quantity_ = 0;
};

}  // namespace lob
