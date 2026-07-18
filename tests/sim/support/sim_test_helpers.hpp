#pragma once

#include "lob/sim/event.hpp"

namespace lob::sim::testing {

inline Event MakeAddEvent(Timestamp t, std::uint64_t sequence, OrderId id, Side side, Price price,
                          Quantity quantity) {
  Event event;
  event.timestamp = t;
  event.kind = EventKind::Replay;
  event.sequence = sequence;
  ReplayMessage message;
  message.kind = ReplayMessage::Kind::Add;
  message.add.id = id;
  message.add.side = side;
  message.add.type = OrderType::Limit;
  message.add.price = price;
  message.add.quantity = quantity;
  message.add.timestamp = t;
  event.payload = message;
  return event;
}

inline Event MakeCancelEvent(Timestamp t, std::uint64_t sequence, OrderId id) {
  Event event;
  event.timestamp = t;
  event.kind = EventKind::Replay;
  event.sequence = sequence;
  ReplayMessage message;
  message.kind = ReplayMessage::Kind::Cancel;
  message.cancel_id = id;
  event.payload = message;
  return event;
}

inline Event MakeReduceEvent(Timestamp t, std::uint64_t sequence, OrderId id,
                             Quantity new_quantity) {
  Event event;
  event.timestamp = t;
  event.kind = EventKind::Replay;
  event.sequence = sequence;
  ReplayMessage message;
  message.kind = ReplayMessage::Kind::Reduce;
  message.reduce_id = id;
  message.reduce_to_quantity = new_quantity;
  event.payload = message;
  return event;
}

}  // namespace lob::sim::testing
