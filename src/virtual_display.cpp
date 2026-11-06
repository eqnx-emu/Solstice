/**
 * @file src/virtual_display.cpp
 * @brief Definitions for the virtual display session lifecycle.
 */
// standard includes
#include <charconv>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>

// local includes
#include "display_device.h"
#include "logging.h"
#include "platform/common.h"
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
    constexpr std::chrono::milliseconds default_apply_window {3s};  ///< How long `begin()` retries applying the display configuration.

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
     * @brief Orchestrator state shared by launch, resume, terminate and the stream sessions.
     */
    struct {
      std::mutex mutex;  ///< Guards every field below.
      std::unique_ptr<backend_t> backend;  ///< Installed driver backend.
      bool active {false};  ///< True while a virtual display is in use.
      guid_t guid {};  ///< Identifier of the active virtual display.
      mode_t mode {};  ///< Mode the active virtual display was created with.
      std::string original_output_name;  ///< `config::video.output_name` before the virtual display took over.
      bool launched_with_display {false};  ///< True when the running app was launched on a virtual display.
      int sessions {0};  ///< Stream sessions whose capture threads may be running.
      bool end_pending {false};  ///< True when `end()` was deferred until the last session stops.
      std::optional<std::chrono::steady_clock::time_point> last_reopen;  ///< Time of the last driver re-open attempt.
      std::filesystem::path state_file;  ///< File recording the active virtual display, empty for none.
      std::chrono::milliseconds apply_window {default_apply_window};  ///< How long `begin()` retries applying the configuration.
      std::condition_variable teardown_cv;  ///< Wakes the teardown worker and the threads waiting for it.
      bool teardown_requested {false};  ///< True while a teardown was handed to the worker and has not run yet.
      bool worker_running {false};  ///< True while the teardown worker thread runs its loop.
      bool worker_stop {false};  ///< Asks the teardown worker to exit once no teardown is requested.
      bool shut_down {false};  ///< True after `shutdown()`: teardowns run in the calling thread.
      std::thread worker;  ///< Teardown worker thread, started on first use.
#ifdef SUNSHINE_TESTS
      bool test_hold_worker {false};  ///< Keeps the worker from picking up requests, unless it is asked to stop.
#endif
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
     * @brief Get the driver status, opening the driver when it is not usable.
     *
     * The driver is opened here the first time a session needs it (see `init()`). Re-opening is attempted at most
     * once every `reopen_interval`. It recovers from a driver that was installed after Sunshine started (fresh
     * install or upgrade) and from a watchdog failure.
     *
     * @return The driver status after the optional open.
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

      BOOST_LOG(info) << "Virtual display driver status is "sv << status_name(status) << ", opening the driver."sv;
      const auto reopened {open_backend_unlocked()};
      BOOST_LOG(info) << "Virtual display driver status: "sv << status_name(reopened);
      return reopened;
    }

    /**
     * @brief Apply the display configuration for the active virtual display and report whether it worked.
     *
     * The virtual display must become primary, so `disabled`, `verify_only` and `ensure_active` are upgraded to
     * `ensure_primary`. Resolution and refresh rate always follow the client's request, and HDR is requested off.
     *
     * @param session The launch session. `enable_sops` is set only for the duration of the call, `enable_hdr` is
     *                cleared when the configuration was applied and left unchanged otherwise.
     * @return True when the configuration was applied.
     * @note Must be called with `STATE.mutex` held and after `config::video.output_name` points at the virtual display.
     */
    bool configure_virtual_display_unlocked(rtsp_stream::launch_session_t &session) {
      using config_option_e = config::video_t::dd_t::config_option_e;

      auto video_config {config::video};
      switch (video_config.dd.configuration_option) {
        case config_option_e::disabled:
        case config_option_e::verify_only:
        case config_option_e::ensure_active:
          video_config.dd.configuration_option = config_option_e::ensure_primary;
          break;
        case config_option_e::ensure_primary:
        case config_option_e::ensure_only_display:
          break;
      }
      video_config.dd.resolution_option = config::video_t::dd_t::resolution_option_e::automatic;
      video_config.dd.refresh_rate_option = config::video_t::dd_t::refresh_rate_option_e::automatic;

      // The virtual display is ours, so apply the requested mode even without "Optimize game settings".
      const auto restore_sops {util::fail_guard([&session, enable_sops = std::exchange(session.enable_sops, true)]() {
        session.enable_sops = enable_sops;
      })};
      const auto enable_hdr {std::exchange(session.enable_hdr, false)};
      if (!display_device::configure_display_now(video_config, session, STATE.apply_window)) {
        session.enable_hdr = enable_hdr;
        return false;
      }
      return true;
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
     * @brief Handle a request to stop using the virtual display, deferring it while sessions run.
     * @note Must be called with `STATE.mutex` held.
     */
    void request_end_unlocked() {
      STATE.launched_with_display = false;
      if (STATE.active && STATE.sessions > 0) {
        BOOST_LOG(info) << "Removing the virtual display once the stream has stopped."sv;
        STATE.end_pending = true;
        return;
      }
      end_unlocked();
    }

    /**
     * @brief Loop of the teardown worker: run each requested teardown that is still due.
     *
     * Exits when `STATE.worker_stop` is set and no teardown is requested, so a pending request always runs first.
     */
    void teardown_worker_main() {
      platf::set_thread_name("virtual_display::teardown");
      std::unique_lock lock {STATE.mutex};
      for (;;) {
        STATE.teardown_cv.wait(lock, []() {
#ifdef SUNSHINE_TESTS
          if (STATE.test_hold_worker && !STATE.worker_stop) {
            return false;
          }
#endif
          return STATE.teardown_requested || STATE.worker_stop;
        });
        if (!STATE.teardown_requested) {
          break;
        }
        STATE.teardown_requested = false;
        // A begin() or a new session may have run since the request; only a teardown that is still due runs.
        if (STATE.end_pending && STATE.sessions == 0) {
          try {
            end_unlocked();
          } catch (const std::exception &e) {
            BOOST_LOG(error) << "Virtual display teardown failed: "sv << e.what();
          }
        }
        STATE.teardown_cv.notify_all();
      }
      STATE.worker_running = false;
      STATE.teardown_cv.notify_all();
    }

    /**
     * @brief Hand the deferred teardown to the worker thread, starting it when needed.
     *
     * After `shutdown()` the teardown runs in the calling thread instead.
     *
     * @note Must be called with `STATE.mutex` held.
     */
    void dispatch_teardown_unlocked() {
      if (STATE.shut_down) {
        end_unlocked();
        return;
      }
      STATE.teardown_requested = true;
      if (!STATE.worker_running) {
        if (STATE.worker.joinable()) {
          // The previous worker has left its loop and released the lock, so joining it cannot block on us.
          STATE.worker.join();
        }
        STATE.worker_running = true;
        STATE.worker = std::thread {teardown_worker_main};
      }
      STATE.teardown_cv.notify_all();
    }

    /**
     * @brief Stop the teardown worker after it has run any requested teardown.
     * @param mark_shut_down Make later teardowns run in the calling thread.
     * @note Must be called without `STATE.mutex` held.
     */
    void stop_worker(const bool mark_shut_down) {
      std::thread worker;
      {
        std::lock_guard lock {STATE.mutex};
        if (mark_shut_down) {
          STATE.shut_down = true;
        }
        STATE.worker_stop = true;
        worker = std::move(STATE.worker);
      }
      STATE.teardown_cv.notify_all();
      if (worker.joinable()) {
        worker.join();
      }
      std::lock_guard lock {STATE.mutex};
      STATE.worker_stop = false;
    }

    /**
     * @brief Joins the teardown worker at exit when `shutdown()` was not called, instead of terminating.
     *
     * A teardown still requested at that point is dropped, because other subsystems may already be destroyed;
     * the state file lets the next start clean up.
     */
    struct worker_exit_guard_t {
      worker_exit_guard_t() = default;
      worker_exit_guard_t(const worker_exit_guard_t &) = delete;
      worker_exit_guard_t &operator=(const worker_exit_guard_t &) = delete;

      ~worker_exit_guard_t() {
        {
          std::lock_guard lock {STATE.mutex};
          STATE.teardown_requested = false;
        }
        stop_worker(true);
      }
    } worker_exit_guard;  ///< Destroyed before `STATE`, which is defined earlier in this file.

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
     * @brief Install a backend and clean up after a crash, opening the driver only when that needs it.
     * @param backend The backend.
     * @param state_file File recording the active virtual display.
     * @note Must be called with `STATE.mutex` held.
     */
    void init_unlocked(std::unique_ptr<backend_t> backend, const std::filesystem::path &state_file) {
      STATE.backend = std::move(backend);
      STATE.state_file = state_file;
      if (std::error_code ec; state_file.empty() || !std::filesystem::exists(state_file, ec)) {
        BOOST_LOG(info) << "Virtual display driver will be opened when a stream first needs it."sv;
        return;
      }
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
    if (STATE.end_pending && STATE.sessions == 0) {
      // The worker has not run the deferred teardown yet; run it now so it cannot remove the display set up below.
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
      BOOST_LOG(info) << "Reusing virtual display with mode "sv << session.width << 'x' << session.height << '@' << session.fps;
      if (!configure_virtual_display_unlocked(session)) {
        BOOST_LOG(warning) << "Could not configure the virtual display, removing it and streaming from the physical display."sv;
        end_unlocked();
        STATE.launched_with_display = false;
        return false;
      }
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
    if (!configure_virtual_display_unlocked(session)) {
      BOOST_LOG(warning) << "Could not configure the new virtual display, removing it and streaming from the physical display."sv;
      end_unlocked();
      STATE.launched_with_display = false;
      return false;
    }
    return true;
  }

  void end() {
    std::lock_guard lock {STATE.mutex};
    request_end_unlocked();
  }

  void end_or_revert(const bool keep_virtual_display) {
    std::lock_guard lock {STATE.mutex};
    if (STATE.active) {
      if (!keep_virtual_display) {
        request_end_unlocked();
      }
      return;
    }
    if (!keep_virtual_display) {
      STATE.launched_with_display = false;
    }
    display_device::revert_configuration();
  }

  void session_started() {
    std::lock_guard lock {STATE.mutex};
    ++STATE.sessions;
  }

  void session_stopped() {
    std::lock_guard lock {STATE.mutex};
    if (STATE.sessions == 0) {
      BOOST_LOG(warning) << "Virtual display session count is unbalanced, ignoring a session stop."sv;
      return;
    }
    if (--STATE.sessions == 0 && STATE.end_pending) {
      dispatch_teardown_unlocked();
    }
  }

  void shutdown() {
    stop_worker(true);
    std::lock_guard lock {STATE.mutex};
    if (STATE.end_pending && STATE.sessions == 0) {
      end_unlocked();
    }
  }

  bool active() {
    std::lock_guard lock {STATE.mutex};
    return STATE.active;
  }

#ifdef SUNSHINE_TESTS
  void test_install_backend(std::unique_ptr<backend_t> backend) {
    stop_worker(false);
    std::lock_guard lock {STATE.mutex};
    STATE.backend = std::move(backend);
    STATE.active = false;
    STATE.guid = {};
    STATE.mode = {};
    STATE.original_output_name.clear();
    STATE.launched_with_display = false;
    STATE.sessions = 0;
    STATE.end_pending = false;
    STATE.last_reopen.reset();
    STATE.state_file.clear();
    STATE.apply_window = default_apply_window;
    STATE.teardown_requested = false;
    STATE.shut_down = false;
    STATE.test_hold_worker = false;
  }

  void test_init(std::unique_ptr<backend_t> backend, const std::filesystem::path &state_file) {
    std::lock_guard lock {STATE.mutex};
    init_unlocked(std::move(backend), state_file);
  }

  void test_wait_for_teardown() {
    std::unique_lock lock {STATE.mutex};
    STATE.teardown_cv.wait(lock, []() {
      return !STATE.teardown_requested;
    });
  }

  void test_set_apply_window(const std::chrono::milliseconds window) {
    std::lock_guard lock {STATE.mutex};
    STATE.apply_window = window;
  }

  int test_session_count() {
    std::lock_guard lock {STATE.mutex};
    return STATE.sessions;
  }

  void test_hold_teardown_worker(const bool hold) {
    {
      std::lock_guard lock {STATE.mutex};
      STATE.test_hold_worker = hold;
    }
    STATE.teardown_cv.notify_all();
  }
#endif
}  // namespace virtual_display
