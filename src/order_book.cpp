#include "lob/order_book.hpp"

#include <algorithm>
#include <utility>

namespace lob {

namespace {

bool Crosses(Side incoming_side, Price incoming_price, Price opposite_price) {
  return incoming_side == Side::Buy ? incoming_price >= opposite_price
                                    : incoming_price <= opposite_price;
}

}  // namespace

template <typename OppositeMap>
void OrderBook::match_against(OppositeMap& opposite, Order& incoming,
                              std::vector<TradeEvent>& trades, Quantity& filled_quantity) {
  while (incoming.quantity > 0 && !opposite.empty()) {
    auto level_it = opposite.begin();
    Level& level = level_it->second;
    if (incoming.type != OrderType::Market &&
        !Crosses(incoming.side, incoming.price, level.price())) {
      break;
    }

    Order* maker = level.front();
    Quantity fill = std::min(incoming.quantity, maker->quantity);

    trades.push_back(TradeEvent{
        maker->price,
        fill,
        incoming.side,
        maker->id,
        incoming.id,
        incoming.timestamp,
    });
    filled_quantity += fill;
    incoming.quantity -= fill;
    maker->quantity -= fill;

    if (maker->quantity == 0) {
      level.pop_front();
      finalize_removal(opposite, level_it, maker);
    }
  }
}

template <typename LevelMap>
void OrderBook::finalize_removal(LevelMap& side_map, typename LevelMap::iterator level_it,
                                 Order* order) {
  if (level_it->second.empty()) {
    side_map.erase(level_it);
  }
  order_index_.erase(order->id);
}

template <typename OppositeMap>
bool OrderBook::CanFullyFill(const OppositeMap& opposite, Side side, Price price,
                             Quantity needed) const {
  Quantity available = 0;
  for (const auto& [level_price, level] : opposite) {
    if (!Crosses(side, price, level_price)) {
      break;
    }
    available += level.total_quantity();
    if (available >= needed) {
      return true;
    }
  }
  return available >= needed;
}

AddOrderResult OrderBook::add_order(Order incoming) {
  AddOrderResult result;

  // Duplicate-id rejection below is defined, testable API behavior (not
  // assumed-impossible programmer error), so it's always the graceful path
  // in every build config -- deliberately no assert() here: a debug-only
  // tripwire would abort before the unit test exercising this exact
  // rejection could observe the result.
  if (order_index_.contains(incoming.id)) {
    result.cancelled_quantity = incoming.quantity;
    return result;
  }
  if (incoming.quantity == 0) {
    return result;
  }

  // Post-Only: reject outright if it would cross at submission time --
  // never takes liquidity, not even partially.
  if (incoming.type == OrderType::PostOnly) {
    Price opposite_best = 0;
    bool has_opposite =
        incoming.side == Side::Buy ? best_ask(opposite_best) : best_bid(opposite_best);
    if (has_opposite && Crosses(incoming.side, incoming.price, opposite_best)) {
      result.cancelled_quantity = incoming.quantity;
      return result;
    }
  }

  // FOK: all-or-nothing. Reject the entire order, untouched, unless the
  // opposite side already has enough crossing liquidity to fill it in
  // full right now.
  if (incoming.type == OrderType::FOK) {
    bool fillable = incoming.side == Side::Buy
                        ? CanFullyFill(asks_, incoming.side, incoming.price, incoming.quantity)
                        : CanFullyFill(bids_, incoming.side, incoming.price, incoming.quantity);
    if (!fillable) {
      result.cancelled_quantity = incoming.quantity;
      return result;
    }
  }

  if (incoming.side == Side::Buy) {
    match_against(asks_, incoming, result.trades, result.filled_quantity);
  } else {
    match_against(bids_, incoming, result.trades, result.filled_quantity);
  }

  if (incoming.quantity > 0) {
    switch (incoming.type) {
      case OrderType::Market:
      case OrderType::IOC:
      case OrderType::FOK:
        // Never rest: Market/IOC drop any unfilled remainder, and FOK's
        // remainder is always zero here given the pre-check above.
        result.cancelled_quantity = incoming.quantity;
        break;
      case OrderType::Limit:
      case OrderType::PostOnly: {
        auto node = std::make_unique<Order>(incoming);
        Order* raw = node.get();
        order_index_.emplace(incoming.id, std::move(node));

        if (incoming.side == Side::Buy) {
          auto [level_it, inserted] = bids_.try_emplace(incoming.price, incoming.price);
          level_it->second.push_back(raw);
        } else {
          auto [level_it, inserted] = asks_.try_emplace(incoming.price, incoming.price);
          level_it->second.push_back(raw);
        }
        result.resting_quantity = incoming.quantity;
        break;
      }
    }
  }

  return result;
}

std::optional<Quantity> OrderBook::cancel_order(OrderId id) {
  auto it = order_index_.find(id);
  if (it == order_index_.end()) {
    return std::nullopt;
  }

  Order* order = it->second.get();
  Quantity remaining = order->quantity;

  if (order->side == Side::Buy) {
    auto level_it = bids_.find(order->price);
    level_it->second.remove(order);
    finalize_removal(bids_, level_it, order);
  } else {
    auto level_it = asks_.find(order->price);
    level_it->second.remove(order);
    finalize_removal(asks_, level_it, order);
  }

  return remaining;
}

std::optional<AddOrderResult> OrderBook::modify_order(OrderId id, Price new_price,
                                                      Quantity new_quantity) {
  auto it = order_index_.find(id);
  if (it == order_index_.end()) {
    return std::nullopt;
  }

  Order* existing = it->second.get();
  Side side = existing->side;
  OrderType type = existing->type;
  Timestamp timestamp = existing->timestamp;
  Quantity old_quantity = existing->quantity;

  if (side == Side::Buy) {
    auto level_it = bids_.find(existing->price);
    level_it->second.remove(existing);
    finalize_removal(bids_, level_it, existing);
  } else {
    auto level_it = asks_.find(existing->price);
    level_it->second.remove(existing);
    finalize_removal(asks_, level_it, existing);
  }

  AddOrderResult result;
  if (new_quantity == 0) {
    result.cancelled_quantity = old_quantity;
    return result;
  }

  Order fresh;
  fresh.id = id;
  fresh.side = side;
  fresh.type = type;
  fresh.price = new_price;
  fresh.quantity = new_quantity;
  fresh.timestamp = timestamp;
  return add_order(fresh);
}

bool OrderBook::best_bid(Price& out_price) const {
  if (bids_.empty()) {
    return false;
  }
  out_price = bids_.begin()->first;
  return true;
}

bool OrderBook::best_ask(Price& out_price) const {
  if (asks_.empty()) {
    return false;
  }
  out_price = asks_.begin()->first;
  return true;
}

bool OrderBook::contains(OrderId id) const {
  return order_index_.contains(id);
}

std::size_t OrderBook::order_count() const {
  return order_index_.size();
}

const Order* OrderBook::debug_peek(OrderId id) const {
  auto it = order_index_.find(id);
  return it == order_index_.end() ? nullptr : it->second.get();
}

std::vector<OrderId> OrderBook::resting_order_ids(Side side, Price price) const {
  std::vector<OrderId> ids;
  if (side == Side::Buy) {
    auto it = bids_.find(price);
    if (it == bids_.end()) {
      return ids;
    }
    for (Order* order = it->second.front(); order != nullptr; order = order->next) {
      ids.push_back(order->id);
    }
  } else {
    auto it = asks_.find(price);
    if (it == asks_.end()) {
      return ids;
    }
    for (Order* order = it->second.front(); order != nullptr; order = order->next) {
      ids.push_back(order->id);
    }
  }
  return ids;
}

std::vector<Price> OrderBook::bid_prices() const {
  std::vector<Price> prices;
  prices.reserve(bids_.size());
  for (const auto& [price, level] : bids_) {
    prices.push_back(price);
  }
  return prices;
}

std::vector<Price> OrderBook::ask_prices() const {
  std::vector<Price> prices;
  prices.reserve(asks_.size());
  for (const auto& [price, level] : asks_) {
    prices.push_back(price);
  }
  return prices;
}

}  // namespace lob
