/**
 * @file tests/unit/test_speedtest.cpp
 * @brief Test src/speedtest.* functions.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <set>
#include <string>

// local includes
#include <src/speedtest.h>

TEST(Speedtest, ParseSecondsAcceptsTheValidRange) {
  EXPECT_EQ(speedtest::parse_seconds("5"), 5);
  EXPECT_EQ(speedtest::parse_seconds("1"), 1);
  EXPECT_EQ(speedtest::parse_seconds("10"), 10);
}

TEST(Speedtest, ParseSecondsRejectsInvalidValues) {
  EXPECT_FALSE(speedtest::parse_seconds("0"));
  EXPECT_FALSE(speedtest::parse_seconds("11"));
  EXPECT_FALSE(speedtest::parse_seconds("-3"));
  EXPECT_FALSE(speedtest::parse_seconds("abc"));
  EXPECT_FALSE(speedtest::parse_seconds("5x"));
  EXPECT_FALSE(speedtest::parse_seconds(""));
  EXPECT_FALSE(speedtest::parse_seconds("99999999999999999999"));
}

TEST(Speedtest, AcquireIsExclusiveUntilReleased) {
  ASSERT_TRUE(speedtest::try_acquire());
  EXPECT_FALSE(speedtest::try_acquire());
  speedtest::release();
  EXPECT_TRUE(speedtest::try_acquire());
  speedtest::release();
}

TEST(Speedtest, BlocksAreFullSizedAndNotConstant) {
  const auto first = speedtest::block(0);
  ASSERT_EQ(first.size(), speedtest::block_size);
  std::set<char> distinct(first.begin(), first.end());
  EXPECT_GT(distinct.size(), 100u);

  const auto wrapped = speedtest::block(speedtest::pool_size / speedtest::block_size);
  EXPECT_EQ(wrapped.data(), first.data());
  EXPECT_NE(speedtest::block(1).data(), first.data());
  EXPECT_FALSE(std::equal(first.begin(), first.end(), speedtest::block(1).begin()));
}

TEST(Speedtest, SlotReleasesOnDestructionAndMove) {
  ASSERT_TRUE(speedtest::try_acquire());
  {
    speedtest::slot_t outer {true};
    EXPECT_TRUE(outer.owns());
    speedtest::slot_t moved {std::move(outer)};
    EXPECT_FALSE(outer.owns());  // NOLINT(bugprone-use-after-move)
    EXPECT_TRUE(moved.owns());
    EXPECT_FALSE(speedtest::try_acquire());
    speedtest::slot_t assigned;
    assigned = std::move(moved);
    EXPECT_TRUE(assigned.owns());
    EXPECT_FALSE(speedtest::try_acquire());
  }
  EXPECT_TRUE(speedtest::try_acquire());
  speedtest::release();
}

TEST(Speedtest, AdmitRejectsBadSeconds) {
  EXPECT_EQ(speedtest::admit("0", 0).status, 400);
  EXPECT_EQ(speedtest::admit("", 0).status, 400);
  EXPECT_TRUE(speedtest::try_acquire());  // no slot was left behind
  speedtest::release();
}

TEST(Speedtest, AdmitRejectsWhileStreaming) {
  const auto result = speedtest::admit("5", 1);
  EXPECT_EQ(result.status, 409);
  EXPECT_FALSE(result.slot.owns());
  EXPECT_TRUE(speedtest::try_acquire());
  speedtest::release();
}

TEST(Speedtest, AdmitAllowsOneAtATimeAndReleasesTheSlot) {
  {
    auto first = speedtest::admit("3", 0);
    EXPECT_EQ(first.status, 200);
    EXPECT_EQ(first.seconds, 3);
    EXPECT_TRUE(first.slot.owns());

    const auto second = speedtest::admit("3", 0);
    EXPECT_EQ(second.status, 429);
    EXPECT_FALSE(second.slot.owns());
  }
  const auto third = speedtest::admit("4", 0);
  EXPECT_EQ(third.status, 200);
  EXPECT_EQ(third.seconds, 4);
}
