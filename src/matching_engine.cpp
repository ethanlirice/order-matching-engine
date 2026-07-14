#include "lob/matching_engine.hpp"

namespace lob {

MatchingEngine::MatchingEngine() = default;

void MatchingEngine::set_trade_callback(TradeCallback callback) {
  trade_callback_ = std::move(callback);
}

void MatchingEngine::submit_order(const Order& /*order*/) {
  // Delegates to OrderBook and emits trade events: implemented in M1.
}

void MatchingEngine::cancel_order(OrderId /*id*/) {
  // Delegates to OrderBook: implemented in M1.
}

}  // namespace lob
