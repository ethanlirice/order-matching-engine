#include "lob/mm/market_maker.hpp"

#include <cassert>

namespace lob::mm {

void MarketMaker::OnBookUpdate(const sim::BookSnapshot& snapshot, Timestamp now,
                               std::uint64_t /*event_ordinal*/, sim::OrderIntentSink& intents) {
  Quote desired = ComputeQuotes(snapshot, now);
  if (desired.has_bid && desired.has_ask) {
    // Should be mathematically impossible given each concrete strategy's
    // floor-clamping -- this catches a bug in that clamp itself, not a
    // runtime data condition.
    assert(desired.bid_price < desired.ask_price);
  }
  RequoteSide(bid_, desired.has_bid, desired.bid_price, desired.bid_quantity, Side::Buy, now,
              intents);
  RequoteSide(ask_, desired.has_ask, desired.ask_price, desired.ask_quantity, Side::Sell, now,
              intents);
}

void MarketMaker::RequoteSide(SideState& state, bool desired_has_quote, Price desired_price,
                              Quantity desired_quantity, Side side, Timestamp now,
                              sim::OrderIntentSink& intents) {
  if (state.pending_ack) {
    // Wait for the in-flight action to resolve before deciding anything
    // new for this side -- avoids issuing a second action against an id
    // whose fate isn't known yet.
    return;
  }
  if (!desired_has_quote) {
    if (state.has_resting) {
      intents.Cancel(state.order_id);
      state.has_resting = false;
    }
    return;
  }
  if (!state.has_resting) {
    NewOrderCommand command;
    command.side = side;
    command.type = OrderType::PostOnly;
    command.price = desired_price;
    command.quantity = desired_quantity;
    command.timestamp = now;
    state.order_id = intents.Submit(command);
    state.pending_ack = true;
    state.resting_price = desired_price;
    state.resting_quantity = desired_quantity;
    return;
  }
  if (state.resting_price != desired_price || state.resting_quantity != desired_quantity) {
    intents.Modify(state.order_id, desired_price, desired_quantity);
    state.pending_ack = true;
    state.resting_price = desired_price;
    state.resting_quantity = desired_quantity;
  }
}

void MarketMaker::OnOrderAck(OrderId id, const AddOrderResult& result) {
  ApplyAck(bid_, id, result);
  ApplyAck(ask_, id, result);
}

void MarketMaker::ApplyAck(SideState& state, OrderId id, const AddOrderResult& result) {
  if (state.order_id != id || !state.pending_ack) {
    return;
  }
  state.pending_ack = false;
  if (result.resting_quantity > 0) {
    state.has_resting = true;
    state.resting_quantity = result.resting_quantity;
  } else {
    state.has_resting = false;
  }
}

void MarketMaker::OnTrade(const TradeEvent& trade, Timestamp /*now*/, std::uint64_t event_ordinal,
                          sim::OrderIntentSink& /*intents*/) {
  if (bid_.order_id != 0 && trade.maker_order_id == bid_.order_id) {
    ApplyFill(bid_, Side::Buy, trade, event_ordinal);
  } else if (ask_.order_id != 0 && trade.maker_order_id == ask_.order_id) {
    ApplyFill(ask_, Side::Sell, trade, event_ordinal);
  }
}

void MarketMaker::ApplyFill(SideState& state, Side side, const TradeEvent& trade,
                            std::uint64_t event_ordinal) {
  state.resting_quantity -= trade.size;
  if (state.resting_quantity == 0) {
    state.has_resting = false;
  }

  if (side == Side::Buy) {
    inventory_ += static_cast<Inventory>(trade.size);
    cash_ -= static_cast<double>(trade.size) * static_cast<double>(trade.price);
  } else {
    inventory_ -= static_cast<Inventory>(trade.size);
    cash_ += static_cast<double>(trade.size) * static_cast<double>(trade.price);
  }

  Fill fill;
  fill.timestamp = trade.timestamp;
  fill.event_ordinal = event_ordinal;
  fill.side = side;
  fill.price = trade.price;
  fill.quantity = trade.size;
  fill.inventory_after = inventory_;
  fills_.push_back(fill);
}

}  // namespace lob::mm
