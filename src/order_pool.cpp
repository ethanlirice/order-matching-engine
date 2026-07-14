#include "lob/order_pool.hpp"

#include <cassert>
#include <limits>

namespace lob {

namespace {
#ifndef NDEBUG
constexpr OrderId kPoisonId = std::numeric_limits<OrderId>::max();
constexpr Quantity kPoisonQuantity = std::numeric_limits<Quantity>::max();
#endif
}  // namespace

OrderPool::OrderPool(std::size_t initial_capacity) : chunk_size_(initial_capacity) {
  AddChunk(chunk_size_);
}

void OrderPool::AddChunk(std::size_t size) {
  assert(size > 0);
  auto chunk = std::make_unique<Order[]>(size);
  for (std::size_t i = 0; i < size; ++i) {
    chunk[i].next = free_list_;
    free_list_ = &chunk[i];
  }
  chunks_.push_back(std::move(chunk));
}

Order* OrderPool::Acquire() {
  if (free_list_ == nullptr) {
    AddChunk(chunk_size_);
  }
  Order* slot = free_list_;
  free_list_ = slot->next;
  *slot = Order{};
  return slot;
}

void OrderPool::Release(Order* order) {
#ifndef NDEBUG
  order->id = kPoisonId;
  order->quantity = kPoisonQuantity;
#endif
  order->next = free_list_;
  order->prev = nullptr;
  free_list_ = order;
}

std::size_t OrderPool::chunk_count() const {
  return chunks_.size();
}

}  // namespace lob
