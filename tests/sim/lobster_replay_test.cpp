#include "lob/sim/lobster_replay.hpp"

#include <gtest/gtest.h>

namespace lob::sim {
namespace {

TEST(LobsterReplayTest, AddRestsAndAppearsInSnapshot) {
  std::vector<LobsterReplayEvent> events = {
      {LobsterReplayEvent::Kind::Add, 1, Side::Buy, 100, 10},
  };

  std::vector<BookSnapshotRow> snapshots = ReplayLobsterEvents(events, /*num_levels=*/2);

  ASSERT_EQ(snapshots.size(), 1u);
  const BookLevelSnapshot& top = snapshots[0].levels[0];
  EXPECT_FALSE(top.bid_empty);
  EXPECT_EQ(top.bid_price, 100);
  EXPECT_EQ(top.bid_size, 10u);
  EXPECT_TRUE(top.ask_empty);
  EXPECT_TRUE(snapshots[0].levels[1].bid_empty);
}

TEST(LobsterReplayTest, CancelRemovesFromSnapshot) {
  std::vector<LobsterReplayEvent> events = {
      {LobsterReplayEvent::Kind::Add, 1, Side::Buy, 100, 10},
      {LobsterReplayEvent::Kind::Cancel, 1, Side::Buy, 0, 0},
  };

  std::vector<BookSnapshotRow> snapshots = ReplayLobsterEvents(events, /*num_levels=*/1);

  ASSERT_EQ(snapshots.size(), 2u);
  EXPECT_FALSE(snapshots[0].levels[0].bid_empty);
  EXPECT_TRUE(snapshots[1].levels[0].bid_empty);
}

TEST(LobsterReplayTest, ReduceShrinksRestingQuantityWithoutRemovingTheLevel) {
  std::vector<LobsterReplayEvent> events = {
      {LobsterReplayEvent::Kind::Add, 1, Side::Buy, 100, 10},
      {LobsterReplayEvent::Kind::Reduce, 1, Side::Buy, 0, 4},
  };

  std::vector<BookSnapshotRow> snapshots = ReplayLobsterEvents(events, /*num_levels=*/1);

  ASSERT_EQ(snapshots.size(), 2u);
  EXPECT_EQ(snapshots[0].levels[0].bid_size, 10u);
  EXPECT_FALSE(snapshots[1].levels[0].bid_empty);
  EXPECT_EQ(snapshots[1].levels[0].bid_size, 4u);
}

TEST(LobsterReplayTest, CrossingAddMatchesAndUpdatesBothSides) {
  std::vector<LobsterReplayEvent> events = {
      {LobsterReplayEvent::Kind::Add, 1, Side::Sell, 100, 10},
      {LobsterReplayEvent::Kind::Add, 2, Side::Buy, 100, 4},
  };

  std::vector<BookSnapshotRow> snapshots = ReplayLobsterEvents(events, /*num_levels=*/1);

  ASSERT_EQ(snapshots.size(), 2u);
  EXPECT_FALSE(snapshots[1].levels[0].ask_empty);
  EXPECT_EQ(snapshots[1].levels[0].ask_size, 6u);
  EXPECT_TRUE(snapshots[1].levels[0].bid_empty)
      << "the marketable buy fully filled against the resting sell and did not rest";
}

TEST(LobsterReplayTest, EmptyLevelsUseLobsterSentinelPrices) {
  std::vector<LobsterReplayEvent> events = {
      {LobsterReplayEvent::Kind::Add, 1, Side::Buy, 100, 10},
  };

  std::vector<BookSnapshotRow> snapshots = ReplayLobsterEvents(events, /*num_levels=*/1);

  const BookLevelSnapshot& top = snapshots[0].levels[0];
  EXPECT_EQ(top.ask_price, 9999999999);
  EXPECT_EQ(top.ask_size, 0u);
}

}  // namespace
}  // namespace lob::sim
