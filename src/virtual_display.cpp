/**
 * @file src/virtual_display.cpp
 * @brief Definitions for deciding when a stream uses a virtual display.
 */
// standard includes
#include <string_view>

// local includes
#include "virtual_display.h"

using namespace std::literals;

namespace virtual_display {
  bool should_use(config::video_t::virtual_display_e global, std::string_view app_value) {
    if (app_value == "enabled"sv) {
      return true;
    }
    if (app_value == "disabled"sv) {
      return false;
    }
    return global == config::video_t::virtual_display_e::always;
  }
}  // namespace virtual_display
