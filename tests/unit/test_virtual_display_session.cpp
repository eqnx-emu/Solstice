/**
 * @file tests/unit/test_virtual_display_session.cpp
 * @brief Test the virtual display session lifecycle in src/virtual_display.*.
 */

// test includes
#include "../tests_common.h"
#include "../tests_log_checker.h"

// standard includes
#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <vector>

// lib includes
#include <display_device/settings_manager_interface.h>
#include <display_device/settings_persistence_interface.h>

// local includes
#include <src/config.h>
#include <src/display_device.h>
#include <src/rtsp.h>
#include <src/virtual_display.h>

#ifdef _WIN32
  #include <src/platform/windows/virtual_display.h>
#endif

namespace {
  using config_option_e = config::video_t::dd_t::config_option_e;
  using device_prep_t = display_device::SingleDisplayConfiguration::DevicePreparation;
  using virtual_display_e = config::video_t::virtual_display_e;

  constexpr auto virtual_gdi_name {R"(\\.\DISPLAY9)"};  ///< GDI name reported by the fake backend.
  constexpr auto virtual_device_id {"{virtual-device-id}"};  ///< Device id of the virtual display in libdisplaydevice.
  constexpr auto physical_device_id {"{physical-device-id}"};  ///< Device id of the physical display.

  /**
   * @brief Ordered record of the calls made by the fakes, shared by the backend and the settings manager.
   */
  struct call_log_t {
    std::mutex mutex;  ///< Guards the call list.
    std::vector<std::string> calls;  ///< Call names in invocation order.

    /**
     * @brief Append a call name.
     * @param call The call name.
     */
    void add(std::string call) {
      std::lock_guard lock {mutex};
      calls.push_back(std::move(call));
    }

    /**
     * @brief Copy the recorded calls.
     * @return The call names in invocation order.
     */
    std::vector<std::string> snapshot() {
      std::lock_guard lock {mutex};
      return calls;
    }
  };

  /**
   * @brief Arguments of one `create` call.
   */
  struct create_call_t {
    virtual_display::mode_t mode;  ///< Requested mode.
    virtual_display::guid_t guid;  ///< Requested identifier.
    std::string serial;  ///< Requested serial.
  };

  /**
   * @brief Fake backend that records calls instead of talking to a driver.
   */
  class fake_backend_t: public virtual_display::backend_t {
  public:
    /**
     * @brief Construct a fake backend.
     * @param log Shared call log.
     * @param status Status reported by the backend.
     * @param create_result Name returned by `create`.
     */
    fake_backend_t(std::shared_ptr<call_log_t> log, virtual_display::driver_status_e status, std::optional<std::string> create_result):
        log {std::move(log)},
        status_value {status},
        create_result {std::move(create_result)} {}

    virtual_display::driver_status_e open() override {
      log->add("open");
      return status_value;
    }

    std::optional<std::string> create(const virtual_display::mode_t &mode, const virtual_display::guid_t &guid, std::string_view serial) override {
      log->add("create");
      creates.push_back({mode, guid, std::string {serial}});
      return create_result;
    }

    bool remove(const virtual_display::guid_t &guid) override {
      log->add("remove");
      removed.push_back(guid);
      return true;
    }

    virtual_display::driver_status_e status() const override {
      return status_value;
    }

    std::shared_ptr<call_log_t> log;  ///< Shared call log.
    virtual_display::driver_status_e status_value;  ///< Reported status.
    std::optional<std::string> create_result;  ///< Value returned by `create`.
    std::vector<create_call_t> creates;  ///< Recorded `create` arguments.
    std::vector<virtual_display::guid_t> removed;  ///< Recorded `remove` arguments.
  };

  /**
   * @brief Settings manager that records applied configurations and reverts.
   */
  class recording_settings_manager_t: public display_device::SettingsManagerInterface {
  public:
    /**
     * @brief Construct the manager.
     * @param log Shared call log.
     * @param persistence Persistence watched by the revert loop.
     */
    recording_settings_manager_t(std::shared_ptr<call_log_t> log, std::shared_ptr<display_device::SettingsPersistenceInterface> persistence):
        log {std::move(log)},
        persistence {std::move(persistence)} {}

    [[nodiscard]] display_device::EnumeratedDeviceList enumAvailableDevices() const override {
      return {
        {.m_device_id = physical_device_id, .m_display_name = R"(\\.\DISPLAY1)", .m_friendly_name = "Panel"},
        {.m_device_id = virtual_device_id, .m_display_name = virtual_gdi_name, .m_friendly_name = "Solstice"},
      };
    }

    [[nodiscard]] std::string getDisplayName(const std::string &device_id) const override {
      for (const auto &device : enumAvailableDevices()) {
        if (device.m_device_id == device_id) {
          return device.m_display_name;
        }
      }
      return {};
    }

    [[nodiscard]] ApplyResult applySettings(const display_device::SingleDisplayConfiguration &config) override {
      log->add("apply");
      std::lock_guard lock {mutex};
      applied.push_back(config);
      return ApplyResult::Ok;
    }

    [[nodiscard]] RevertResult revertSettings() override {
      log->add("revert");
      const auto result {revert_result.load()};
      if (result == RevertResult::Ok) {
        std::ignore = persistence->clear();
      }
      return result;
    }

    [[nodiscard]] bool resetPersistence() override {
      log->add("reset_persistence");
      return persistence->clear();
    }

    /**
     * @brief Copy the applied configurations.
     * @return Configurations in application order.
     */
    std::vector<display_device::SingleDisplayConfiguration> applied_configs() {
      std::lock_guard lock {mutex};
      return applied;
    }

    std::atomic<RevertResult> revert_result {RevertResult::Ok};  ///< Result returned by `revertSettings`.

  private:
    std::shared_ptr<call_log_t> log;  ///< Shared call log.
    std::shared_ptr<display_device::SettingsPersistenceInterface> persistence;  ///< Persistence watched by the revert loop.
    std::mutex mutex;  ///< Guards the applied list.
    std::vector<display_device::SingleDisplayConfiguration> applied;  ///< Applied configurations.
  };

  /**
   * @brief Expected identifier for a client id.
   * @param unique_id The client unique id.
   * @return The identifier the orchestrator should pass to the backend.
   */
  virtual_display::guid_t expected_guid([[maybe_unused]] std::string_view unique_id) {
#ifdef _WIN32
    return platf::vdisplay::guid_from_client_id(unique_id);
#else
    return virtual_display::guid_t {};
#endif
  }

  /**
   * @brief Build a launch session requesting a mode.
   * @param width Requested width.
   * @param height Requested height.
   * @param fps Requested frame rate.
   * @param unique_id Client unique id.
   * @return The session (heap allocated because launch sessions cannot be copied or moved).
   */
  std::unique_ptr<rtsp_stream::launch_session_t> make_session(int width, int height, int fps, std::string unique_id = "switch-1") {
    auto session {std::make_unique<rtsp_stream::launch_session_t>()};
    session->width = width;
    session->height = height;
    session->fps = fps;
    session->unique_id = std::move(unique_id);
    session->enable_hdr = true;
    return session;
  }
}  // namespace

/**
 * @brief Fixture installing a fake backend and a recording settings manager.
 */
class VirtualDisplay: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    saved_video = config::video;
    config::video.virtual_display = virtual_display_e::always;
    config::video.output_name = physical_device_id;
    config::video.dd.configuration_option = config_option_e::disabled;
    log = std::make_shared<call_log_t>();
    install_backend(virtual_display::driver_status_e::ok, std::string {virtual_gdi_name});

    const auto persistence {display_device::test_make_watched_persistence()};
    auto manager {std::make_unique<recording_settings_manager_t>(log, persistence)};
    settings = manager.get();
    display_device::test_install_settings_manager(std::move(manager), persistence);
  }

  void TearDown() override {
    virtual_display::end();
    virtual_display::test_install_backend(nullptr);
    display_device::test_reset_display_device();
    config::video = saved_video;
    BaseTest::TearDown();
  }

  /**
   * @brief Replace the backend with a fresh fake.
   * @param status Status reported by the fake.
   * @param create_result Name returned by `create`.
   */
  void install_backend(virtual_display::driver_status_e status, std::optional<std::string> create_result) {
    auto fake {std::make_unique<fake_backend_t>(log, status, std::move(create_result))};
    backend = fake.get();
    virtual_display::test_install_backend(std::move(fake));
  }

  config::video_t saved_video {};  ///< Video config restored after the test.
  std::shared_ptr<call_log_t> log;  ///< Shared call log.
  fake_backend_t *backend {nullptr};  ///< Installed fake backend.
  recording_settings_manager_t *settings {nullptr};  ///< Installed settings manager.
};

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
