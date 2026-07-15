#include "lob/sim/simulator.hpp"

#include <utility>

namespace lob::sim {

void Simulator::LoadEvents(std::vector<Event> events) {
  for (auto& event : events) {
    queue_.push(std::move(event));
  }
}

void Simulator::Run() {
  while (!queue_.empty()) {
    Event event = queue_.top();
    queue_.pop();
    clock_.advance_to(event.timestamp);

    if (event.kind == EventKind::Replay) {
      ApplyReplayMessage(std::get<ReplayMessage>(event.payload));
    }
    // StrategyOrderArrival handling lands in M4 Step 3.
  }
}

void Simulator::ApplyReplayMessage(const ReplayMessage& message) {
  if (message.kind == ReplayMessage::Kind::Add) {
    Order order;
    order.id = message.add.id;
    order.side = message.add.side;
    order.type = message.add.type;
    order.price = message.add.price;
    order.quantity = message.add.quantity;
    order.timestamp = message.add.timestamp;

    AddOrderResult result = engine_.submit_order(order);
    for (const TradeEvent& trade : result.trades) {
      trade_log_.push_back(trade);
    }
  } else {
    engine_.cancel_order(message.cancel_id);
  }
}

const OrderBook& Simulator::DebugBook() const {
  return engine_.book();
}

}  // namespace lob::sim
