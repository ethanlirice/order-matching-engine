#pragma once

#include <functional>
#include <optional>

#include "lob/order.hpp"
#include "lob/order_book.hpp"

namespace lob {

// Thin wrapper around a single-symbol OrderBook that owns trade-event
// emission. This is the boundary L2 (strategies) talks to -- it knows
// nothing about strategies itself (§4: clean layer boundaries). All
// matching logic lives in OrderBook; this class only delegates and fans
// out trade events to the registered callback.
class MatchingEngine {
 public:
  using TradeCallback = std::function<void(const TradeEvent&)>;

  MatchingEngine() = default;

  void set_trade_callback(TradeCallback callback);

  AddOrderResult submit_order(const Order& order);
  std::optional<Quantity> cancel_order(OrderId id);
  std::optional<AddOrderResult> modify_order(OrderId id, Price new_price, Quantity new_quantity);

 private:
  OrderBook book_;
  TradeCallback trade_callback_;
};

}  // namespace lob
