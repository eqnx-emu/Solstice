/**
 * @file tests/unit/platform/windows/test_viiper_gamepad.cpp
 * @brief Tests for Moonlight to Switch 2 Pro conversions used by the libVIIPER backend.
 */

// test includes
#include "../../../tests_common.h"

#ifdef _WIN32

  // standard includes
  #include <cmath>

  // local includes
  #include "src/config.h"
  #include "src/platform/windows/viiper_gamepad.h"

namespace {
  constexpr float G = 9.80665F;

  /**
   * @brief Decode raw accelerometer values the way SDL's Switch 2 driver does.
   */
  std::array<float, 3> sdl_accel(const std::array<std::int16_t, 3> &raw) {
    const float scale = G * 8.0F / 32767.0F;
    return {raw[0] * scale, raw[2] * scale, raw[1] * -scale};
  }

  /**
   * @brief Decode raw gyroscope values to deg/s the way SDL's Switch 2 driver does.
   */
  std::array<float, 3> sdl_gyro_deg(const std::array<std::int16_t, 3> &raw) {
    const float scale = 34.8F / 32767.0F * 180.0F / 3.14159265F;
    return {raw[0] * scale, raw[2] * scale, raw[1] * -scale};
  }

  /**
   * @brief Build actuator bytes with SDL's EncodeHDRumble layout after a sequence byte.
   */
  std::array<std::uint8_t, 16> sdl_actuator(std::uint16_t high_amp, std::uint16_t low_amp) {
    constexpr std::uint16_t high_freq = 0x187;
    constexpr std::uint16_t low_freq = 0x112;
    std::array<std::uint8_t, 16> out {};
    out[0] = 0x50;
    out[1] = static_cast<std::uint8_t>(high_freq & 0xFF);
    out[2] = static_cast<std::uint8_t>(((high_amp >> 4) & 0xFC) | ((high_freq >> 8) & 0x03));
    out[3] = static_cast<std::uint8_t>((high_amp >> 12) | (low_freq << 4));
    out[4] = static_cast<std::uint8_t>((low_amp & 0xC0) | ((low_freq >> 4) & 0x3F));
    out[5] = static_cast<std::uint8_t>(low_amp >> 8);
    return out;
  }
}  // namespace

/**
 * @brief VIIPER is only chosen for Switch, or automatic selection with a Nintendo client controller.
 */
TEST(ViiperGamepadTest, SelectsSwitchEmulationOnlyWhenRequested) {
  using platf::viiper::should_emulate_ns2pro;
  EXPECT_TRUE(should_emulate_ns2pro(config::GAMEPAD_DRIVER_VIIPER, "switch", LI_CTYPE_XBOX));
  EXPECT_TRUE(should_emulate_ns2pro(config::GAMEPAD_DRIVER_VIIPER, "auto", LI_CTYPE_NINTENDO));
  EXPECT_FALSE(should_emulate_ns2pro(config::GAMEPAD_DRIVER_VIIPER, "auto", LI_CTYPE_UNKNOWN));
  EXPECT_FALSE(should_emulate_ns2pro(config::GAMEPAD_DRIVER_VIIPER, "x360", LI_CTYPE_NINTENDO));
  EXPECT_FALSE(should_emulate_ns2pro(config::GAMEPAD_DRIVER_VIGEMBUS, "switch", LI_CTYPE_NINTENDO));
}

/**
 * @brief Face buttons map by position and analog triggers become digital ZL/ZR.
 */
TEST(ViiperGamepadTest, MapsButtonsByPosition) {
  using platf::viiper::ns2pro_buttons;
  EXPECT_EQ(ns2pro_buttons(platf::A, 0, 0), 0x00000001u);  // south -> Nintendo B
  EXPECT_EQ(ns2pro_buttons(platf::B, 0, 0), 0x00000002u);  // east -> Nintendo A
  EXPECT_EQ(ns2pro_buttons(platf::X, 0, 0), 0x00000004u);  // west -> Nintendo Y
  EXPECT_EQ(ns2pro_buttons(platf::Y, 0, 0), 0x00000008u);  // north -> Nintendo X
  EXPECT_EQ(ns2pro_buttons(platf::START | platf::BACK, 0, 0), 0x00000040u | 0x00004000u);
  EXPECT_EQ(ns2pro_buttons(platf::HOME | platf::MISC_BUTTON, 0, 0), 0x00010000u | 0x00020000u);
  EXPECT_EQ(ns2pro_buttons(0, 255, 0), 0x00002000u);
  EXPECT_EQ(ns2pro_buttons(0, 0, 255), 0x00000020u);
  EXPECT_EQ(ns2pro_buttons(0, 10, 10), 0u);
}

/**
 * @brief Stick values map to the 12-bit range with up and right as larger values.
 */
TEST(ViiperGamepadTest, MapsSticksTo12Bit) {
  using platf::viiper::ns2pro_stick;
  EXPECT_EQ(ns2pro_stick(0), 2048);
  EXPECT_EQ(ns2pro_stick(32767), 4095);
  EXPECT_LE(ns2pro_stick(-32768), 1);
  EXPECT_GT(ns2pro_stick(16000), 2048);
}

/**
 * @brief Motion values survive SDL's Switch 2 decoding.
 */
TEST(ViiperGamepadTest, MotionRoundTripsThroughSdlDecoding) {
  const auto rest = platf::viiper::ns2pro_accel(0.0F, G, 0.0F);
  EXPECT_EQ(rest[0], 0);
  EXPECT_EQ(rest[1], 0);
  EXPECT_NEAR(rest[2], 4096, 1);

  const auto accel = sdl_accel(platf::viiper::ns2pro_accel(1.5F, -3.0F, 7.25F));
  EXPECT_NEAR(accel[0], 1.5F, 0.01F);
  EXPECT_NEAR(accel[1], -3.0F, 0.01F);
  EXPECT_NEAR(accel[2], 7.25F, 0.01F);

  const auto gyro = sdl_gyro_deg(platf::viiper::ns2pro_gyro(90.0F, -45.0F, 300.0F));
  EXPECT_NEAR(gyro[0], 90.0F, 0.2F);
  EXPECT_NEAR(gyro[1], -45.0F, 0.2F);
  EXPECT_NEAR(gyro[2], 300.0F, 0.2F);
}

/**
 * @brief HD rumble output decodes back to Moonlight motor strengths.
 */
TEST(ViiperGamepadTest, DecodesHdRumble) {
  const auto off = platf::viiper::ns2pro_rumble(sdl_actuator(0, 0), sdl_actuator(0, 0));
  EXPECT_EQ(off.first, 0);
  EXPECT_EQ(off.second, 0);

  const auto full = platf::viiper::ns2pro_rumble(sdl_actuator(29000, 29000), sdl_actuator(0, 14500));
  EXPECT_NEAR(full.first, 65535, 200);
  EXPECT_NEAR(full.second, 32767, 200);
}

#endif
