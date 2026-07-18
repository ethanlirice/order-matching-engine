#include "lob/matching_engine.hpp"

#include <utility>

namespace lob {

void MatchingEngine::set_trade_callback(TradeCallback callback) {
  trade_callback_ = std::move(callback);
}

AddOrderResult MatchingEngine::submit_order(const Order& order) {
  AddOrderResult result = book_.add_order(order);
  if (trade_callback_) {
    for (const TradeEvent& trade : result.trades) {
      trade_callback_(trade);
    }
  }
  return result;
}

std::optional<Quantity> MatchingEngine::cancel_order(OrderId id) {
  return book_.cancel_order(id);
}

std::optional<Quantity> MatchingEngine::ReduceQuantity(OrderId id, Quantity new_quantity) {
  return book_.ReduceQuantity(id, new_quantity);
}

std::optional<AddOrderResult> MatchingEngine::modify_order(OrderId id, Price new_price,
                                                           Quantity new_quantity) {
  std::optional<AddOrderResult> result = book_.modify_order(id, new_price, new_quantity);
  if (result.has_value() && trade_callback_) {
    for (const TradeEvent& trade : result->trades) {
      trade_callback_(trade);
    }
  }
  return result;
}

}  // namespace lob
