/**
 * @file src/virtual_display.cpp
 * @brief Definitions for the virtual display session lifecycle.
 */
// standard includes
#include <charconv>
#include <chrono>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

// local includes
#include "display_device.h"
#include "logging.h"
#include "rtsp.h"
#include "utility.h"
#include "virtual_display.h"

#ifdef _WIN32
  #include "platform/windows/virtual_display.h"
#endif

using namespace std::literals;

namespace virtual_display {
  namespace {
    constexpr std::size_t max_serial_length {13};  ///< EDID serial field holds 13 characters plus the terminator.
    constexpr auto reopen_interval {10s};  ///< Minimum time between two attempts to re-open an unusable driver.

#ifndef _WIN32
    /**
     * @brief Backend used where no virtual display driver exists.
     */
    class null_backend_t: public backend_t {
    public:
      driver_status_e open() override {
        return driver_status_e::failed;
      }

      std::optional<std::string> create(const mode_t &, const guid_t &, std::string_view) override {
        return std::nullopt;
      }

      bool remove(const guid_t &) override {
        return false;
      }

      driver_status_e status() const override {
        return driver_status_e::failed;
      }
    };
#endif

    /**
     * @brief Orchestrator state shared by launch, resume and terminate.
     */
    struct {
      std::mutex mutex;  ///< Guards every field below.
      std::unique_ptr<backend_t> backend;  ///< Installed driver backend.
      bool active {false};  ///< True while a virtual display is in use.
      guid_t guid {};  ///< Identifier of the active virtual display.
      mode_t mode {};  ///< Mode the active virtual display was created with.
      std::string original_output_name;  ///< `config::video.output_name` before the virtual display took over.
      bool launched_with_display {false};  ///< True when the running app was launched on a virtual display.
      bool streaming {false};  ///< True while stream sessions (and their capture threads) are running.
      bool end_pending {false};  ///< True when `end()` was deferred until streaming stops.
      std::optional<std::chrono::steady_clock::time_point> last_reopen;  ///< Time of the last driver re-open attempt.
      std::filesystem::path state_file;  ///< File recording the active virtual display, empty for none.
    } STATE;

    /**
     * @brief Get a printable name of a driver status.
     * @param status The driver status.
     * @return The status name.
     */
    std::string_view status_name(const driver_status_e status) {
      switch (status) {
        case driver_status_e::ok:
          return "ok"sv;
        case driver_status_e::failed:
          return "failed"sv;
        case driver_status_e::version_incompatible:
          return "version_incompatible"sv;
        case driver_status_e::watchdog_failed:
          return "watchdog_failed"sv;
        case driver_status_e::unknown:
          break;
      }
      return "unknown"sv;
    }

    /**
     * @brief Derive the identifier of a client's virtual display.
     * @param identity The client identity (see `client_identity()`).
     * @return The identifier (all zero where no driver exists).
     */
    guid_t client_guid([[maybe_unused]] std::string_view identity) {
#ifdef _WIN32
      return platf::vdisplay::guid_from_client_id(identity);
#else
      return guid_t {};
#endif
    }

    /**
     * @brief Format an identifier as 32 uppercase hexadecimal digits.
     * @param guid The identifier.
     * @return The hexadecimal text.
     */
    std::string to_hex(const guid_t &guid) {
      constexpr auto digits {"0123456789ABCDEF"sv};
      std::string out;
      out.reserve(guid.bytes.size() * 2);
      for (const auto byte : guid.bytes) {
        out.push_back(digits[byte >> 4]);
        out.push_back(digits[byte & 0x0f]);
      }
      return out;
    }

    /**
     * @brief Parse an identifier written by `to_hex()`.
     * @param hex The hexadecimal text.
     * @return The identifier, or nullopt when the text is malformed.
     */
    std::optional<guid_t> from_hex(std::string_view hex) {
      guid_t guid;
      if (hex.size() != guid.bytes.size() * 2) {
        return std::nullopt;
      }
      for (std::size_t i = 0; i < guid.bytes.size(); ++i) {
        const auto *first {hex.data() + i * 2};
        if (const auto [ptr, ec] {std::from_chars(first, first + 2, guid.bytes[i], 16)}; ec != std::errc {} || ptr != first + 2) {
          return std::nullopt;
        }
      }
      return guid;
    }

    /**
     * @brief Derive the EDID serial of a client's virtual display from its identifier.
     * @param guid The identifier.
     * @return The serial, 13 characters long.
     */
    std::string serial_for(const guid_t &guid) {
      return to_hex(guid).substr(0, max_serial_length);
    }

    /**
     * @brief Record the active virtual display, so that a crash can be cleaned up by the next `init()`.
     * @note Must be called with `STATE.mutex` held. Failures are only logged.
     */
    void write_state_unlocked() {
      if (STATE.state_file.empty()) {
        return;
      }
      std::ofstream file {STATE.state_file, std::ios::trunc};
      file << to_hex(STATE.guid) << ' ' << STATE.mode.width << ' ' << STATE.mode.height << ' ' << STATE.mode.fps << '\n';
      if (!file) {
        BOOST_LOG(warning) << "Failed to write the virtual display state file "sv << STATE.state_file.string();
      }
    }

    /**
     * @brief Delete the record written by `write_state_unlocked()`.
     * @note Must be called with `STATE.mutex` held.
     */
    void clear_state_unlocked() {
      if (STATE.state_file.empty()) {
        return;
      }
      std::error_code ec;
      std::filesystem::remove(STATE.state_file, ec);
    }

    /**
     * @brief Open the driver and select the render adapter.
     * @return The resulting driver status.
     * @note Must be called with `STATE.mutex` held and a backend installed.
     */
    driver_status_e open_backend_unlocked() {
      const auto status {STATE.backend->open()};
#ifdef _WIN32
      if (auto *sudovda {dynamic_cast<platf::vdisplay::sudovda_backend_t *>(STATE.backend.get())}; sudovda && status == driver_status_e::ok) {
        sudovda->set_render_adapter(config::video.adapter_name);
      }
#endif
      return status;
    }

    /**
     * @brief Get the driver status, re-opening the driver when it is not usable.
     *
     * The re-open is attempted at most once every `reopen_interval`. It recovers from a driver that was installed
     * after Sunshine started (fresh install or upgrade) and from a watchdog failure.
     *
     * @return The driver status after the optional re-open.
     * @note Must be called with `STATE.mutex` held.
     */
    driver_status_e ensure_driver_unlocked() {
      if (!STATE.backend) {
        return driver_status_e::failed;
      }
      const auto status {STATE.backend->status()};
      if (status == driver_status_e::ok) {
        return status;
      }

      const auto now {std::chrono::steady_clock::now()};
      if (STATE.last_reopen && now - *STATE.last_reopen < reopen_interval) {
        return status;
      }
      STATE.last_reopen = now;

      BOOST_LOG(info) << "Virtual display driver status is "sv << status_name(status) << ", re-opening the driver."sv;
      const auto reopened {open_backend_unlocked()};
      BOOST_LOG(info) << "Virtual display driver status: "sv << status_name(reopened);
      return reopened;
    }

    /**
     * @brief Apply the display configuration for the active virtual display.
     *
     * The virtual display must become primary, so `disabled` and `verify_only` are upgraded to
     * `ensure_primary`. Resolution and refresh rate always follow the client's request.
     *
     * @param session The launch session. `enable_sops` is set only for the duration of the call.
     * @note Must be called with `STATE.mutex` held and after `config::video.output_name` points at the virtual display.
     */
    void configure_virtual_display(rtsp_stream::launch_session_t &session) {
      using config_option_e = config::video_t::dd_t::config_option_e;

      auto video_config {config::video};
      if (video_config.dd.configuration_option == config_option_e::disabled || video_config.dd.configuration_option == config_option_e::verify_only) {
        video_config.dd.configuration_option = config_option_e::ensure_primary;
      }
      video_config.dd.resolution_option = config::video_t::dd_t::resolution_option_e::automatic;
      video_config.dd.refresh_rate_option = config::video_t::dd_t::refresh_rate_option_e::automatic;

      // The virtual display is ours, so apply the requested mode even without "Optimize game settings".
      const auto restore_sops {util::fail_guard([&session, enable_sops = std::exchange(session.enable_sops, true)]() {
        session.enable_sops = enable_sops;
      })};
      display_device::configure_display(video_config, session);
    }

    /**
     * @brief Tear down the active virtual display.
     *
     * Reverts the display configuration immediately while the virtual display still exists, falling back to the
     * scheduled revert when that fails, then removes the display and restores `config::video.output_name`.
     *
     * @note Must be called with `STATE.mutex` held and no stream running. Does nothing when no virtual display is active.
     */
    void end_unlocked() {
      STATE.end_pending = false;
      if (!STATE.active) {
        return;
      }

      // Revert while the virtual display still exists, so the physical primary display can be restored.
      if (!display_device::revert_configuration_now()) {
        BOOST_LOG(warning) << "Immediate display configuration revert failed, retrying in the background."sv;
        display_device::revert_configuration();
      }
      if (STATE.backend && !STATE.backend->remove(STATE.guid)) {
        BOOST_LOG(warning) << "Failed to remove the virtual display."sv;
      }
      clear_state_unlocked();
      config::video.output_name = std::move(STATE.original_output_name);
      STATE.original_output_name.clear();
      STATE.active = false;
    }

    /**
     * @brief Clean up a virtual display left behind by a crash.
     *
     * SudoVDA cannot list its monitors, so the identifier comes from the state file. The monitor is removed in case
     * the watchdog has not done so yet (a fast restart keeps it alive, since any open handle pings the driver).
     * It is then re-created and torn down as in `end()`, so that the display configuration persisted by
     * `display_device` at the time of the crash, whose initial topology contains the virtual display, can be reverted.
     *
     * @note Must be called with `STATE.mutex` held, after the driver was opened.
     */
    void recover_unlocked() {
      if (STATE.state_file.empty()) {
        return;
      }

      std::ifstream file {STATE.state_file};
      if (!file) {
        return;
      }
      std::string hex;
      mode_t mode {};
      file >> hex >> mode.width >> mode.height >> mode.fps;
      if (!file) {
        mode = {};
      }
      file.close();
      const auto guid {from_hex(hex)};
      if (!guid || mode.width == 0 || mode.height == 0 || mode.fps == 0) {
        BOOST_LOG(warning) << "Ignoring a malformed virtual display state file."sv;
        clear_state_unlocked();
        return;
      }

      if (!STATE.backend || STATE.backend->status() != driver_status_e::ok) {
        // Without the driver the monitor cannot exist; keep the record for the next start.
        return;
      }

      BOOST_LOG(info) << "Cleaning up the virtual display left behind by the previous run."sv;
      STATE.backend->remove(*guid);
      if (!STATE.backend->create(mode, *guid, serial_for(*guid))) {
        BOOST_LOG(warning) << "Could not re-create the previous virtual display; its display configuration may not be restored."sv;
        clear_state_unlocked();
        return;
      }

      STATE.guid = *guid;
      STATE.mode = mode;
      STATE.original_output_name = config::video.output_name;
      STATE.active = true;
      end_unlocked();
    }

    /**
     * @brief Install a backend, open it and clean up after a crash.
     * @param backend The backend.
     * @param state_file File recording the active virtual display.
     * @note Must be called with `STATE.mutex` held.
     */
    void init_unlocked(std::unique_ptr<backend_t> backend, const std::filesystem::path &state_file) {
      STATE.backend = std::move(backend);
      STATE.state_file = state_file;
      const auto status {open_backend_unlocked()};
      BOOST_LOG(info) << "Virtual display driver status: "sv << status_name(status);
      recover_unlocked();
    }
  }  // namespace

  bool should_use(config::video_t::virtual_display_e global, std::string_view app_value) {
    if (global == config::video_t::virtual_display_e::disabled) {
      return false;
    }
    if (app_value == "enabled"sv) {
      return true;
    }
    if (app_value == "disabled"sv) {
      return false;
    }
    return global == config::video_t::virtual_display_e::always;
  }

  std::string client_identity(const rtsp_stream::launch_session_t &session) {
    return session.client_cert.empty() ? session.unique_id : session.client_cert;
  }

  void init(const std::filesystem::path &state_file) {
    try {
      std::lock_guard lock {STATE.mutex};
#ifdef _WIN32
      init_unlocked(std::make_unique<platf::vdisplay::sudovda_backend_t>(), state_file);
#else
      init_unlocked(std::make_unique<null_backend_t>(), state_file);
#endif
    } catch (const std::exception &e) {
      BOOST_LOG(warning) << "Virtual display driver initialization failed: "sv << e.what();
    }
  }

  bool begin(rtsp_stream::launch_session_t &session, std::string_view app_value, const start_e start) {
    std::lock_guard lock {STATE.mutex};
    if (STATE.end_pending && !STATE.streaming) {
      end_unlocked();
    }

    if (!should_use(config::video.virtual_display, app_value)) {
      // A display left over from a session that wanted one must not be targeted by the physical display path.
      end_unlocked();
      STATE.launched_with_display = false;
      return false;
    }

    if (STATE.active && STATE.backend && STATE.backend->status() != driver_status_e::ok) {
      // The driver connection was lost (watchdog), so the display is gone and Windows moved the game elsewhere.
      BOOST_LOG(warning) << "Virtual display driver connection was lost (status: "sv << status_name(STATE.backend->status()) << ")."sv;
      end_unlocked();  // also restores config::video.output_name
      if (start == start_e::resume) {
        // Re-creating the display now would leave the stream on an empty desktop: the game is on the physical display.
        BOOST_LOG(warning) << "Not re-creating the virtual display on resume, streaming from the physical display."sv;
        STATE.launched_with_display = false;
        return false;
      }
    }

    if (STATE.active) {
      session.enable_hdr = false;
      BOOST_LOG(info) << "Reusing virtual display with mode "sv << session.width << 'x' << session.height << '@' << session.fps;
      configure_virtual_display(session);
      return true;
    }

    if (start == start_e::launch) {
      STATE.launched_with_display = false;
    } else if (!STATE.launched_with_display) {
      BOOST_LOG(info) << "The running app was launched on the physical display, resuming without a virtual display."sv;
      return false;
    }

    const auto status {ensure_driver_unlocked()};
    if (status != driver_status_e::ok) {
      BOOST_LOG(warning) << "Virtual display driver is unavailable (status: "sv << status_name(status) << "), streaming from the physical display."sv;
      return false;
    }

    const mode_t mode {static_cast<uint32_t>(session.width), static_cast<uint32_t>(session.height), static_cast<uint32_t>(session.fps)};
    const auto guid {client_guid(client_identity(session))};
    // A monitor with this identifier may survive a crash (see recover_unlocked); creating it again would fail.
    STATE.backend->remove(guid);
    const auto gdi_name {STATE.backend->create(mode, guid, serial_for(guid))};
    if (!gdi_name) {
      BOOST_LOG(warning) << "Failed to create a virtual display, streaming from the physical display."sv;
      STATE.backend->remove(guid);
      return false;
    }

    auto device_id {display_device::map_display_name(*gdi_name)};
    if (device_id.empty()) {
      BOOST_LOG(warning) << "Virtual display "sv << *gdi_name << " is not known to the display device API, streaming from the physical display."sv;
      STATE.backend->remove(guid);
      return false;
    }

    BOOST_LOG(info) << "Created virtual display "sv << *gdi_name << " with mode "sv << mode.width << 'x' << mode.height << '@' << mode.fps;
    STATE.original_output_name = std::exchange(config::video.output_name, std::move(device_id));
    STATE.guid = guid;
    STATE.mode = mode;
    STATE.active = true;
    STATE.launched_with_display = true;
    write_state_unlocked();
    session.enable_hdr = false;
    configure_virtual_display(session);
    return true;
  }

  void end() {
    std::lock_guard lock {STATE.mutex};
    STATE.launched_with_display = false;
    if (STATE.active && STATE.streaming) {
      BOOST_LOG(info) << "Removing the virtual display once the stream has stopped."sv;
      STATE.end_pending = true;
      return;
    }
    end_unlocked();
  }

  void streaming_started() {
    std::lock_guard lock {STATE.mutex};
    STATE.streaming = true;
  }

  void streaming_stopped() {
    std::lock_guard lock {STATE.mutex};
    STATE.streaming = false;
    if (STATE.end_pending) {
      end_unlocked();
    }
  }

  bool active() {
    std::lock_guard lock {STATE.mutex};
    return STATE.active;
  }

#ifdef SUNSHINE_TESTS
  void test_install_backend(std::unique_ptr<backend_t> backend) {
    std::lock_guard lock {STATE.mutex};
    STATE.backend = std::move(backend);
    STATE.active = false;
    STATE.guid = {};
    STATE.mode = {};
    STATE.original_output_name.clear();
    STATE.launched_with_display = false;
    STATE.streaming = false;
    STATE.end_pending = false;
    STATE.last_reopen.reset();
    STATE.state_file.clear();
  }

  void test_init(std::unique_ptr<backend_t> backend, const std::filesystem::path &state_file) {
    std::lock_guard lock {STATE.mutex};
    init_unlocked(std::move(backend), state_file);
  }
#endif
}  // namespace virtual_display
