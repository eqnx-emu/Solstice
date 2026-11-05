/**
 * @file tests/unit/test_virtual_display_session.cpp
 * @brief Test the virtual display session lifecycle in src/virtual_display.*.
 */

// test includes
#include "../tests_common.h"
#include "../tests_log_checker.h"
#include "virtual_display_test_support.h"

// standard includes
#include <algorithm>
#include <chrono>

TEST_F(VirtualDisplay, SkipsWhenDisabled) {
  config::video.virtual_display = virtual_display_e::disabled;
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, ""));
  EXPECT_TRUE(log->snapshot().empty());
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
}

TEST_F(VirtualDisplay, FallsBackWhenDriverUnavailable) {
  config::video.virtual_display = virtual_display_e::disabled;
  install_backend(virtual_display::driver_status_e::failed, std::string {virtual_gdi_name});
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, "enabled"));
  EXPECT_TRUE(backend->creates.empty());
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_TRUE(session->enable_hdr);
  EXPECT_TRUE(log_checker::line_contains("test_sunshine.log", "Virtual display driver is unavailable"));
}

TEST_F(VirtualDisplay, CreatesWithClientMode) {
  auto session {make_session(1280, 720, 60)};

  ASSERT_TRUE(virtual_display::begin(*session, ""));

  ASSERT_EQ(backend->creates.size(), 1);
  EXPECT_EQ(backend->creates[0].mode.width, 1280);
  EXPECT_EQ(backend->creates[0].mode.height, 720);
  EXPECT_EQ(backend->creates[0].mode.fps, 60);
  EXPECT_EQ(backend->creates[0].guid, expected_guid("switch-1"));
  EXPECT_EQ(backend->creates[0].serial, "switch-1");
  EXPECT_EQ(config::video.output_name, virtual_device_id);
  EXPECT_FALSE(session->enable_hdr);
  EXPECT_TRUE(virtual_display::active());

  const auto applied {settings->applied_configs()};
  ASSERT_EQ(applied.size(), 1);
  EXPECT_EQ(applied[0].m_device_id, virtual_device_id);
  EXPECT_EQ(applied[0].m_device_prep, device_prep_t::EnsurePrimary);
  EXPECT_EQ(applied[0].m_resolution, (display_device::Resolution {1280, 720}));
  EXPECT_NE(applied[0].m_hdr_state, std::make_optional(display_device::HdrState::Enabled));
}

TEST_F(VirtualDisplay, TruncatesSerialToThirteenCharacters) {
  auto session {make_session(1280, 720, 60, "0123456789abcdefghij")};

  ASSERT_TRUE(virtual_display::begin(*session, ""));

  ASSERT_EQ(backend->creates.size(), 1);
  EXPECT_EQ(backend->creates[0].serial, "0123456789abc");
}

TEST_F(VirtualDisplay, UsesRequestedResolutionNotScreenSize) {
  auto docked {make_session(1920, 1080, 60)};
  ASSERT_TRUE(virtual_display::begin(*docked, ""));
  ASSERT_EQ(backend->creates.size(), 1);
  EXPECT_EQ(backend->creates[0].mode.width, 1920);
  EXPECT_EQ(backend->creates[0].mode.height, 1080);
  EXPECT_EQ(backend->creates[0].mode.fps, 60);
  virtual_display::end();

  auto supersampled {make_session(2560, 1440, 60)};
  ASSERT_TRUE(virtual_display::begin(*supersampled, ""));
  ASSERT_EQ(backend->creates.size(), 2);
  EXPECT_EQ(backend->creates[1].mode.width, 2560);
  EXPECT_EQ(backend->creates[1].mode.height, 1440);
  EXPECT_EQ(backend->creates[1].mode.fps, 60);
}

TEST_F(VirtualDisplay, RespectsEnsureOnlyDisplay) {
  config::video.dd.configuration_option = config_option_e::ensure_only_display;
  auto session {make_session(1280, 720, 60)};

  ASSERT_TRUE(virtual_display::begin(*session, ""));

  const auto applied {settings->applied_configs()};
  ASSERT_EQ(applied.size(), 1);
  EXPECT_EQ(applied[0].m_device_prep, device_prep_t::EnsureOnlyDisplay);
  EXPECT_EQ(config::video.dd.configuration_option, config_option_e::ensure_only_display);
}

TEST_F(VirtualDisplay, UpgradesVerifyOnlyToEnsurePrimary) {
  config::video.dd.configuration_option = config_option_e::verify_only;
  auto session {make_session(1280, 720, 60)};

  ASSERT_TRUE(virtual_display::begin(*session, ""));

  const auto applied {settings->applied_configs()};
  ASSERT_EQ(applied.size(), 1);
  EXPECT_EQ(applied[0].m_device_prep, device_prep_t::EnsurePrimary);
  EXPECT_EQ(config::video.dd.configuration_option, config_option_e::verify_only);
}

TEST_F(VirtualDisplay, RemovesDisplayWhenNameLookupFails) {
  install_backend(virtual_display::driver_status_e::ok, std::nullopt);
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, ""));

  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(log->snapshot(), (std::vector<std::string> {"create", "remove"}));
  EXPECT_TRUE(settings->applied_configs().empty());
}

TEST_F(VirtualDisplay, RemovesDisplayWhenDeviceIsNotEnumerated) {
  install_backend(virtual_display::driver_status_e::ok, std::string {R"(\\.\DISPLAY42)"});
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, ""));

  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(log->snapshot(), (std::vector<std::string> {"create", "remove"}));
}

TEST_F(VirtualDisplay, ResumeReusesDisplayAndAppliesNewMode) {
  auto first {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*first, ""));

  auto resumed {make_session(1920, 1080, 60)};
  ASSERT_TRUE(virtual_display::begin(*resumed, ""));

  EXPECT_EQ(backend->creates.size(), 1);
  EXPECT_FALSE(resumed->enable_hdr);
  const auto applied {settings->applied_configs()};
  ASSERT_EQ(applied.size(), 2);
  EXPECT_EQ(applied[1].m_device_id, virtual_device_id);
  EXPECT_EQ(applied[1].m_resolution, (display_device::Resolution {1920, 1080}));
}

TEST_F(VirtualDisplay, EndRevertsBeforeRemoving) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, ""));

  virtual_display::end();

  const auto calls {log->snapshot()};
  const auto revert {std::ranges::find(calls, "revert")};
  const auto remove {std::ranges::find(calls, "remove")};
  ASSERT_NE(revert, calls.end());
  ASSERT_NE(remove, calls.end());
  EXPECT_LT(revert, remove);
  ASSERT_EQ(backend->removed.size(), 1);
  EXPECT_EQ(backend->removed[0], expected_guid("switch-1"));
}

TEST_F(VirtualDisplay, EndRevertsImmediatelyDespiteRevertDelay) {
  display_device::test_set_config_revert_delay(std::chrono::seconds {10});
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, ""));

  virtual_display::end();

  // With a 10 s revert delay only an immediate revert can have run by the time end() returns.
  const auto calls {log->snapshot()};
  const auto revert {std::ranges::find(calls, "revert")};
  const auto remove {std::ranges::find(calls, "remove")};
  ASSERT_NE(revert, calls.end());
  ASSERT_NE(remove, calls.end());
  EXPECT_LT(revert, remove);
  EXPECT_FALSE(display_device::test_revert_retry_is_scheduled());
}

TEST_F(VirtualDisplay, EndSchedulesRevertRetryWhenImmediateRevertFails) {
  display_device::test_set_config_revert_delay(std::chrono::seconds {10});
  settings->revert_result = display_device::SettingsManagerInterface::RevertResult::SwitchingTopologyFailed;
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, ""));

  virtual_display::end();

  const auto calls {log->snapshot()};
  EXPECT_EQ(std::ranges::count(calls, "revert"), 1);
  EXPECT_LT(std::ranges::find(calls, "revert"), std::ranges::find(calls, "remove"));
  EXPECT_TRUE(display_device::test_revert_retry_is_scheduled());
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
}

TEST_F(VirtualDisplay, BeginEndsActiveDisplayWhenNoLongerWanted) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, ""));

  config::video.virtual_display = virtual_display_e::disabled;
  auto resumed {make_session(1280, 720, 60)};
  EXPECT_FALSE(virtual_display::begin(*resumed, ""));

  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_EQ(backend->removed.size(), 1);
  EXPECT_TRUE(resumed->enable_hdr);
}

TEST_F(VirtualDisplay, BeginRestoresClientSopsFlag) {
  auto session {make_session(1280, 720, 60)};
  session->enable_sops = false;

  ASSERT_TRUE(virtual_display::begin(*session, ""));

  EXPECT_FALSE(session->enable_sops);
  const auto applied {settings->applied_configs()};
  ASSERT_EQ(applied.size(), 1);
  EXPECT_EQ(applied[0].m_resolution, (display_device::Resolution {1280, 720}));
}

TEST_F(VirtualDisplay, EndRestoresOriginalOutputName) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, ""));
  ASSERT_EQ(config::video.output_name, virtual_device_id);

  virtual_display::end();

  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_FALSE(virtual_display::active());
}

TEST_F(VirtualDisplay, EndIsIdempotent) {
  virtual_display::end();
  EXPECT_TRUE(log->snapshot().empty());

  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, ""));
  virtual_display::end();
  virtual_display::end();

  EXPECT_EQ(backend->removed.size(), 1);
  EXPECT_EQ(config::video.output_name, physical_device_id);
}

TEST_F(VirtualDisplay, BeginWithoutBackendFallsBack) {
  virtual_display::test_install_backend(nullptr);
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, "enabled"));
  EXPECT_FALSE(virtual_display::active());
}

TEST_F(VirtualDisplay, InitNeverThrowsAndStaysInactive) {
  EXPECT_NO_THROW(virtual_display::init());
  EXPECT_FALSE(virtual_display::active());
#ifndef _WIN32
  auto session {make_session(1280, 720, 60)};
  EXPECT_FALSE(virtual_display::begin(*session, "enabled"));
#endif
}
