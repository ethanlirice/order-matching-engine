#pragma once

#include <functional>

#include "lob/order.hpp"
#include "lob/order_book.hpp"

namespace lob {

// Thin wrapper around a single-symbol OrderBook that owns trade-event
// emission. This is the boundary L2 (strategies) talks to -- it knows
// nothing about strategies itself (§4: clean layer boundaries). Interface
// only for M0; submit/cancel wiring to OrderBook lands in M1.
class MatchingEngine {
 public:
  using TradeCallback = std::function<void(const TradeEvent&)>;

  MatchingEngine();

  void set_trade_callback(TradeCallback callback);

  void submit_order(const Order& order);
  void cancel_order(OrderId id);

 private:
  OrderBook book_;
  TradeCallback trade_callback_;
};

}  // namespace lob
