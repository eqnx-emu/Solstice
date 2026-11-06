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
#include <atomic>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

namespace {
  using enum virtual_display::start_e;
  using namespace std::chrono_literals;
  using virtual_display_test::config_option_e;
  using virtual_display_test::device_prep_t;
  using virtual_display_test::expected_guid;
  using virtual_display_test::expected_serial;
  using virtual_display_test::make_session;
  using virtual_display_test::physical_device_id;
  using virtual_display_test::virtual_device_id;
  using virtual_display_test::virtual_display_e;
  using virtual_display_test::virtual_gdi_name;

  /**
   * @brief Check that a call happens after another one.
   * @param calls The recorded calls.
   * @param first The earlier call.
   * @param second The call expected after the first occurrence of `first`.
   * @return True when `second` occurs after `first`.
   */
  bool happens_after(const std::vector<std::string> &calls, const std::string &first, const std::string &second) {
    const auto it {std::ranges::find(calls, first)};
    return it != calls.end() && std::find(std::next(it), calls.end(), second) != calls.end();
  }
}  // namespace

TEST_F(VirtualDisplay, SkipsWhenDisabled) {
  config::video.virtual_display = virtual_display_e::disabled;
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, "", launch));
  EXPECT_TRUE(log->snapshot().empty());
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
}

TEST_F(VirtualDisplay, FallsBackWhenDriverUnavailable) {
  config::video.virtual_display = virtual_display_e::apps;
  install_backend(virtual_display::driver_status_e::failed, std::string {virtual_gdi_name});
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, "enabled", launch));
  EXPECT_TRUE(backend->creates.empty());
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_TRUE(session->enable_hdr);
  EXPECT_TRUE(log_checker::line_contains("test_sunshine.log", "Virtual display driver is unavailable"));
}

TEST_F(VirtualDisplay, CreatesWithClientMode) {
  auto session {make_session(1280, 720, 60, "0123456789ABCDEF", "PEM-SWITCH")};

  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  ASSERT_EQ(backend->creates.size(), 1);
  EXPECT_EQ(backend->creates[0].mode.width, 1280);
  EXPECT_EQ(backend->creates[0].mode.height, 720);
  EXPECT_EQ(backend->creates[0].mode.fps, 60);
  EXPECT_EQ(backend->creates[0].guid, expected_guid("PEM-SWITCH"));
  EXPECT_EQ(backend->creates[0].serial, expected_serial(expected_guid("PEM-SWITCH")));
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

TEST_F(VirtualDisplay, SerialIsThirteenHexDigitsOfMonitorId) {
  auto session {make_session(1280, 720, 60, "0123456789abcdefghij")};

  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  ASSERT_EQ(backend->creates.size(), 1);
  EXPECT_EQ(backend->creates[0].serial.size(), 13);
  EXPECT_EQ(backend->creates[0].serial, expected_serial(backend->creates[0].guid));
}

TEST_F(VirtualDisplay, MonitorIdFollowsClientCertificateNotUniqueId) {
  // Moonlight clients send the same fixed uniqueid, so only the paired certificate tells them apart.
  auto first {make_session(1280, 720, 60, "0123456789ABCDEF", "PEM-A")};
  ASSERT_TRUE(virtual_display::begin(*first, "", launch));
  virtual_display::end();

  auto second {make_session(1280, 720, 60, "0123456789ABCDEF", "PEM-B")};
  ASSERT_TRUE(virtual_display::begin(*second, "", launch));

  ASSERT_EQ(backend->creates.size(), 2);
  EXPECT_EQ(backend->creates[0].guid, expected_guid("PEM-A"));
  EXPECT_EQ(backend->creates[1].guid, expected_guid("PEM-B"));
#ifdef _WIN32
  EXPECT_NE(backend->creates[0].guid, backend->creates[1].guid);
#endif
}

TEST_F(VirtualDisplay, MonitorIdFallsBackToUniqueIdWithoutCertificate) {
  auto session {make_session(1280, 720, 60, "switch-1", "")};

  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  ASSERT_EQ(backend->creates.size(), 1);
  EXPECT_EQ(backend->creates[0].guid, expected_guid("switch-1"));
}

TEST_F(VirtualDisplay, ClientIdentityPrefersCertificate) {
  const auto with_cert {make_session(1280, 720, 60, "0123456789ABCDEF", "PEM-A")};
  const auto without_cert {make_session(1280, 720, 60, "0123456789ABCDEF")};

  EXPECT_EQ(virtual_display::client_identity(*with_cert), "PEM-A");
  EXPECT_EQ(virtual_display::client_identity(*without_cert), "0123456789ABCDEF");
}

TEST_F(VirtualDisplay, UsesRequestedResolutionNotScreenSize) {
  auto docked {make_session(1920, 1080, 60)};
  ASSERT_TRUE(virtual_display::begin(*docked, "", launch));
  ASSERT_EQ(backend->creates.size(), 1);
  EXPECT_EQ(backend->creates[0].mode.width, 1920);
  EXPECT_EQ(backend->creates[0].mode.height, 1080);
  EXPECT_EQ(backend->creates[0].mode.fps, 60);
  virtual_display::end();

  auto supersampled {make_session(2560, 1440, 60)};
  ASSERT_TRUE(virtual_display::begin(*supersampled, "", launch));
  ASSERT_EQ(backend->creates.size(), 2);
  EXPECT_EQ(backend->creates[1].mode.width, 2560);
  EXPECT_EQ(backend->creates[1].mode.height, 1440);
  EXPECT_EQ(backend->creates[1].mode.fps, 60);
}

TEST_F(VirtualDisplay, RespectsEnsureOnlyDisplay) {
  config::video.dd.configuration_option = config_option_e::ensure_only_display;
  auto session {make_session(1280, 720, 60)};

  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  const auto applied {settings->applied_configs()};
  ASSERT_EQ(applied.size(), 1);
  EXPECT_EQ(applied[0].m_device_prep, device_prep_t::EnsureOnlyDisplay);
  EXPECT_EQ(config::video.dd.configuration_option, config_option_e::ensure_only_display);
}

TEST_F(VirtualDisplay, UpgradesVerifyOnlyToEnsurePrimary) {
  config::video.dd.configuration_option = config_option_e::verify_only;
  auto session {make_session(1280, 720, 60)};

  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  const auto applied {settings->applied_configs()};
  ASSERT_EQ(applied.size(), 1);
  EXPECT_EQ(applied[0].m_device_prep, device_prep_t::EnsurePrimary);
  EXPECT_EQ(config::video.dd.configuration_option, config_option_e::verify_only);
}

TEST_F(VirtualDisplay, RemovesDisplayWhenNameLookupFails) {
  install_backend(virtual_display::driver_status_e::ok, std::nullopt);
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, "", launch));

  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(log->snapshot(), (std::vector<std::string> {"remove", "create", "remove"}));
  EXPECT_TRUE(settings->applied_configs().empty());
}

TEST_F(VirtualDisplay, RemovesDisplayWhenDeviceIsNotEnumerated) {
  install_backend(virtual_display::driver_status_e::ok, std::string {R"(\\.\DISPLAY42)"});
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, "", launch));

  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(log->snapshot(), (std::vector<std::string> {"remove", "create", "remove"}));
}

TEST_F(VirtualDisplay, RemovesLeftoverDisplayBeforeCreating) {
  auto session {make_session(1280, 720, 60)};

  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  const auto calls {log->snapshot()};
  ASSERT_GE(calls.size(), 2);
  EXPECT_EQ(calls[0], "remove");
  EXPECT_EQ(calls[1], "create");
  ASSERT_EQ(backend->removed.size(), 1);
  EXPECT_EQ(backend->removed[0], expected_guid("switch-1"));
}

TEST_F(VirtualDisplay, ResumeReusesDisplayAndAppliesNewMode) {
  auto first {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*first, "", launch));

  auto resumed {make_session(1920, 1080, 60)};
  ASSERT_TRUE(virtual_display::begin(*resumed, "", resume));

  EXPECT_EQ(backend->creates.size(), 1);
  EXPECT_FALSE(resumed->enable_hdr);
  const auto applied {settings->applied_configs()};
  ASSERT_EQ(applied.size(), 2);
  EXPECT_EQ(applied[1].m_device_id, virtual_device_id);
  EXPECT_EQ(applied[1].m_resolution, (display_device::Resolution {1920, 1080}));
}

TEST_F(VirtualDisplay, EndRevertsBeforeRemoving) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  virtual_display::end();

  EXPECT_TRUE(happens_after(log->snapshot(), "revert", "remove"));
  ASSERT_EQ(backend->removed.size(), 2);
  EXPECT_EQ(backend->removed.back(), expected_guid("switch-1"));
}

TEST_F(VirtualDisplay, EndRevertsImmediatelyDespiteRevertDelay) {
  display_device::test_set_config_revert_delay(std::chrono::seconds {10});
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  virtual_display::end();

  // With a 10 s revert delay only an immediate revert can have run by the time end() returns.
  EXPECT_TRUE(happens_after(log->snapshot(), "revert", "remove"));
  EXPECT_FALSE(display_device::test_revert_retry_is_scheduled());
}

TEST_F(VirtualDisplay, EndSchedulesRevertRetryWhenImmediateRevertFails) {
  display_device::test_set_config_revert_delay(std::chrono::seconds {10});
  settings->revert_result = display_device::SettingsManagerInterface::RevertResult::SwitchingTopologyFailed;
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  virtual_display::end();

  const auto calls {log->snapshot()};
  EXPECT_EQ(std::ranges::count(calls, "revert"), 1);
  EXPECT_TRUE(happens_after(calls, "revert", "remove"));
  EXPECT_TRUE(display_device::test_revert_retry_is_scheduled());
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
}

TEST_F(VirtualDisplay, BeginEndsActiveDisplayWhenNoLongerWanted) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  config::video.virtual_display = virtual_display_e::disabled;
  auto resumed {make_session(1280, 720, 60)};
  EXPECT_FALSE(virtual_display::begin(*resumed, "", resume));

  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_EQ(backend->removed.size(), 2);
  EXPECT_TRUE(resumed->enable_hdr);
}

TEST_F(VirtualDisplay, BeginRestoresClientSopsFlag) {
  auto session {make_session(1280, 720, 60)};
  session->enable_sops = false;

  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  EXPECT_FALSE(session->enable_sops);
  const auto applied {settings->applied_configs()};
  ASSERT_EQ(applied.size(), 1);
  EXPECT_EQ(applied[0].m_resolution, (display_device::Resolution {1280, 720}));
}

TEST_F(VirtualDisplay, EndRestoresOriginalOutputName) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  ASSERT_EQ(config::video.output_name, virtual_device_id);

  virtual_display::end();

  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_FALSE(virtual_display::active());
}

TEST_F(VirtualDisplay, EndIsIdempotent) {
  virtual_display::end();
  EXPECT_TRUE(log->snapshot().empty());

  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  virtual_display::end();
  virtual_display::end();

  // One removal of a possible leftover before creating, one at the end.
  EXPECT_EQ(backend->removed.size(), 2);
  EXPECT_EQ(config::video.output_name, physical_device_id);
}

TEST_F(VirtualDisplay, BeginWithoutBackendFallsBack) {
  virtual_display::test_install_backend(nullptr);
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, "enabled", launch));
  EXPECT_FALSE(virtual_display::active());
}

TEST_F(VirtualDisplay, InitNeverThrowsAndStaysInactive) {
  EXPECT_NO_THROW(virtual_display::init(state_file));
  EXPECT_FALSE(virtual_display::active());
#ifndef _WIN32
  auto session {make_session(1280, 720, 60)};
  EXPECT_FALSE(virtual_display::begin(*session, "enabled", launch));
#endif
}

TEST_F(VirtualDisplay, ReopensDriverWhenUnavailable) {
  // The installer may start Sunshine before the driver exists; the driver must be picked up without a restart.
  install_backend(virtual_display::driver_status_e::failed, std::string {virtual_gdi_name});
  backend->open_status = virtual_display::driver_status_e::ok;
  auto session {make_session(1280, 720, 60)};

  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  EXPECT_TRUE(happens_after(log->snapshot(), "open", "create"));
  EXPECT_TRUE(virtual_display::active());
}

TEST_F(VirtualDisplay, RateLimitsDriverReopen) {
  install_backend(virtual_display::driver_status_e::failed, std::string {virtual_gdi_name});
  auto first {make_session(1280, 720, 60)};
  auto second {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*first, "", launch));
  EXPECT_FALSE(virtual_display::begin(*second, "", launch));

  EXPECT_EQ(std::ranges::count(log->snapshot(), "open"), 1);
  EXPECT_TRUE(backend->creates.empty());
}

TEST_F(VirtualDisplay, ResumeDoesNotRecreateDisplayAfterWatchdogFailure) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  backend->status_value = virtual_display::driver_status_e::watchdog_failed;
  backend->open_status = virtual_display::driver_status_e::ok;
  auto resumed {make_session(1920, 1080, 60)};
  EXPECT_FALSE(virtual_display::begin(*resumed, "", resume));

  EXPECT_EQ(backend->creates.size(), 1);
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_TRUE(resumed->enable_hdr);

  // The launched-with-display state was cleared, so a later resume does not create one either.
  auto again {make_session(1920, 1080, 60)};
  EXPECT_FALSE(virtual_display::begin(*again, "", resume));
  EXPECT_EQ(backend->creates.size(), 1);
}

TEST_F(VirtualDisplay, LaunchRecreatesDisplayAfterWatchdogFailure) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  backend->status_value = virtual_display::driver_status_e::watchdog_failed;
  backend->open_status = virtual_display::driver_status_e::ok;
  auto resumed {make_session(1920, 1080, 60)};
  ASSERT_TRUE(virtual_display::begin(*resumed, "", launch));

  EXPECT_TRUE(happens_after(log->snapshot(), "open", "create"));
  ASSERT_EQ(backend->creates.size(), 2);
  EXPECT_EQ(backend->creates[1].mode.width, 1920);
  EXPECT_TRUE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, virtual_device_id);
}

TEST_F(VirtualDisplay, ResumeDoesNotCreateWhenLaunchFellBack) {
  install_backend(virtual_display::driver_status_e::ok, std::nullopt);
  auto launched {make_session(1280, 720, 60)};
  ASSERT_FALSE(virtual_display::begin(*launched, "", launch));

  // The driver works again, but the game is already on the physical display.
  backend->create_result = std::string {virtual_gdi_name};
  auto resumed {make_session(1280, 720, 60)};
  EXPECT_FALSE(virtual_display::begin(*resumed, "", resume));

  EXPECT_EQ(backend->creates.size(), 1);
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_TRUE(resumed->enable_hdr);
}

TEST_F(VirtualDisplay, ResumeDoesNotCreateAfterEnd) {
  auto launched {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*launched, "", launch));
  virtual_display::end();

  auto resumed {make_session(1280, 720, 60)};
  EXPECT_FALSE(virtual_display::begin(*resumed, "", resume));
  EXPECT_EQ(backend->creates.size(), 1);
}

TEST_F(VirtualDisplay, GlobalDisabledOverridesAppEnabled) {
  config::video.virtual_display = virtual_display_e::disabled;
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, "enabled", launch));
  EXPECT_TRUE(log->snapshot().empty());
}

TEST_F(VirtualDisplay, EndDefersTeardownWhileStreaming) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  virtual_display::session_started();

  // E.g. the app exits mid-stream and terminate() runs on the control thread while capture is still running.
  virtual_display::end();

  EXPECT_TRUE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, virtual_device_id);
  EXPECT_EQ(backend->removed.size(), 1);
  EXPECT_EQ(std::ranges::count(log->snapshot(), "revert"), 0);

  virtual_display::session_stopped();
  virtual_display::test_wait_for_teardown();

  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_EQ(backend->removed.size(), 2);
  EXPECT_TRUE(happens_after(log->snapshot(), "revert", "remove"));
}

TEST_F(VirtualDisplay, SessionStoppedWithoutPendingEndKeepsDisplay) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  virtual_display::session_started();

  virtual_display::session_stopped();
  virtual_display::test_wait_for_teardown();

  EXPECT_TRUE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, virtual_device_id);
}

TEST_F(VirtualDisplay, StateFileTracksActiveDisplay) {
  virtual_display::test_init(std::make_unique<virtual_display_test::fake_backend_t>(log, virtual_display::driver_status_e::ok, std::string {virtual_gdi_name}), state_file);
  EXPECT_FALSE(std::filesystem::exists(state_file));

  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  EXPECT_TRUE(std::filesystem::exists(state_file));

  virtual_display::end();
  EXPECT_FALSE(std::filesystem::exists(state_file));
}

TEST_F(VirtualDisplay, InitCleansUpDisplayLeftByCrash) {
  virtual_display::test_init(std::make_unique<virtual_display_test::fake_backend_t>(log, virtual_display::driver_status_e::ok, std::string {virtual_gdi_name}), state_file);
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  ASSERT_TRUE(std::filesystem::exists(state_file));

  // Crash: the process state is gone, the state file is not.
  virtual_display::test_install_backend(nullptr);
  config::video.output_name = physical_device_id;
  log->calls.clear();

  auto restarted {std::make_unique<virtual_display_test::fake_backend_t>(log, virtual_display::driver_status_e::ok, std::string {virtual_gdi_name})};
  const auto *fake {restarted.get()};
  virtual_display::test_init(std::move(restarted), state_file);

  // Remove the orphan, re-create it so the persisted configuration can be reverted against it, then remove it.
  EXPECT_EQ(log->snapshot(), (std::vector<std::string> {"open", "remove", "create", "revert", "remove"}));
  ASSERT_EQ(fake->creates.size(), 1);
  EXPECT_EQ(fake->creates[0].guid, expected_guid("switch-1"));
  EXPECT_EQ(fake->creates[0].mode.width, 1280);
  EXPECT_EQ(fake->creates[0].mode.height, 720);
  EXPECT_EQ(fake->creates[0].mode.fps, 60);
  EXPECT_EQ(fake->removed.back(), expected_guid("switch-1"));
  EXPECT_FALSE(std::filesystem::exists(state_file));
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
}

TEST_F(VirtualDisplay, InitKeepsCrashRecordWhileDriverUnavailable) {
  {
    std::ofstream file {state_file};
    file << "00112233445566778899AABBCCDDEEFF 1280 720 60\n";
  }

  virtual_display::test_init(std::make_unique<virtual_display_test::fake_backend_t>(log, virtual_display::driver_status_e::failed, std::string {virtual_gdi_name}), state_file);

  EXPECT_EQ(log->snapshot(), (std::vector<std::string> {"open"}));
  EXPECT_TRUE(std::filesystem::exists(state_file));
}

TEST_F(VirtualDisplay, InitDropsMalformedCrashRecord) {
  {
    std::ofstream file {state_file};
    file << "not-a-guid 1280\n";
  }

  virtual_display::test_init(std::make_unique<virtual_display_test::fake_backend_t>(log, virtual_display::driver_status_e::ok, std::string {virtual_gdi_name}), state_file);

  EXPECT_EQ(log->snapshot(), (std::vector<std::string> {"open"}));
  EXPECT_FALSE(std::filesystem::exists(state_file));
}

TEST_F(VirtualDisplay, UpgradesEnsureActiveToEnsurePrimary) {
  // ensure_active only activates the display, so the game would still open on the physical primary display.
  config::video.dd.configuration_option = config_option_e::ensure_active;
  auto session {make_session(1280, 720, 60)};

  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  const auto applied {settings->applied_configs()};
  ASSERT_EQ(applied.size(), 1);
  EXPECT_EQ(applied[0].m_device_prep, device_prep_t::EnsurePrimary);
  EXPECT_EQ(config::video.dd.configuration_option, config_option_e::ensure_active);
}

TEST_F(VirtualDisplay, KeepsEnsurePrimary) {
  config::video.dd.configuration_option = config_option_e::ensure_primary;
  auto session {make_session(1280, 720, 60)};

  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  const auto applied {settings->applied_configs()};
  ASSERT_EQ(applied.size(), 1);
  EXPECT_EQ(applied[0].m_device_prep, device_prep_t::EnsurePrimary);
}

TEST_F(VirtualDisplay, BeginRetriesConfigurationUntilApplied) {
  display_device::test_set_retry_interval(1ms);
  settings->fail_applies(2);
  auto session {make_session(1280, 720, 60)};

  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  EXPECT_EQ(std::ranges::count(log->snapshot(), "apply"), 3);
  EXPECT_TRUE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, virtual_device_id);
  EXPECT_FALSE(session->enable_hdr);
}

TEST_F(VirtualDisplay, BeginFallsBackWhenConfigurationKeepsFailing) {
  auto fake {std::make_unique<virtual_display_test::fake_backend_t>(log, virtual_display::driver_status_e::ok, std::string {virtual_gdi_name})};
  const auto *installed {fake.get()};
  virtual_display::test_init(std::move(fake), state_file);
  display_device::test_set_retry_interval(1ms);
  virtual_display::test_set_apply_window(20ms);
  settings->fail_applies(-1);
  auto session {make_session(1280, 720, 60)};

  EXPECT_FALSE(virtual_display::begin(*session, "", launch));

  const auto calls {log->snapshot()};
  EXPECT_GE(std::ranges::count(calls, "apply"), 2);
  // Torn down as in end(): revert while the display exists, then remove it.
  EXPECT_TRUE(happens_after(calls, "apply", "revert"));
  EXPECT_TRUE(happens_after(calls, "revert", "remove"));
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_TRUE(session->enable_hdr);
  EXPECT_FALSE(std::filesystem::exists(state_file));
  EXPECT_TRUE(log_checker::line_contains("test_sunshine.log", "Could not configure the new virtual display"));

  // The app now runs on the physical display, so a resume must not create a virtual display.
  settings->fail_applies(0);
  auto resumed {make_session(1280, 720, 60)};
  EXPECT_FALSE(virtual_display::begin(*resumed, "", resume));
  EXPECT_EQ(installed->creates.size(), 1);
}

TEST_F(VirtualDisplay, ResumeFallsBackWhenConfigurationFails) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  display_device::test_set_retry_interval(1ms);
  virtual_display::test_set_apply_window(5ms);
  settings->fail_applies(-1);

  auto resumed {make_session(1920, 1080, 60)};
  EXPECT_FALSE(virtual_display::begin(*resumed, "", resume));

  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_TRUE(resumed->enable_hdr);
  EXPECT_EQ(backend->removed.size(), 2);
  EXPECT_TRUE(log_checker::line_contains("test_sunshine.log", "Could not configure the virtual display"));

  settings->fail_applies(0);
  auto again {make_session(1920, 1080, 60)};
  EXPECT_FALSE(virtual_display::begin(*again, "", resume));
  EXPECT_EQ(backend->creates.size(), 1);
}

TEST_F(VirtualDisplay, InitDefersDriverOpenWithoutCrashRecord) {
  auto fake {std::make_unique<virtual_display_test::fake_backend_t>(log, virtual_display::driver_status_e::unknown, std::string {virtual_gdi_name})};
  fake->open_status = virtual_display::driver_status_e::ok;
  virtual_display::test_init(std::move(fake), state_file);

  EXPECT_TRUE(log->snapshot().empty());

  // A session that does not want a virtual display does not open the driver either.
  auto physical {make_session(1280, 720, 60)};
  EXPECT_FALSE(virtual_display::begin(*physical, "disabled", launch));
  EXPECT_TRUE(log->snapshot().empty());

  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  const auto calls {log->snapshot()};
  ASSERT_FALSE(calls.empty());
  EXPECT_EQ(calls[0], "open");
  EXPECT_TRUE(happens_after(calls, "open", "create"));
}

TEST_F(VirtualDisplay, DeferredTeardownRunsOffTheCallerThread) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  virtual_display::session_started();
  virtual_display::end();

  virtual_display::session_stopped();
  virtual_display::test_wait_for_teardown();

  EXPECT_FALSE(virtual_display::active());
  EXPECT_NE(settings->revert_thread.load(), std::this_thread::get_id());
  EXPECT_TRUE(happens_after(log->snapshot(), "revert", "remove"));
  EXPECT_EQ(config::video.output_name, physical_device_id);
}

TEST_F(VirtualDisplay, SessionStartingWhileAnotherStopsKeepsDisplay) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  virtual_display::session_started();
  virtual_display::end();

  // A second session starts before the first one has finished stopping.
  virtual_display::session_started();
  virtual_display::session_stopped();
  virtual_display::test_wait_for_teardown();

  EXPECT_EQ(virtual_display::test_session_count(), 1);
  EXPECT_TRUE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, virtual_device_id);
  EXPECT_EQ(std::ranges::count(log->snapshot(), "revert"), 0);

  virtual_display::session_stopped();
  virtual_display::test_wait_for_teardown();

  EXPECT_EQ(virtual_display::test_session_count(), 0);
  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
}

TEST_F(VirtualDisplay, SessionCountDoesNotDependOnStartStopOrder) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  virtual_display::session_started();

  // The old session stops just before the new one starts; the display is never torn down under the new session.
  virtual_display::session_stopped();
  virtual_display::session_started();
  virtual_display::end();
  virtual_display::test_wait_for_teardown();

  EXPECT_EQ(virtual_display::test_session_count(), 1);
  EXPECT_TRUE(virtual_display::active());

  virtual_display::session_stopped();
  virtual_display::test_wait_for_teardown();
  EXPECT_FALSE(virtual_display::active());
}

TEST_F(VirtualDisplay, UnbalancedSessionStopIsIgnored) {
  virtual_display::session_stopped();

  EXPECT_EQ(virtual_display::test_session_count(), 0);
  EXPECT_TRUE(log_checker::line_contains("test_sunshine.log", "Virtual display session count is unbalanced"));
}

TEST_F(VirtualDisplay, BeginRunsPendingTeardownBeforeCreating) {
  auto first {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*first, "", launch));
  virtual_display::session_started();
  virtual_display::end();
  virtual_display::test_hold_teardown_worker(true);
  virtual_display::session_stopped();
  log->calls.clear();

  // The worker has not run the teardown yet when the next launch arrives.
  auto second {make_session(1920, 1080, 60)};
  ASSERT_TRUE(virtual_display::begin(*second, "", launch));

  EXPECT_EQ(log->snapshot(), (std::vector<std::string> {"revert", "remove", "remove", "create", "apply"}));

  // The released worker finds nothing left to do.
  virtual_display::test_hold_teardown_worker(false);
  virtual_display::test_wait_for_teardown();
  EXPECT_TRUE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, virtual_device_id);
  EXPECT_EQ(std::ranges::count(log->snapshot(), "revert"), 1);
}

TEST_F(VirtualDisplay, BeginWaitsForRunningTeardown) {
  auto first {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*first, "", launch));

  std::promise<void> entered;
  std::promise<void> release;
  const auto release_future {release.get_future().share()};
  settings->on_revert = [&entered, release_future, done = false]() mutable {
    if (!done) {
      done = true;
      entered.set_value();
      release_future.wait();
    }
  };

  virtual_display::session_started();
  virtual_display::end();
  virtual_display::session_stopped();
  entered.get_future().wait();  // the worker is inside the teardown and holds the lock

  auto second {make_session(1920, 1080, 60)};
  std::atomic_bool second_result {false};
  std::thread launcher {[&second, &second_result]() {
    second_result = virtual_display::begin(*second, "", launch);
  }};
  release.set_value();
  launcher.join();

  EXPECT_TRUE(second_result);
  const auto calls {log->snapshot()};
  // First the teardown (revert, remove), then the new display (remove leftover, create, apply).
  const std::vector<std::string> tail {"revert", "remove", "remove", "create", "apply"};
  ASSERT_GE(calls.size(), tail.size());
  EXPECT_TRUE(std::equal(tail.begin(), tail.end(), calls.end() - static_cast<std::ptrdiff_t>(tail.size())));
  EXPECT_TRUE(virtual_display::active());
  ASSERT_EQ(backend->creates.size(), 2);
  EXPECT_EQ(backend->creates[1].mode.width, 1920);
}

TEST_F(VirtualDisplay, ShutdownRunsPendingTeardown) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  virtual_display::session_started();
  virtual_display::end();
  virtual_display::test_hold_teardown_worker(true);
  virtual_display::session_stopped();
  EXPECT_TRUE(virtual_display::active());

  virtual_display::shutdown();

  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_TRUE(happens_after(log->snapshot(), "revert", "remove"));
}

TEST_F(VirtualDisplay, TeardownAfterShutdownRunsInline) {
  virtual_display::shutdown();
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  virtual_display::session_started();
  virtual_display::end();

  virtual_display::session_stopped();

  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(settings->revert_thread.load(), std::this_thread::get_id());
}

TEST_F(VirtualDisplay, ShutdownWithoutPendingTeardownKeepsDisplay) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  virtual_display::shutdown();

  EXPECT_TRUE(virtual_display::active());
}

TEST_F(VirtualDisplay, EndOrRevertEndsActiveDisplay) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  virtual_display::end_or_revert();

  EXPECT_FALSE(virtual_display::active());
  EXPECT_EQ(config::video.output_name, physical_device_id);
  EXPECT_TRUE(happens_after(log->snapshot(), "revert", "remove"));
  EXPECT_FALSE(display_device::test_revert_retry_is_scheduled());
}

TEST_F(VirtualDisplay, EndOrRevertKeepsActiveDisplayWhenAsked) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));

  virtual_display::end_or_revert(true);

  EXPECT_TRUE(virtual_display::active());
  EXPECT_EQ(std::ranges::count(log->snapshot(), "revert"), 0);

  // The app was launched on the virtual display and still runs, so a resume reuses it.
  auto resumed {make_session(1280, 720, 60)};
  EXPECT_TRUE(virtual_display::begin(*resumed, "", resume));
  EXPECT_EQ(backend->creates.size(), 1);
}

TEST_F(VirtualDisplay, EndOrRevertDefersWhileSessionRuns) {
  auto session {make_session(1280, 720, 60)};
  ASSERT_TRUE(virtual_display::begin(*session, "", launch));
  virtual_display::session_started();

  virtual_display::end_or_revert();

  EXPECT_TRUE(virtual_display::active());
  virtual_display::session_stopped();
  virtual_display::test_wait_for_teardown();
  EXPECT_FALSE(virtual_display::active());
}

TEST_F(VirtualDisplay, EndOrRevertRevertsPhysicalDisplayWithoutVirtualDisplay) {
  virtual_display::end_or_revert();

  EXPECT_EQ(log->snapshot(), (std::vector<std::string> {"revert"}));
  EXPECT_TRUE(backend->removed.empty());
}

TEST_F(VirtualDisplay, EndOrRevertRevertsPhysicalDisplayEvenWhenKeeping) {
  install_backend(virtual_display::driver_status_e::ok, std::nullopt);
  auto launched {make_session(1280, 720, 60)};
  ASSERT_FALSE(virtual_display::begin(*launched, "", launch));
  log->calls.clear();

  virtual_display::end_or_revert(true);

  EXPECT_EQ(log->snapshot(), (std::vector<std::string> {"revert"}));
}
