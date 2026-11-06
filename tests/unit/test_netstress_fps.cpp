/**
 * @file tests/unit/test_netstress_fps.cpp
 * @brief Test the frame rate resolution of solstice-netstress in tools/netstress/fps.h.
 */
// test includes
#include "../tests_common.h"

// local includes
#include "tools/netstress/fps.h"

TEST(NetstressFps, DefaultsTo60) {
  EXPECT_EQ(netstress::resolve_fps("", ""), 60);
}

TEST(NetstressFps, CliWinsOverEnvironment) {
  EXPECT_EQ(netstress::resolve_fps("144", "30"), 144);
}

TEST(NetstressFps, UsesEnvironmentWithoutCli) {
  EXPECT_EQ(netstress::resolve_fps("", "120"), 120);
}

TEST(NetstressFps, NormalisesScaledEnvironmentValue) {
  EXPECT_EQ(netstress::resolve_fps("", "59940"), 60);
  EXPECT_EQ(netstress::resolve_fps("", "119880"), 120);
}

TEST(NetstressFps, ClampsToRange) {
  EXPECT_EQ(netstress::resolve_fps("1", ""), 10);
  EXPECT_EQ(netstress::resolve_fps("1000", ""), 240);
  EXPECT_EQ(netstress::resolve_fps("", "5"), 10);
  EXPECT_EQ(netstress::resolve_fps("", "500"), 240);
}

TEST(NetstressFps, InvalidSourcesFallThrough) {
  EXPECT_EQ(netstress::resolve_fps("abc", "90"), 90);
  EXPECT_EQ(netstress::resolve_fps("0", "90"), 90);
  EXPECT_EQ(netstress::resolve_fps("-5", ""), 60);
  EXPECT_EQ(netstress::resolve_fps("", "x"), 60);
  EXPECT_EQ(netstress::resolve_fps("", "0"), 60);
  EXPECT_EQ(netstress::resolve_fps("", "12345678901234"), 60);
}
