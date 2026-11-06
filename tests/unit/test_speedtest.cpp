/**
 * @file tests/unit/test_speedtest.cpp
 * @brief Test src/speedtest.* functions.
 */
// test includes
#include "../tests_common.h"

// standard includes
#include <algorithm>
#include <chrono>
#include <set>
#include <string>

// local includes
#include <src/speedtest.h>

using namespace std::chrono_literals;

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

TEST(Speedtest, ChunkHeaderIsLowercaseHexSizeAndCrlf) {
  EXPECT_EQ(speedtest::chunk_header(speedtest::block_size), "10000\r\n");
  EXPECT_EQ(speedtest::chunk_header(255), "ff\r\n");
  EXPECT_EQ(speedtest::chunk_header(1), "1\r\n");
}

TEST(Speedtest, ChunkFramingMatchesChunkedEncoding) {
  EXPECT_EQ(speedtest::chunk_trailer, "\r\n");
  EXPECT_EQ(speedtest::last_chunk, "0\r\n\r\n");

  const auto data {speedtest::block(0)};
  const auto chunk {speedtest::chunk_header(data.size()) + std::string {data.begin(), data.end()} + std::string {speedtest::chunk_trailer}};
  EXPECT_EQ(chunk.size(), 7 + speedtest::block_size + 2);
  EXPECT_EQ(chunk.substr(0, 7), "10000\r\n");
  EXPECT_EQ(chunk.substr(chunk.size() - 2), "\r\n");
}

TEST(Speedtest, DrainTimeIsZeroWithoutMeasuredRate) {
  EXPECT_EQ(speedtest::drain_time(speedtest::block_size, 0, 1s), 0ns);
  EXPECT_EQ(speedtest::drain_time(speedtest::block_size, speedtest::block_size, 0ns), 0ns);
}

TEST(Speedtest, DrainTimeFollowsMeasuredRate) {
  // 64 KiB completed in 1 s: four queued blocks take 4 s.
  EXPECT_EQ(speedtest::drain_time(4 * speedtest::block_size, speedtest::block_size, 1s), 4s);
  // Ten blocks per second: one block takes 100 ms.
  EXPECT_EQ(speedtest::drain_time(speedtest::block_size, 10 * speedtest::block_size, 1s), 100ms);
}

TEST(Speedtest, NextStepSendsBlocksUntilTheQueueIsFull) {
  const auto now {std::chrono::steady_clock::now()};
  const auto deadline {now + 5s};

  EXPECT_EQ(speedtest::next_step(now, deadline, 0, 0ns, false), speedtest::step_e::send_block);
  EXPECT_EQ(speedtest::next_step(now, deadline, speedtest::queue_depth - 1, 1s, false), speedtest::step_e::send_block);
  EXPECT_EQ(speedtest::next_step(now, deadline, speedtest::queue_depth, 0ns, false), speedtest::step_e::wait);
}

TEST(Speedtest, NextStepStopsQueuingAtTheDeadline) {
  const auto now {std::chrono::steady_clock::now()};

  EXPECT_EQ(speedtest::next_step(now, now, 1, 0ns, false), speedtest::step_e::wait);
  EXPECT_EQ(speedtest::next_step(now, now, 0, 0ns, false), speedtest::step_e::finish);
  EXPECT_EQ(speedtest::next_step(now, now - 1s, 0, 0ns, false), speedtest::step_e::finish);
}

TEST(Speedtest, NextStepStopsQueuingWhenTheQueueWouldNotDrainInTime) {
  // On a slow link the queued blocks would still be sending long after the deadline.
  const auto now {std::chrono::steady_clock::now()};
  const auto deadline {now + 2s};

  EXPECT_EQ(speedtest::next_step(now, deadline, 1, 3s, false), speedtest::step_e::wait);
  EXPECT_EQ(speedtest::next_step(now, deadline, 0, 2s, false), speedtest::step_e::finish);
  EXPECT_EQ(speedtest::next_step(now, deadline, 0, 1s, false), speedtest::step_e::send_block);
}

TEST(Speedtest, NextStepFinishesOnShutdown) {
  const auto now {std::chrono::steady_clock::now()};
  const auto deadline {now + 5s};

  EXPECT_EQ(speedtest::next_step(now, deadline, 2, 0ns, true), speedtest::step_e::wait);
  EXPECT_EQ(speedtest::next_step(now, deadline, 0, 0ns, true), speedtest::step_e::finish);
}

TEST(Speedtest, CutoffLeavesTenSecondsOfGrace) {
  const auto deadline {std::chrono::steady_clock::now()};

  EXPECT_EQ(speedtest::cutoff_time(deadline) - deadline, 10s);
  EXPECT_EQ(speedtest::stall_grace, 10s);
}
