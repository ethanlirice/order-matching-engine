#include "lob/sim/synthetic_generator.hpp"

#include <random>

#include "lob/order_command.hpp"

namespace lob::sim {

SyntheticGenerator::SyntheticGenerator(SyntheticGeneratorConfig config) : config_(config) {}

std::vector<Event> SyntheticGenerator::Generate() {
  std::mt19937_64 rng(config_.seed);
  std::exponential_distribution<double> inter_arrival(config_.arrival_rate);
  std::uniform_int_distribution<int> side_dist(0, 1);
  std::uniform_int_distribution<Price> offset_dist(1, config_.price_offset_ticks);
  std::uniform_int_distribution<Quantity> quantity_dist(config_.min_quantity, config_.max_quantity);
  std::uniform_real_distribution<double> unit_dist(0.0, 1.0);
  std::uniform_int_distribution<int> drift_dist(-1, 1);

  std::vector<Event> events;
  std::vector<OrderId> live_ids;
  OrderId next_id = 1;
  std::uint64_t next_sequence = 0;
  Price mid_price = config_.base_price;
  Timestamp current_time = 0;

  while (true) {
    double gap = inter_arrival(rng);
    // +1 keeps timestamps strictly progressing regardless of how small the
    // drawn gap rounds down to -- simple, deterministic, and sufficient
    // for a controlled synthetic stream (the event queue's tie-break rule
    // still handles genuine same-timestamp ties correctly if ever needed).
    current_time += static_cast<Timestamp>(gap) + 1;
    if (current_time > config_.duration) {
      break;
    }

    mid_price += drift_dist(rng);

    bool make_cancel = !live_ids.empty() && unit_dist(rng) < config_.cancel_probability;

    ReplayMessage message;
    if (make_cancel) {
      std::uniform_int_distribution<std::size_t> pick(0, live_ids.size() - 1);
      std::size_t index = pick(rng);
      message.kind = ReplayMessage::Kind::Cancel;
      message.cancel_id = live_ids[index];
      live_ids[index] = live_ids.back();
      live_ids.pop_back();
    } else {
      Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
      bool aggressive = unit_dist(rng) < config_.aggressive_probability;
      Price offset = offset_dist(rng);
      // Aggressive orders are priced through the drifting mid (likely to
      // cross real resting liquidity near it); passive orders are priced
      // away from it (likely to rest).
      Price price;
      if (side == Side::Buy) {
        price = aggressive ? mid_price + offset : mid_price - offset;
      } else {
        price = aggressive ? mid_price - offset : mid_price + offset;
      }

      NewOrderCommand command;
      command.id = next_id++;
      command.side = side;
      command.type = OrderType::Limit;
      command.price = price;
      command.quantity = quantity_dist(rng);
      command.timestamp = current_time;

      message.kind = ReplayMessage::Kind::Add;
      message.add = command;
      live_ids.push_back(command.id);
    }

    Event event;
    event.timestamp = current_time;
    event.kind = EventKind::Replay;
    event.sequence = next_sequence++;
    event.payload = message;
    events.push_back(std::move(event));
  }

  return events;
}

}  // namespace lob::sim
