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

  // Cancels are immediate/un-acked (Simulator never fires OnOrderAck for
  // one) and aren't gated by action_pending_.
  if (!desired.has_bid && bid_.has_resting) {
    intents.Cancel(bid_.order_id);
    bid_.has_resting = false;
  }
  if (!desired.has_ask && ask_.has_resting) {
    intents.Cancel(ask_.order_id);
    ask_.has_resting = false;
  }

  if (action_pending_) {
    // Wait for the one in-flight Submit/Modify (either side) to resolve --
    // see the class comment on why only one may ever be in flight at a
    // time.
    return;
  }
  if (desired.has_bid &&
      TryRequoteSide(bid_, desired.bid_price, desired.bid_quantity, Side::Buy, now, intents)) {
    return;
  }
  if (desired.has_ask) {
    TryRequoteSide(ask_, desired.ask_price, desired.ask_quantity, Side::Sell, now, intents);
  }
}

bool MarketMaker::TryRequoteSide(SideState& state, Price desired_price, Quantity desired_quantity,
                                 Side side, Timestamp now, sim::OrderIntentSink& intents) {
  if (!state.has_resting) {
    NewOrderCommand command;
    command.side = side;
    command.type = OrderType::PostOnly;
    command.price = desired_price;
    command.quantity = desired_quantity;
    command.timestamp = now;
    state.order_id = intents.Submit(command);
    state.resting_price = desired_price;
    state.resting_quantity = desired_quantity;
    action_pending_ = true;
    return true;
  }
  if (state.resting_price != desired_price || state.resting_quantity != desired_quantity) {
    intents.Modify(state.order_id, desired_price, desired_quantity);
    state.resting_price = desired_price;
    state.resting_quantity = desired_quantity;
    action_pending_ = true;
    return true;
  }
  return false;
}

std::optional<double> MarketMaker::ReferenceMid(const sim::BookSnapshot& snapshot) const {
  if (!snapshot.has_bid || !snapshot.has_ask) {
    return has_reference_mid_ ? std::optional<double>(reference_mid_) : std::nullopt;
  }

  bool bid_is_entirely_ours = bid_.has_resting && snapshot.best_bid == bid_.resting_price &&
                              snapshot.best_bid_quantity <= bid_.resting_quantity;
  bool ask_is_entirely_ours = ask_.has_resting && snapshot.best_ask == ask_.resting_price &&
                              snapshot.best_ask_quantity <= ask_.resting_quantity;
  if (bid_is_entirely_ours && ask_is_entirely_ours) {
    return has_reference_mid_ ? std::optional<double>(reference_mid_) : std::nullopt;
  }

  reference_mid_ =
      (static_cast<double>(snapshot.best_bid) + static_cast<double>(snapshot.best_ask)) / 2.0;
  has_reference_mid_ = true;
  return reference_mid_;
}

void MarketMaker::OnOrderAck(OrderId id, const AddOrderResult& result) {
  // Simulator only fires this for a Submit/Modify, and only one is ever in
  // flight globally -- this must be it.
  action_pending_ = false;
  ApplyAck(bid_, id, result);
  ApplyAck(ask_, id, result);
}

void MarketMaker::ApplyAck(SideState& state, OrderId id, const AddOrderResult& result) {
  if (state.order_id != id) {
    return;
  }
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
