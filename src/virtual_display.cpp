/**
 * @file src/virtual_display.cpp
 * @brief Definitions for the virtual display session lifecycle.
 */
// standard includes
#include <exception>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

// local includes
#include "display_device.h"
#include "logging.h"
#include "rtsp.h"
#include "virtual_display.h"

#ifdef _WIN32
  #include "platform/windows/virtual_display.h"
#endif

using namespace std::literals;

namespace virtual_display {
  namespace {
    constexpr std::size_t max_serial_length {13};  ///< EDID serial field holds 13 characters plus the terminator.

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
      std::string original_output_name;  ///< `config::video.output_name` before the virtual display took over.
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
     * @param unique_id The client unique id.
     * @return The identifier (all zero where no driver exists).
     */
    guid_t client_guid([[maybe_unused]] std::string_view unique_id) {
#ifdef _WIN32
      return platf::vdisplay::guid_from_client_id(unique_id);
#else
      return guid_t {};
#endif
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
      const bool enable_sops {std::exchange(session.enable_sops, true)};
      display_device::configure_display(video_config, session);
      session.enable_sops = enable_sops;
    }
  }  // namespace

  bool should_use(config::video_t::virtual_display_e global, std::string_view app_value) {
    if (app_value == "enabled"sv) {
      return true;
    }
    if (app_value == "disabled"sv) {
      return false;
    }
    return global == config::video_t::virtual_display_e::always;
  }

  void init() {
    try {
      std::lock_guard lock {STATE.mutex};
#ifdef _WIN32
      auto backend {std::make_unique<platf::vdisplay::sudovda_backend_t>()};
      const auto status {backend->open()};
      backend->set_render_adapter(config::video.adapter_name);
      STATE.backend = std::move(backend);
#else
      STATE.backend = std::make_unique<null_backend_t>();
      const auto status {STATE.backend->open()};
#endif
      BOOST_LOG(info) << "Virtual display driver status: "sv << status_name(status);
    } catch (const std::exception &e) {
      BOOST_LOG(warning) << "Virtual display driver initialization failed: "sv << e.what();
    }
  }

  bool begin(rtsp_stream::launch_session_t &session, std::string_view app_value) {
    std::lock_guard lock {STATE.mutex};
    if (!should_use(config::video.virtual_display, app_value)) {
      return false;
    }

    if (STATE.active) {
      session.enable_hdr = false;
      BOOST_LOG(info) << "Reusing virtual display with mode "sv << session.width << 'x' << session.height << '@' << session.fps;
      configure_virtual_display(session);
      return true;
    }

    const auto status {STATE.backend ? STATE.backend->status() : driver_status_e::failed};
    if (status != driver_status_e::ok) {
      BOOST_LOG(warning) << "Virtual display driver is unavailable (status: "sv << status_name(status) << "), streaming from the physical display."sv;
      return false;
    }

    const mode_t mode {static_cast<uint32_t>(session.width), static_cast<uint32_t>(session.height), static_cast<uint32_t>(session.fps)};
    const auto guid {client_guid(session.unique_id)};
    const auto serial {std::string_view {session.unique_id}.substr(0, max_serial_length)};
    const auto gdi_name {STATE.backend->create(mode, guid, serial)};
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
    STATE.active = true;
    session.enable_hdr = false;
    configure_virtual_display(session);
    return true;
  }

  void end() {
    std::lock_guard lock {STATE.mutex};
    if (!STATE.active) {
      return;
    }

    // Revert while the virtual display still exists, so the physical primary display can be restored.
    display_device::revert_configuration();
    if (STATE.backend && !STATE.backend->remove(STATE.guid)) {
      BOOST_LOG(warning) << "Failed to remove the virtual display."sv;
    }
    config::video.output_name = std::move(STATE.original_output_name);
    STATE.original_output_name.clear();
    STATE.active = false;
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
    STATE.original_output_name.clear();
  }
#endif
}  // namespace virtual_display
