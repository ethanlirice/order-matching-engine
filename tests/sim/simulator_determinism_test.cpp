#include <gtest/gtest.h>

#include <vector>

#include "lob/sim/simulator.hpp"
#include "lob/sim/synthetic_generator.hpp"

namespace lob::sim {
namespace {

struct RecordedCallback {
  enum class Kind { BookUpdate, Trade };

  Kind kind = Kind::BookUpdate;
  Timestamp now = 0;
  BookSnapshot snapshot;  // valid iff kind == BookUpdate
  TradeEvent trade;       // valid iff kind == Trade
};

// Records every callback it receives, and -- to exercise strategy-event/
// replay-event interleaving determinism, not just pure-replay determinism
// -- periodically submits an order using only its own deterministic
// counter (no RNG), so replaying identical input through two instances of
// this strategy can never itself introduce nondeterminism.
class RecordingStrategy : public Strategy {
 public:
  void OnBookUpdate(const BookSnapshot& snapshot, Timestamp now,
                    OrderIntentSink& intents) override {
    RecordedCallback entry;
    entry.kind = RecordedCallback::Kind::BookUpdate;
    entry.now = now;
    entry.snapshot = snapshot;
    log_.push_back(entry);

    ++update_count_;
    if (update_count_ % 7 == 0) {
      NewOrderCommand command;
      command.side = (update_count_ % 2 == 0) ? Side::Buy : Side::Sell;
      command.type = OrderType::Limit;
      command.price = snapshot.has_bid ? snapshot.best_bid - 1 : 9990;
      command.quantity = 3;
      command.timestamp = now;
      intents.Submit(command);
    }
  }

  void OnTrade(const TradeEvent& trade, Timestamp now, OrderIntentSink& /*intents*/) override {
    RecordedCallback entry;
    entry.kind = RecordedCallback::Kind::Trade;
    entry.now = now;
    entry.trade = trade;
    log_.push_back(entry);
  }

  const std::vector<RecordedCallback>& log() const { return log_; }

 private:
  std::vector<RecordedCallback> log_;
  int update_count_ = 0;
};

SyntheticGeneratorConfig TestConfig() {
  SyntheticGeneratorConfig config;
  config.seed = 123;
  config.duration = 5000;
  config.arrival_rate = 0.05;
  return config;
}

TEST(SimulatorDeterminismTest, IdenticalSeededRunsProduceByteIdenticalCallbacksAndState) {
  RecordingStrategy strategy_a;
  RecordingStrategy strategy_b;
  Simulator simulator_a(&strategy_a, /*latency=*/10);
  Simulator simulator_b(&strategy_b, /*latency=*/10);

  SyntheticGenerator generator_a(TestConfig());
  SyntheticGenerator generator_b(TestConfig());
  simulator_a.LoadEvents(generator_a.Generate());
  simulator_b.LoadEvents(generator_b.Generate());

  simulator_a.Run();
  simulator_b.Run();

  ASSERT_EQ(strategy_a.log().size(), strategy_b.log().size());
  ASSERT_GT(strategy_a.log().size(), 0u);
  for (std::size_t i = 0; i < strategy_a.log().size(); ++i) {
    const RecordedCallback& entry_a = strategy_a.log()[i];
    const RecordedCallback& entry_b = strategy_b.log()[i];
    ASSERT_EQ(entry_a.kind, entry_b.kind) << "divergence at callback index " << i;
    EXPECT_EQ(entry_a.now, entry_b.now);
    if (entry_a.kind == RecordedCallback::Kind::BookUpdate) {
      EXPECT_EQ(entry_a.snapshot.has_bid, entry_b.snapshot.has_bid);
      EXPECT_EQ(entry_a.snapshot.best_bid, entry_b.snapshot.best_bid);
      EXPECT_EQ(entry_a.snapshot.has_ask, entry_b.snapshot.has_ask);
      EXPECT_EQ(entry_a.snapshot.best_ask, entry_b.snapshot.best_ask);
    } else {
      EXPECT_EQ(entry_a.trade.price, entry_b.trade.price);
      EXPECT_EQ(entry_a.trade.size, entry_b.trade.size);
      EXPECT_EQ(entry_a.trade.maker_order_id, entry_b.trade.maker_order_id);
      EXPECT_EQ(entry_a.trade.taker_order_id, entry_b.trade.taker_order_id);
    }
  }

  EXPECT_EQ(simulator_a.DebugBook().bid_prices(), simulator_b.DebugBook().bid_prices());
  EXPECT_EQ(simulator_a.DebugBook().ask_prices(), simulator_b.DebugBook().ask_prices());
  EXPECT_EQ(simulator_a.trade_log().size(), simulator_b.trade_log().size());
}

}  // namespace
}  // namespace lob::sim
