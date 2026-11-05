/**
 * @file src/virtual_display_types.h
 * @brief Platform-neutral types shared by the virtual display orchestrator and its backends.
 */
#pragma once

// standard includes
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace virtual_display {
  /**
   * @brief Requested display mode of a virtual display.
   */
  struct mode_t {
    uint32_t width;  ///< Width in pixels.
    uint32_t height;  ///< Height in pixels.
    uint32_t fps;  ///< Refresh rate in frames per second (or millihertz when >= 1000).
  };

  /**
   * @brief State of the virtual display driver connection.
   */
  enum class driver_status_e {
    unknown,  ///< The driver has not been opened yet.
    ok,  ///< The driver is open and usable.
    failed,  ///< The driver could not be opened.
    version_incompatible,  ///< The driver protocol version is not compatible.
    watchdog_failed  ///< Pinging the driver failed and the connection was dropped.
  };

  /**
   * @brief Platform-neutral 16 byte identifier of a virtual monitor.
   */
  struct guid_t {
    std::array<uint8_t, 16> bytes {};  ///< Raw identifier bytes.

    /**
     * @brief Compare two identifiers.
     * @param other The identifier to compare with.
     * @return True when all bytes are equal.
     */
    bool operator==(const guid_t &other) const = default;
  };

  /**
   * @brief Pure interface of a virtual display driver backend.
   */
  class backend_t {
  public:
    virtual ~backend_t() = default;

    /**
     * @brief Open the driver connection.
     * @return The resulting driver status.
     */
    virtual driver_status_e open() = 0;

    /**
     * @brief Create (or reuse) a virtual monitor.
     * @param mode The requested display mode.
     * @param guid Stable identifier of the monitor.
     * @param serial Serial number reported in the monitor EDID.
     * @return The GDI device name (UTF-8) of the new monitor, or nullopt on failure.
     */
    virtual std::optional<std::string> create(const mode_t &mode, const guid_t &guid, std::string_view serial) = 0;

    /**
     * @brief Remove a virtual monitor.
     * @param guid Identifier of the monitor to remove.
     * @return True when the monitor was removed.
     */
    virtual bool remove(const guid_t &guid) = 0;

    /**
     * @brief Get the current driver status.
     * @return The driver status.
     */
    virtual driver_status_e status() const = 0;
  };
}  // namespace virtual_display
