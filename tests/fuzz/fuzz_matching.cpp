#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "lob/order_book.hpp"
#include "support/test_helpers.hpp"

// libFuzzer harness (coverage-guided): decodes the mutated byte buffer into
// a deterministic sequence of add/cancel/modify ops applied to one fresh
// OrderBook, checking invariants after every op. A crash/abort is how
// libFuzzer registers a finding -- GTest-style EXPECT would not stop the
// fuzzer, so violations here call std::abort() directly.
//
// Exercises all 5 OrderType values (Limit, Market, IOC, FOK, PostOnly).

namespace {

class ByteCursor {
 public:
  ByteCursor(const std::uint8_t* data, std::size_t size) : data_(data), size_(size) {}

  bool HasBytes(std::size_t n) const { return pos_ + n <= size_; }

  std::uint8_t NextByte() { return pos_ < size_ ? data_[pos_++] : 0; }

 private:
  const std::uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

void CheckOrAbort(const lob::OrderBook& book, const char* what) {
  std::string reason;
  if (!lob::testing::CheckInvariants(book, &reason)) {
    std::fprintf(stderr, "invariant violated after %s: %s\n", what, reason.c_str());
    std::abort();
  }
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  using lob::Order;
  using lob::OrderBook;
  using lob::OrderId;
  using lob::OrderType;
  using lob::Price;
  using lob::Quantity;
  using lob::Side;

  ByteCursor cursor(data, size);
  OrderBook book;
  std::vector<OrderId> known_ids;
  OrderId next_id = 1;

  while (cursor.HasBytes(6)) {
    std::uint8_t kind_byte = cursor.NextByte();
    std::uint8_t side_byte = cursor.NextByte();
    std::uint8_t price_byte = cursor.NextByte();
    std::uint8_t qty_byte = cursor.NextByte();
    std::uint8_t id_select_byte = cursor.NextByte();
    std::uint8_t extra_price_byte = cursor.NextByte();

    Side side = (side_byte & 1) != 0 ? Side::Sell : Side::Buy;
    Price price = 95 + static_cast<Price>(price_byte % 11);   // small range: frequent crossing
    Quantity qty = 1 + static_cast<Quantity>(qty_byte % 20);  // avoid the trivial zero-qty path

    int op_choice = kind_byte % 10;
    if (op_choice <= 6 || known_ids.empty()) {
      static constexpr OrderType kTypes[] = {OrderType::Limit, OrderType::Market, OrderType::IOC,
                                             OrderType::FOK, OrderType::PostOnly};
      OrderType type = kTypes[kind_byte % 5];
      OrderId id = next_id++;
      Order order = lob::testing::MakeOrder(id, side, type, price, qty);

      lob::AddOrderResult result = book.add_order(order);
      if (qty != result.filled_quantity + result.resting_quantity + result.cancelled_quantity) {
        std::fprintf(stderr, "quantity conservation violated on add id=%llu\n",
                     static_cast<unsigned long long>(id));
        std::abort();
      }
      known_ids.push_back(id);
      CheckOrAbort(book, "add_order");
    } else if (op_choice <= 8) {
      OrderId target = known_ids[id_select_byte % known_ids.size()];
      const Order* before = book.debug_peek(target);
      bool expected_present = before != nullptr;
      Quantity expected_quantity = expected_present ? before->quantity : 0;

      std::optional<Quantity> cancelled = book.cancel_order(target);
      if (cancelled.has_value() != expected_present) {
        std::fprintf(stderr, "cancel_order presence mismatch for id=%llu\n",
                     static_cast<unsigned long long>(target));
        std::abort();
      }
      if (cancelled.has_value() && *cancelled != expected_quantity) {
        std::fprintf(stderr, "cancel_order returned wrong quantity for id=%llu\n",
                     static_cast<unsigned long long>(target));
        std::abort();
      }
      CheckOrAbort(book, "cancel_order");
    } else {
      OrderId target = known_ids[id_select_byte % known_ids.size()];
      bool expected_present = book.contains(target);
      Price new_price = 95 + static_cast<Price>(extra_price_byte % 11);

      std::optional<lob::AddOrderResult> result = book.modify_order(target, new_price, qty);
      if (result.has_value() != expected_present) {
        std::fprintf(stderr, "modify_order presence mismatch for id=%llu\n",
                     static_cast<unsigned long long>(target));
        std::abort();
      }
      if (result.has_value() &&
          qty != result->filled_quantity + result->resting_quantity + result->cancelled_quantity) {
        std::fprintf(stderr, "quantity conservation violated on modify id=%llu\n",
                     static_cast<unsigned long long>(target));
        std::abort();
      }
      CheckOrAbort(book, "modify_order");
    }
  }

  return 0;
}
