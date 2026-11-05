/**
 * @file src/virtual_display.h
 * @brief Declarations for deciding when a stream uses a virtual display.
 */
#pragma once

// standard includes
#include <string_view>

// local includes
#include "config.h"

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
}  // namespace virtual_display
