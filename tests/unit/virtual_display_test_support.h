/**
 * @file tests/unit/virtual_display_test_support.h
 * @brief Fakes and fixture shared by the virtual display tests.
 */
#pragma once

// test includes
#include "../tests_common.h"

// standard includes
#include <atomic>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
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

namespace virtual_display_test {
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
      status_value = open_status.value_or(status_value);
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
    std::optional<virtual_display::driver_status_e> open_status;  ///< Status adopted by `open`, if set.
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
   * @brief Expected identifier for a client identity.
   * @param identity The client certificate, or the unique id of a session without one.
   * @return The identifier the orchestrator should pass to the backend.
   */
  inline virtual_display::guid_t expected_guid([[maybe_unused]] std::string_view identity) {
#ifdef _WIN32
    return platf::vdisplay::guid_from_client_id(identity);
#else
    return virtual_display::guid_t {};
#endif
  }

  /**
   * @brief Expected EDID serial for a monitor identifier: its first 13 uppercase hexadecimal digits.
   * @param guid The monitor identifier.
   * @return The serial the orchestrator should pass to the backend.
   */
  inline std::string expected_serial(const virtual_display::guid_t &guid) {
    constexpr std::string_view digits {"0123456789ABCDEF"};
    std::string hex;
    for (const auto byte : guid.bytes) {
      hex.push_back(digits[byte >> 4]);
      hex.push_back(digits[byte & 0x0f]);
    }
    return hex.substr(0, 13);
  }

  /**
   * @brief Build a launch session requesting a mode.
   * @param width Requested width.
   * @param height Requested height.
   * @param fps Requested frame rate.
   * @param unique_id Client unique id.
   * @param client_cert Paired client certificate, empty for none.
   * @return The session (heap allocated because launch sessions cannot be copied or moved).
   */
  inline std::unique_ptr<rtsp_stream::launch_session_t> make_session(int width, int height, int fps, std::string unique_id = "switch-1", std::string client_cert = {}) {
    auto session {std::make_unique<rtsp_stream::launch_session_t>()};
    session->width = width;
    session->height = height;
    session->fps = fps;
    session->unique_id = std::move(unique_id);
    session->client_cert = std::move(client_cert);
    session->enable_hdr = true;
    return session;
  }
}  // namespace virtual_display_test

/**
 * @brief Fixture installing a fake backend and a recording settings manager.
 */
class VirtualDisplay: public BaseTest {
protected:
  void SetUp() override {
    BaseTest::SetUp();
    std::error_code ec;
    std::filesystem::remove(state_file, ec);
    saved_video = config::video;
    config::video.virtual_display = virtual_display_test::virtual_display_e::always;
    config::video.output_name = virtual_display_test::physical_device_id;
    config::video.dd.configuration_option = virtual_display_test::config_option_e::disabled;
    log = std::make_shared<virtual_display_test::call_log_t>();
    install_backend(virtual_display::driver_status_e::ok, std::string {virtual_display_test::virtual_gdi_name});

    const auto persistence {display_device::test_make_watched_persistence()};
    auto manager {std::make_unique<virtual_display_test::recording_settings_manager_t>(log, persistence)};
    settings = manager.get();
    display_device::test_install_settings_manager(std::move(manager), persistence);
  }

  void TearDown() override {
    virtual_display::end();
    virtual_display::test_install_backend(nullptr);
    std::error_code ec;
    std::filesystem::remove(state_file, ec);
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
    auto fake {std::make_unique<virtual_display_test::fake_backend_t>(log, status, std::move(create_result))};
    backend = fake.get();
    virtual_display::test_install_backend(std::move(fake));
  }

  config::video_t saved_video {};  ///< Video config restored after the test.
  std::filesystem::path state_file {std::filesystem::temp_directory_path() / "sunshine_test_virtual_display.state"};  ///< State file used by init tests.
  std::shared_ptr<virtual_display_test::call_log_t> log;  ///< Shared call log.
  virtual_display_test::fake_backend_t *backend {nullptr};  ///< Installed fake backend.
  virtual_display_test::recording_settings_manager_t *settings {nullptr};  ///< Installed settings manager.
};
