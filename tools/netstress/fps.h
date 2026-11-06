/**
 * @file tools/netstress/fps.h
 * @brief Frame rate resolution for solstice-netstress, free of platform dependencies so it can be unit-tested.
 */
#pragma once

// standard includes
#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

namespace netstress {
  constexpr int default_fps = 60;  ///< Frame rate used when neither `--fps` nor `SUNSHINE_CLIENT_FPS` is usable.
  constexpr int min_fps = 10;  ///< Lower bound of the resolved frame rate.
  constexpr int max_fps = 240;  ///< Upper bound of the resolved frame rate.

  /**
   * @brief Parse a positive decimal integer.
   *
   * @param text Text to parse; surrounding whitespace is not accepted.
   * @return The value, or std::nullopt when the text is empty, not entirely numeric, zero, or too long.
   */
  inline std::optional<long> parse_positive(std::string_view text) {
    if (text.empty() || text.size() > 9 || !std::ranges::all_of(text, [](char c) {
          return c >= '0' && c <= '9';
        })) {
      return std::nullopt;
    }
    const long value = std::strtol(std::string {text}.c_str(), nullptr, 10);
    if (value <= 0) {
      return std::nullopt;
    }
    return value;
  }

  /**
   * @brief Normalise a `SUNSHINE_CLIENT_FPS` value to frames per second.
   * @details Sunshine exports whole frames per second (the third field of the launch `mode`, e.g. "60"). Values
   * above 1000 are treated defensively as fractional rates scaled by 1000 (e.g. "59940" is 59.94 fps) and rounded.
   *
   * @param text The environment variable value.
   * @return Frames per second, or std::nullopt when absent or invalid.
   */
  inline std::optional<int> normalise_client_fps(std::string_view text) {
    const auto value = parse_positive(text);
    if (!value) {
      return std::nullopt;
    }
    if (*value > 1000) {
      return static_cast<int>((*value + 500) / 1000);
    }
    return static_cast<int>(*value);
  }

  /**
   * @brief Resolve the frame rate to draw at.
   * @details Priority: `--fps N`, then `SUNSHINE_CLIENT_FPS`, then default_fps. The result is clamped to
   * [min_fps, max_fps]. An invalid source is skipped in favour of the next one.
   *
   * @param cli_fps The `--fps` argument text, or empty when absent.
   * @param env_fps The `SUNSHINE_CLIENT_FPS` value, or empty when absent.
   * @return Frames per second in [min_fps, max_fps].
   */
  inline int resolve_fps(std::string_view cli_fps, std::string_view env_fps) {
    int fps = default_fps;
    if (const auto cli = parse_positive(cli_fps)) {
      fps = static_cast<int>(std::min<long>(*cli, max_fps));
    } else if (const auto env = normalise_client_fps(env_fps)) {
      fps = *env;
    }
    return std::clamp(fps, min_fps, max_fps);
  }
}  // namespace netstress
