/**
 * @file src/virtual_display.h
 * @brief Declarations for the virtual display session lifecycle.
 */
#pragma once

// standard includes
#include <memory>
#include <string_view>

// local includes
#include "config.h"
#include "virtual_display_types.h"

namespace rtsp_stream {
  struct launch_session_t;
}

namespace virtual_display {
  /**
   * @brief Decide whether a session should stream from a virtual display.
   *
   * An app that sets "enabled" always uses it and one that sets "disabled" never does.
   * An app without an override uses it only when the global setting is `always`.
   *
   * @param global The global virtual display policy.
   * @param app_value The app's "virtual-display" value: "enabled", "disabled", or empty to follow the global policy.
   * @return `true` when the session should use a virtual display.
   */
  bool should_use(config::video_t::virtual_display_e global, std::string_view app_value);

  /**
   * @brief Create the platform backend and open the virtual display driver.
   *
   * On Windows this opens SudoVDA and selects `config::video.adapter_name` as its render adapter.
   * Other platforms get a backend whose status is always `failed`. Never throws; the status is logged.
   */
  void init();

  /**
   * @brief Start streaming from a virtual display when the session asks for one.
   *
   * Creates a virtual display with the client's requested mode, points `config::video.output_name` at it,
   * forces HDR off and configures it as the primary display (`disabled` and `verify_only` are upgraded to
   * `ensure_primary`; `ensure_only_display` is respected). When a virtual display is already active (resume),
   * no new display is created and only the new mode is applied.
   *
   * @param session The launch session. Its `enable_hdr` is cleared when the virtual display is used.
   * @param app_value The app's "virtual-display" value.
   * An active virtual display that the session no longer wants is torn down as in `end()` before returning `false`.
   *
   * @return `true` when the virtual display is used and the display was configured;
   *         `false` when the caller should configure the physical display as usual.
   */
  bool begin(rtsp_stream::launch_session_t &session, std::string_view app_value);

  /**
   * @brief Stop using the virtual display.
   *
   * Reverts the display configuration immediately (falling back to the scheduled revert when that fails),
   * then removes the virtual display and restores the original
   * `config::video.output_name`. Does nothing when no virtual display is active.
   */
  void end();

  /**
   * @brief Report whether a virtual display is in use.
   * @return `true` between a successful `begin()` and `end()`.
   */
  bool active();

#ifdef SUNSHINE_TESTS
  /**
   * @brief Replace the backend and drop any active session state.
   * @param backend Backend used by later calls, or nullptr for none.
   */
  void test_install_backend(std::unique_ptr<backend_t> backend);
#endif
}  // namespace virtual_display
