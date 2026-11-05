/**
 * @file tests/unit/platform/windows/test_virtual_display.cpp
 * @brief Test src/platform/windows/virtual_display.cpp pure helpers.
 */

// test includes
#include "../../../tests_common.h"

// standard includes
#include <cstring>
#include <string>

#ifdef _WIN32
  // local includes
  #include <src/platform/windows/virtual_display.h>

namespace {
  constexpr GUID test_guid = {0x11223344, 0x5566, 0x7788, {1, 2, 3, 4, 5, 6, 7, 8}};
}  // namespace

TEST(MakeAddParams, ConvertsFpsToMillihertz) {
  EXPECT_EQ(platf::vdisplay::make_add_params({1280, 720, 60}, test_guid, "n", "s").RefreshRate, 60000u);
  EXPECT_EQ(platf::vdisplay::make_add_params({1280, 720, 59940}, test_guid, "n", "s").RefreshRate, 59940u);
}

TEST(MakeAddParams, RoundsOddSizeDown) {
  const auto params = platf::vdisplay::make_add_params({1281, 721, 60}, test_guid, "n", "s");
  EXPECT_EQ(params.Width, 1280u);
  EXPECT_EQ(params.Height, 720u);
}

TEST(MakeAddParams, TruncatesNames) {
  const auto params = platf::vdisplay::make_add_params({1280, 720, 60}, test_guid, "Solstice-very-long-name", "0123456789ABCDEFGH");
  EXPECT_EQ(std::strlen(params.DeviceName), 13u);
  EXPECT_EQ(std::string(params.DeviceName), "Solstice-very");
  EXPECT_EQ(std::strlen(params.SerialNumber), 13u);
  EXPECT_EQ(std::memcmp(&params.MonitorGuid, &test_guid, sizeof(GUID)), 0);
}

TEST(GuidFromClientId, IsStableAndDistinct) {
  using platf::vdisplay::guid_from_client_id;
  EXPECT_EQ(guid_from_client_id("A"), guid_from_client_id("A"));
  EXPECT_NE(guid_from_client_id("A"), guid_from_client_id("B"));
  EXPECT_EQ(guid_from_client_id(""), guid_from_client_id(""));
  EXPECT_NE(guid_from_client_id(""), guid_from_client_id("A"));
}

TEST(GuidFromClientId, SetsUuidVersionAndVariantBits) {
  const auto guid = platf::vdisplay::guid_from_client_id("A");
  EXPECT_EQ(guid.bytes[6] & 0xf0, 0x80);
  EXPECT_EQ(guid.bytes[8] & 0xc0, 0x80);
}

TEST(ProtocolCompat, ChecksMajorAndMinor) {
  using platf::vdisplay::is_protocol_compatible;
  EXPECT_TRUE(is_protocol_compatible({0, 2, 1, false}));
  EXPECT_TRUE(is_protocol_compatible({0, 3, 0, false}));
  EXPECT_FALSE(is_protocol_compatible({0, 1, 9, false}));
  EXPECT_FALSE(is_protocol_compatible({1, 2, 1, false}));
}
#endif
