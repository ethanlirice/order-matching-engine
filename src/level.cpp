#include "lob/level.hpp"

namespace lob {

Level::Level(Price price) : price_(price) {}

Price Level::price() const {
  return price_;
}

bool Level::empty() const {
  return head_ == nullptr;
}

Quantity Level::total_quantity() const {
  Quantity total = 0;
  for (Order* order = head_; order != nullptr; order = order->next) {
    total += order->quantity;
  }
  return total;
}

Order* Level::front() const {
  return head_;
}

void Level::push_back(Order* order) {
  order->prev = tail_;
  order->next = nullptr;
  if (tail_ != nullptr) {
    tail_->next = order;
  } else {
    head_ = order;
  }
  tail_ = order;
}

Order* Level::pop_front() {
  if (head_ == nullptr) {
    return nullptr;
  }
  Order* front = head_;
  head_ = front->next;
  if (head_ != nullptr) {
    head_->prev = nullptr;
  } else {
    tail_ = nullptr;
  }
  front->next = nullptr;
  front->prev = nullptr;
  return front;
}

void Level::remove(Order* order) {
  if (order->prev != nullptr) {
    order->prev->next = order->next;
  } else {
    head_ = order->next;
  }
  if (order->next != nullptr) {
    order->next->prev = order->prev;
  } else {
    tail_ = order->prev;
  }
  order->prev = nullptr;
  order->next = nullptr;
}

}  // namespace lob
