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
    } else {
      ApplyStrategyIntent(std::get<StrategyIntent>(event.payload));
    }
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
    HandleTrades(result.trades);
  } else {
    engine_.cancel_order(message.cancel_id);
    HandleTrades({});
  }
}

void Simulator::ApplyStrategyIntent(const StrategyIntent& intent) {
  switch (intent.kind) {
    case StrategyIntent::Kind::Submit: {
      Order order;
      order.id = intent.submit.id;
      order.side = intent.submit.side;
      order.type = intent.submit.type;
      order.price = intent.submit.price;
      order.quantity = intent.submit.quantity;
      order.timestamp = intent.submit.timestamp;

      AddOrderResult result = engine_.submit_order(order);
      HandleTrades(result.trades);
      break;
    }
    case StrategyIntent::Kind::Cancel: {
      engine_.cancel_order(intent.target_id);
      HandleTrades({});
      break;
    }
    case StrategyIntent::Kind::Modify: {
      auto result =
          engine_.modify_order(intent.target_id, intent.modify_price, intent.modify_quantity);
      HandleTrades(result.has_value() ? result->trades : std::vector<TradeEvent>{});
      break;
    }
  }
}

void Simulator::HandleTrades(const std::vector<TradeEvent>& trades) {
  for (const TradeEvent& trade : trades) {
    trade_log_.push_back(trade);
  }

  if (strategy_ == nullptr) {
    return;
  }

  for (const TradeEvent& trade : trades) {
    strategy_->OnTrade(trade, clock_.now(), *this);
  }

  BookSnapshot snapshot;
  Price price = 0;
  if (engine_.book().best_bid(price)) {
    snapshot.has_bid = true;
    snapshot.best_bid = price;
  }
  if (engine_.book().best_ask(price)) {
    snapshot.has_ask = true;
    snapshot.best_ask = price;
  }
  strategy_->OnBookUpdate(snapshot, clock_.now(), *this);
}

Event Simulator::MakeStrategyEvent(StrategyIntent intent) {
  Event event;
  event.timestamp = clock_.now() + latency_;
  event.kind = EventKind::StrategyOrderArrival;
  event.sequence = next_sequence_++;
  event.payload = std::move(intent);
  return event;
}

OrderId Simulator::Submit(NewOrderCommand command) {
  command.id = next_strategy_id_++;
  StrategyIntent intent;
  intent.kind = StrategyIntent::Kind::Submit;
  intent.submit = command;
  queue_.push(MakeStrategyEvent(std::move(intent)));
  return command.id;
}

void Simulator::Cancel(OrderId id) {
  StrategyIntent intent;
  intent.kind = StrategyIntent::Kind::Cancel;
  intent.target_id = id;
  queue_.push(MakeStrategyEvent(std::move(intent)));
}

void Simulator::Modify(OrderId id, Price new_price, Quantity new_quantity) {
  StrategyIntent intent;
  intent.kind = StrategyIntent::Kind::Modify;
  intent.target_id = id;
  intent.modify_price = new_price;
  intent.modify_quantity = new_quantity;
  queue_.push(MakeStrategyEvent(std::move(intent)));
}

const OrderBook& Simulator::DebugBook() const {
  return engine_.book();
}

}  // namespace lob::sim
