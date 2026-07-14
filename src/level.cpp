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
  return total_quantity_;
}

void Level::push_back(Order* /*order*/) {
  // Intrusive-list append: implemented in M1.
}

Order* Level::pop_front() {
  // Intrusive-list pop: implemented in M1.
  return nullptr;
}

void Level::remove(Order* /*order*/) {
  // Intrusive-list unlink: implemented in M1.
}

}  // namespace lob
