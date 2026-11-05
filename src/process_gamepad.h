/**
 * @file src/process_gamepad.h
 * @brief Declarations for reading the running application's gamepad type override.
 *
 * Kept separate from process.h so Windows input code can use it without pulling in Boost.Asio after Windows.h.
 */
#pragma once

// standard includes
#include <string>

namespace proc {
  /**
   * @brief Get the gamepad type override of the running application.
   *
   * @return The running app's gamepad type, or an empty string when no app runs or it has no override.
   */
  std::string running_app_gamepad();
}  // namespace proc
